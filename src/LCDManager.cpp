/**
 * @file LCDManager.cpp
 * @brief LCD表示管理クラス実装
 */

#include "LCDManager.h"
#include "common.h"

namespace sastle {

LCDManager::LCDManager() 
    : _config(nullptr)
    , _debugEnabled(false)
    , _initialized(false)
    , _lcdWidth(128)
    , _lcdHeight(128)
    , _rotation(0)
{
}

LCDManager::~LCDManager() {
}

bool LCDManager::begin(ConfigManager* config) {
    if (!config) {
        DEBUG_PRINTLN("[LCDManager] Error: ConfigManager is null");
        return false;
    }
    
    _config = config;
    _debugEnabled = _config->getLCDDebugEnabled();

#if !BOARD_HAS_LCD
    // LCD非搭載ボード: 常に無効・no-op (update() 等は呼ばれない)
    _debugEnabled = false;
    DEBUG_PRINTLN("[LCDManager] No LCD on this board (no-op)");
    return true;
#else
    if (!_debugEnabled) {
        DEBUG_PRINTLN("[LCDManager] LCD debug disabled");
        return true;  // デバッグ無効時も成功扱い
    }

    // M5Unified LCD初期化
    auto cfg = M5.config();
    M5.begin(cfg);
    
    _lcdWidth = M5.Display.width();
    _lcdHeight = M5.Display.height();
    _rotation = 0;  // config.jsonから取得可能

    // 一括転送(pushImage)用バッファ。1画素ずつ writePixel すると 128x128 で
    // ~150ms/フレームかかり描画FPSの律速になっていたため、全画素を組んで
    // 1回の pushImage で転送する。
    _lcdBuf = (uint16_t*)malloc((size_t)_lcdWidth * _lcdHeight * sizeof(uint16_t));

    M5.Display.setRotation(_rotation);
    M5.Display.setBrightness(128);  // 初期輝度50%
    M5.Display.fillScreen(TFT_BLACK);
    
    _initialized = true;
    
    DEBUG_PRINTF("[LCDManager] Initialized: %dx%d, rotation=%d, debug=%s\n",
                 _lcdWidth, _lcdHeight, _rotation, 
                 _debugEnabled ? "ON" : "OFF");
    
    // 起動メッセージ表示
    drawText("LCD Ready", 10, 50, 2, TFT_GREEN);
    delay(1000);
    clear();

    return true;
#endif // BOARD_HAS_LCD
}

void LCDManager::update(ImageManager* imageManager) {
    if (!_initialized || !_debugEnabled || !imageManager || !_lcdBuf) {
        return;
    }

#if BOARD_HAS_LCD
    // スロットル: デバッグ表示は最大 ~10Hz で十分。毎フレーム描くと描画FPSを縛る。
    unsigned long now = millis();
    if (now - _lastUpdateMs < 100) {
        return;
    }
    _lastUpdateMs = now;

    uint16_t imgWidth = imageManager->getWidth();
    uint16_t imgHeight = imageManager->getHeight();

    // 全画素をバッファに組み立て (RAM上、高速) → 1回の pushImage で一括転送。
    // 旧実装は writePixel を 128x128=16384回呼び ~150ms かかっていた。
    for (uint16_t y = 0; y < _lcdHeight; y++) {
        uint16_t srcY = (y * imgHeight) / _lcdHeight;
        uint16_t* row = _lcdBuf + (size_t)y * _lcdWidth;
        for (uint16_t x = 0; x < _lcdWidth; x++) {
            uint16_t srcX = (x * imgWidth) / _lcdWidth;
            uint8_t r, g, b;
            imageManager->getPixel(srcX, srcY, r, g, b);
            row[x] = M5.Display.color565(r, g, b);
        }
    }
    M5.Display.pushImage(0, 0, _lcdWidth, _lcdHeight, _lcdBuf);
#endif // BOARD_HAS_LCD
}

void LCDManager::drawStatus(const LcdStatus& s) {
    if (!_initialized || !_debugEnabled) {
        return;
    }
#if BOARD_HAS_LCD
    // ~3Hz にスロットル (映像レンダリングを縛らない)。点滅もこの周期。
    unsigned long now = millis();
    if (now - _lastStatusMs < 300) {
        return;
    }
    _lastStatusMs = now;
    _heartbeat = !_heartbeat;

    // ちらつき防止のためスプライトへ描いて一括転送 (遅延生成)
    if (!_statusCanvas) {
        _statusCanvas = new M5Canvas(&M5.Display);
        _statusCanvas->createSprite(_lcdWidth, _lcdHeight);
    }
    M5Canvas& c = *_statusCanvas;
    c.fillSprite(TFT_BLACK);

    // ハートビート + タイトル
    c.fillCircle(12, 12, 5, _heartbeat ? TFT_GREEN : 0x0320);
    c.setTextColor(TFT_GREEN);
    c.setTextSize(2);
    c.setCursor(26, 5);
    c.print("STANDBY");

    // 情報行 (device 自前のライブ値)
    c.setTextSize(1);
    c.setTextColor(TFT_WHITE);
    int y = 34;
    const int dy = 14;
    uint32_t up = s.uptime_s;
    c.setCursor(4, y); c.printf("up   %02u:%02u:%02u",
                                (unsigned)(up / 3600), (unsigned)((up % 3600) / 60), (unsigned)(up % 60)); y += dy;
    c.setCursor(4, y); c.printf("ip   %s", s.ip ? s.ip : "-"); y += dy;
    c.setCursor(4, y); c.printf("wifi %u client", (unsigned)s.clients); y += dy;
    c.setCursor(4, y); c.printf("heap %uKB", (unsigned)(s.free_heap / 1024)); y += dy;
    c.setCursor(4, y); c.printf("fps  %.1f", s.fps); y += dy;

    // IMU: pitch をテキスト + バー表示 (動く=描画ループ生存の証明)
    if (s.imu_ok) {
        float pitch = atan2f(2.0f * (s.qw * s.qx + s.qy * s.qz),
                             1.0f - 2.0f * (s.qx * s.qx + s.qy * s.qy));
        c.setCursor(4, y); c.printf("imu %+.2f", pitch);
        const int bx = 66, bw = 58;
        c.drawRect(bx, y - 1, bw, 9, 0x4208);
        int px = bx + bw / 2 + (int)(pitch / 3.14159f * (bw / 2));
        if (px < bx + 1) px = bx + 1;
        if (px > bx + bw - 3) px = bx + bw - 3;
        c.fillRect(px, y, 2, 7, TFT_GREEN);
    }

    c.pushSprite(0, 0);
#endif // BOARD_HAS_LCD
}

void LCDManager::drawWifiQr(const char* wifiQrText, const char* ssid, const char* url) {
    if (!_initialized || !_debugEnabled || !wifiQrText) {
        return;
    }
#if BOARD_HAS_LCD
    unsigned long now = millis();
    if (now - _lastStatusMs < 500) {
        return;
    }
    _lastStatusMs = now;
    _heartbeat = !_heartbeat;

    if (!_statusCanvas) {
        _statusCanvas = new M5Canvas(&M5.Display);
        _statusCanvas->createSprite(_lcdWidth, _lcdHeight);
    }
    M5Canvas& c = *_statusCanvas;
    c.fillSprite(TFT_WHITE);  // QR は白地に黒が読み取りやすい

    // バイトモード / 誤り訂正 L の容量: v3=53, v4=78, v5=106, v6=134 文字
    const size_t len = strlen(wifiQrText);
    uint8_t version = 3;
    if (len > 106)     version = 6;
    else if (len > 78) version = 5;
    else if (len > 53) version = 4;

    // 128x128 のうち上 104px を QR に使い、下に SSID / URL を小さく出す
    const int32_t qrSize = 100;
    c.qrcode(wifiQrText, (_lcdWidth - qrSize) / 2, 2, qrSize, version);

    c.setTextSize(1);
    c.setTextColor(TFT_BLACK);
    c.setCursor(4, 106);
    c.printf("Wi-Fi %s", ssid ? ssid : "");
    c.setCursor(4, 117);
    c.print(url ? url : "");
    // 生存表示 (点滅)
    c.fillCircle(_lcdWidth - 6, _lcdHeight - 6, 3, _heartbeat ? TFT_GREEN : TFT_WHITE);

    c.pushSprite(0, 0);
#endif // BOARD_HAS_LCD
}

void LCDManager::drawText(const char* text, int16_t x, int16_t y, uint8_t textSize, uint16_t color) {
    if (!_initialized || !_debugEnabled) {
        return;
    }
#if BOARD_HAS_LCD
    M5.Display.setTextSize(textSize);
    M5.Display.setTextColor(color);
    M5.Display.setCursor(x, y);
    M5.Display.print(text);
#endif
}

void LCDManager::clear(uint16_t color) {
    if (!_initialized || !_debugEnabled) {
        return;
    }
#if BOARD_HAS_LCD
    M5.Display.fillScreen(color);
#endif
}

} // namespace sastle
