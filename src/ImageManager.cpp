/**
 * @file ImageManager.cpp
 * @brief ImageManager実装
 */

#include "ImageManager.h"

namespace sastle {

// 静的メンバー初期化
ImageManager* ImageManager::_instance = nullptr;

ImageManager::ImageManager()
    : _initialized(false),
      _width(320),
      _height(160),
      _bufferSize(0),
      _framesDecoded(0),
      _framesDropped(0),
      _decodeErrors(0),
      _lastFrameTime(0),
      _fpsTimestamp(0),
      _fpsFrameCount(0),
      _currentFPS(0.0f),
      _lastJpegSize(0),
      _tjpgTargetBuffer(nullptr) {
    _instance = this;
}

ImageManager::~ImageManager() {
    end();
    _instance = nullptr;
}

bool ImageManager::begin(ConfigManager& config) {
    if (_initialized) {
        Serial.println("[ImageManager] Already initialized");
        return true;
    }

    ImageConfig imgConfig = config.getImageConfig();
    _width = imgConfig.width;
    _height = imgConfig.height;
    _bufferSize = (size_t)_width * _height * sizeof(uint16_t); // RGB565

    Serial.printf("[ImageManager] Initializing: %dx%d RGB565\n", _width, _height);
    Serial.printf("  Buffer size: %u bytes x3\n", (unsigned)_bufferSize);

    // PSRAMチェック
    if (!psramFound()) {
        Serial.println("[ImageManager] ERROR: PSRAM not found!");
        return false;
    }

    Serial.printf("  Free PSRAM: %d bytes\n", ESP.getFreePsram());

    // バッファ確保
    if (!allocateBuffers()) {
        Serial.println("[ImageManager] ERROR: Buffer allocation failed");
        return false;
    }

    // TJpg_Decoder初期化 (320x160 をそのままデコード = scale 1)
    TJpgDec.setJpgScale(1);
    // デコード結果は uint16_t バッファへ memcpy 格納し、getPixel() が
    // ネイティブ RGB565 (R=15..11, G=10..5, B=4..0) としてビット抽出する。
    // そのため TFT 転送用のバイトスワップは掛けない (true にすると保存値の
    // 上位/下位バイトが入れ替わり、読み出し時に色チャンネルが崩れて BGR 風になる)。
    TJpgDec.setSwapBytes(false);
    TJpgDec.setCallback(tjpgOutput);

    _initialized = true;
    _fpsTimestamp = millis();

    Serial.println("[ImageManager] Initialized successfully");

    return true;
}

void ImageManager::end() {
    if (!_initialized) {
        return;
    }

    freeBuffers();
    _initialized = false;

    Serial.println("[ImageManager] Deinitialized");
}

bool ImageManager::allocateBuffers() {
    // RGB565 トリプルバッファ
    if (!_pool.allocate(_bufferSize)) {
        Serial.println("[ImageManager] Failed to allocate frame buffers");
        return false;
    }

    _displayBuffer = _pool.displayBuffer();
    _decodeBuffer = _pool.decodeBuffer();

    Serial.printf("[ImageManager] Buffers allocated. Free PSRAM: %d bytes\n", ESP.getFreePsram());
    return true;
}

void ImageManager::freeBuffers() {
    _pool.freeAll();
    _displayBuffer = nullptr;
    _decodeBuffer = nullptr;
}

// render側(毎パス): 表示待ちの完成フレームがあれば display に採用し、_displayBuffer を更新する。
void ImageManager::adoptReadyFrame() {
    _pool.adopt();
    _displayBuffer = _pool.displayBuffer();
}

bool ImageManager::submitJpegFrame(const uint8_t* jpeg, size_t size) {
    if (!_initialized || !jpeg || size == 0) {
        return false;
    }
    _lastJpegSize = size;
    if (!decodeJPEG(jpeg, size)) {
        _decodeErrors++;
        return false;
    }
    publishFrame();
    _framesDecoded++;
    _lastFrameTime = millis();
    _fpsFrameCount++;
    calculateFPS();
    return true;
}

bool ImageManager::decodeJPEG(const uint8_t* jpeg_data, size_t jpeg_size) {
    // デコードターゲットバッファを設定
    _tjpgTargetBuffer = _decodeBuffer;

    // JPEGヘッダから解像度を取得して検証
    uint16_t w = 0, h = 0;
    if (TJpgDec.getJpgSize(&w, &h, jpeg_data, jpeg_size) != JDR_OK) {
        Serial.println("[ImageManager] Failed to get JPEG size");
        return false;
    }
    if (w != _width || h != _height) {
        Serial.printf("[ImageManager] Size mismatch: expected %dx%d, got %dx%d\n",
                     _width, _height, w, h);
        return false;
    }

    // デコード実行 (所要時間を計測)
    unsigned long decodeStart = micros();
    if (TJpgDec.drawJpg(0, 0, jpeg_data, jpeg_size) != JDR_OK) {
        Serial.println("[ImageManager] JPEG decode failed");
        return false;
    }
    _lastDecodeUs = (uint32_t)(micros() - decodeStart);

    return true;
}

// decode側: 完成フレームを ready に公開し、次の書込先(_decodeBuffer)を更新する。
void ImageManager::publishFrame() {
    bool dropped = _pool.publish();
    _decodeBuffer = _pool.decodeBuffer();
    if (dropped) {
        _framesDropped++;  // render が追いつかず表示前に破棄
    }
}

void ImageManager::calculateFPS() {
    unsigned long now = millis();
    unsigned long elapsed = now - _fpsTimestamp;

    // 1秒ごとにFPS計算
    if (elapsed >= 1000) {
        _currentFPS = (_fpsFrameCount * 1000.0f) / elapsed;
        _fpsFrameCount = 0;
        _fpsTimestamp = now;
    }
}

bool ImageManager::getPixel(uint16_t x, uint16_t y, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (!_initialized || !_displayBuffer) {
        return false;
    }

    if (x >= _width || y >= _height) {
        return false;
    }

    // RGB565を取得
    uint16_t rgb565 = _displayBuffer[y * _width + x];

    // RGB565 -> RGB888変換
    r = ((rgb565 >> 11) & 0x1F) << 3;  // R: 5bit -> 8bit
    g = ((rgb565 >> 5) & 0x3F) << 2;   // G: 6bit -> 8bit
    b = (rgb565 & 0x1F) << 3;          // B: 5bit -> 8bit

    return true;
}

ImageStats ImageManager::getStats() const {
    ImageStats stats;
    stats.frames_decoded = _framesDecoded;
    stats.frames_dropped = _framesDropped;
    stats.decode_errors = _decodeErrors;
    stats.fps = _currentFPS;
    stats.last_frame_time = _lastFrameTime;
    stats.last_jpeg_size = _lastJpegSize;
    stats.decode_time_us = _lastDecodeUs;
    return stats;
}

void ImageManager::printStats() {
    Serial.println("\n=== ImageManager Statistics ===");
    Serial.printf("Frames Decoded:  %u\n", _framesDecoded);
    Serial.printf("Frames Dropped:  %u\n", _framesDropped);
    Serial.printf("Decode Errors:   %u\n", _decodeErrors);
    Serial.printf("Current FPS:     %.2f\n", _currentFPS);
    Serial.printf("Last JPEG Size:  %zu bytes\n", _lastJpegSize);
    Serial.printf("Buffer Size:     %zu bytes x3\n", _bufferSize);
    Serial.printf("Free PSRAM:      %d bytes\n", ESP.getFreePsram());
}

// TJpg_Decoderコールバック (静的)
bool ImageManager::tjpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (!_instance || !_instance->_tjpgTargetBuffer) {
        return false;
    }

    // ビットマップをターゲットバッファにコピー
    uint16_t* target = _instance->_tjpgTargetBuffer;
    uint16_t width = _instance->_width;

    for (uint16_t row = 0; row < h; row++) {
        uint16_t* dest = target + ((y + row) * width + x);
        uint16_t* src = bitmap + (row * w);
        memcpy(dest, src, w * sizeof(uint16_t));
    }

    return true;  // 継続
}

} // namespace sastle
