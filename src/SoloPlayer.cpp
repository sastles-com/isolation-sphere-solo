/**
 * @file SoloPlayer.cpp
 * @brief SoloPlayer実装
 */

#include "SoloPlayer.h"

#include <esp_timer.h>

namespace sastle {

namespace {
constexpr int64_t kPeriodUs = 1000000LL / kSoloFps;  ///< 100ms @10fps
constexpr TickType_t kMutexWait = pdMS_TO_TICKS(50);
}  // namespace

SoloPlayer::SoloPlayer()
    : _config(nullptr),
      _image(nullptr),
      _width(320),
      _height(160),
      _frameBuf(nullptr),
      _frameCap(0),
      _mutex(nullptr),
      _task(nullptr),
      _state(State::NoVideo),
      _lastError(nullptr),
      _videoBytes(0),
      _videoFrames(0),
      _frames(0),
      _loops(0),
      _deadlineMisses(0),
      _decodeErrors(0),
      _lastFrameBytes(0),
      _lastReadUs(0),
      _lastTickUs(0),
      _fps(0.0f),
      _fpsCount(0),
      _fpsTimestamp(0) {}

SoloPlayer::~SoloPlayer() {
    if (_task) {
        vTaskDelete(_task);
        _task = nullptr;
    }
    _reader.close();
    if (_frameBuf) {
        free(_frameBuf);
        _frameBuf = nullptr;
    }
    if (_mutex) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
}

bool SoloPlayer::begin(ConfigManager& config, ImageManager& image) {
    _config = &config;
    _image = &image;
    _videoPath = config.getSoloVideoPath();
    _width = (uint16_t)config.getImageWidth();
    _height = (uint16_t)config.getImageHeight();

    if (!_mutex) {
        _mutex = xSemaphoreCreateMutex();
        if (!_mutex) {
            Serial.println("[SoloPlayer] ERROR: mutex allocation failed");
            return false;
        }
    }

    if (!_frameBuf) {
        _frameCap = kSoloMaxFrameBytes;
        _frameBuf = (uint8_t*)ps_malloc(_frameCap);
        if (!_frameBuf) {
            Serial.println("[SoloPlayer] ERROR: PSRAM frame buffer allocation failed");
            return false;
        }
    }

    Serial.printf("[SoloPlayer] video=%s expect=%ux%u fps=%u max_frame=%u bytes\n",
                  _videoPath.c_str(), _width, _height, (unsigned)kSoloFps, (unsigned)_frameCap);

    reload();
    return true;
}

bool SoloPlayer::startTask(uint8_t core, uint8_t priority, uint32_t stackSize) {
    if (_task) {
        return true;
    }
    BaseType_t r = xTaskCreatePinnedToCore(taskFunc, "solo_play", stackSize, this, priority, &_task, core);
    if (r != pdPASS) {
        Serial.println("[SoloPlayer] Failed to create playback task");
        _task = nullptr;
        return false;
    }
    Serial.printf("[SoloPlayer] Playback task pinned to core %u (prio %u)\n", core, priority);
    return true;
}

// ---------------------------------------------------------------------------
// 状態操作 (HTTP / シリアルコンソール側から呼ばれる)
// ---------------------------------------------------------------------------

void SoloPlayer::play() {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return;
    if (_state == State::Stopped && _reader.isOpen()) {
        _state = State::Playing;
        Serial.println("[SoloPlayer] play");
    }
    xSemaphoreGive(_mutex);
}

void SoloPlayer::stop() {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return;
    if (_state == State::Playing) {
        _state = State::Stopped;
        Serial.println("[SoloPlayer] stop");
    }
    xSemaphoreGive(_mutex);
}

bool SoloPlayer::reload() {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;
    closeVideoLocked();
    const bool ok = openVideoLocked();
    xSemaphoreGive(_mutex);
    return ok;
}

bool SoloPlayer::beginUpload() {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;
    if (_state == State::Uploading) {
        xSemaphoreGive(_mutex);
        return false;
    }
    closeVideoLocked();
    _state = State::Uploading;
    _lastError = nullptr;
    Serial.println("[SoloPlayer] upload begin (playback paused)");
    xSemaphoreGive(_mutex);
    return true;
}

void SoloPlayer::endUpload() {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return;
    // Uploading 以外なら何もしない (二重呼び出し保護)
    if (_state == State::Uploading) {
        _state = State::NoVideo;
        openVideoLocked();
        Serial.printf("[SoloPlayer] upload end -> %s\n", stateName());
    }
    xSemaphoreGive(_mutex);
}

bool SoloPlayer::validateFile(const char* path, MjpegInfo& info, const char** errorOut) {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;
    bool ok = false;
    if (_state != State::Uploading || _reader.isOpen()) {
        if (errorOut) *errorOut = "internal: validate requires upload state";
    } else {
        ok = MjpegReader::validate(path, _width, _height, _frameCap, _frameBuf, _frameCap, info, errorOut);
    }
    xSemaphoreGive(_mutex);
    return ok;
}

// ---------------------------------------------------------------------------
// 内部 (mutex 取得済み)
// ---------------------------------------------------------------------------

bool SoloPlayer::openVideoLocked() {
    _videoBytes = 0;
    _videoFrames = 0;

    if (!LittleFS.exists(_videoPath.c_str())) {
        _state = State::NoVideo;
        _lastError = nullptr;
        Serial.printf("[SoloPlayer] no video at %s\n", _videoPath.c_str());
        return false;
    }

    // 起動時/差し替え時にファイル全体を検証し、部分ファイルや解像度違いを再生しない。
    MjpegInfo info;
    const char* err = nullptr;
    if (!MjpegReader::validate(_videoPath.c_str(), _width, _height, _frameCap,
                               _frameBuf, _frameCap, info, &err)) {
        setErrorLocked(err ? err : "invalid video file");
        Serial.printf("[SoloPlayer] video rejected: %s (got %ux%u, %u frames)\n",
                      _lastError, info.width, info.height, (unsigned)info.frames);
        return false;
    }

    if (!_reader.open(_videoPath.c_str(), _frameBuf, _frameCap)) {
        setErrorLocked("failed to open video file");
        return false;
    }

    _videoBytes = info.fileBytes;
    _videoFrames = info.frames;
    _lastError = nullptr;
    _state = State::Playing;
    Serial.printf("[SoloPlayer] video ok: %u frames, %u bytes, max frame %u bytes (%.1fs @%ufps)\n",
                  (unsigned)info.frames, (unsigned)info.fileBytes, (unsigned)info.maxFrameBytes,
                  (float)info.frames / (float)kSoloFps, (unsigned)kSoloFps);
    return true;
}

void SoloPlayer::closeVideoLocked() {
    _reader.close();
    if (_state == State::Playing || _state == State::Stopped) {
        _state = State::NoVideo;
    }
}

void SoloPlayer::setErrorLocked(const char* msg) {
    _reader.close();
    _state = State::Error;
    _lastError = msg;
}

// ---------------------------------------------------------------------------
// 再生タスク
// ---------------------------------------------------------------------------

void SoloPlayer::tick() {
    if (_state != State::Playing) {
        return;
    }
    // HTTP 側が操作中なら今回のフレームは見送る (次の締切で再試行)
    if (xSemaphoreTake(_mutex, kMutexWait) != pdTRUE) {
        return;
    }

    if (_state == State::Playing && _reader.isOpen()) {
        const int64_t t0 = esp_timer_get_time();
        size_t size = 0;
        bool wrapped = false;
        const MjpegReader::Status st = _reader.next(size, wrapped);
        _lastReadUs = (uint32_t)(esp_timer_get_time() - t0);

        switch (st) {
            case MjpegReader::Status::Ok:
                if (wrapped) {
                    _loops++;
                }
                _lastFrameBytes = (uint32_t)size;
                if (_image->submitJpegFrame(_frameBuf, size)) {
                    _frames++;
                    _fpsCount++;
                } else {
                    _decodeErrors++;
                }
                break;
            case MjpegReader::Status::Empty:
                setErrorLocked("video file has no frames");
                break;
            case MjpegReader::Status::Corrupt:
                setErrorLocked("video file is corrupt");
                break;
            case MjpegReader::Status::TooLarge:
                setErrorLocked("frame exceeds size limit");
                break;
            case MjpegReader::Status::NotOpen:
                _state = State::NoVideo;
                break;
        }
        _lastTickUs = (uint32_t)(esp_timer_get_time() - t0);

        const unsigned long now = millis();
        if (now - _fpsTimestamp >= 1000) {
            _fps = (_fpsCount * 1000.0f) / (float)(now - _fpsTimestamp);
            _fpsCount = 0;
            _fpsTimestamp = now;
        }
    }

    xSemaphoreGive(_mutex);
}

void SoloPlayer::taskFunc(void* param) {
    SoloPlayer* self = static_cast<SoloPlayer*>(param);
    Serial.printf("[SoloPlayer] task started on core %d\n", xPortGetCoreID());

    // 単調増加クロック (esp_timer) で締切を管理する。処理時間ぶん待ち時間を削るので、
    // 「デコード後に100ms待つ」方式のような累積ドリフトが起きない。
    int64_t next = esp_timer_get_time() + kPeriodUs;
    for (;;) {
        self->tick();

        const int64_t now = esp_timer_get_time();
        if (now < next) {
            const int64_t waitUs = next - now;
            TickType_t ticks = pdMS_TO_TICKS(waitUs / 1000);
            vTaskDelay(ticks > 0 ? ticks : 1);
        } else {
            // 締切超過: 遅れたぶんの締切は捨てて次の締切へ揃える (遅延を溜め込まない)。
            const int64_t missed = (now - next) / kPeriodUs;
            if (missed > 0) {
                self->_deadlineMisses += (uint32_t)missed;
                next += missed * kPeriodUs;
            }
            vTaskDelay(1);  // busy loop 防止
        }
        next += kPeriodUs;
    }
}

// ---------------------------------------------------------------------------
// 参照系
// ---------------------------------------------------------------------------

const char* SoloPlayer::stateName() const {
    switch (_state) {
        case State::NoVideo:   return "no_video";
        case State::Playing:   return "playing";
        case State::Stopped:   return "stopped";
        case State::Uploading: return "uploading";
        case State::Error:     return "error";
    }
    return "unknown";
}

SoloPlayer::Stats SoloPlayer::stats() const {
    Stats s;
    s.frames = _frames;
    s.loops = _loops;
    s.deadlineMisses = _deadlineMisses;
    s.decodeErrors = _decodeErrors;
    s.lastFrameBytes = _lastFrameBytes;
    s.lastReadUs = _lastReadUs;
    s.lastTickUs = _lastTickUs;
    s.fps = _fps;
    return s;
}

}  // namespace sastle
