#include "FramePump.h"

#include <esp_timer.h>

#include "Log.h"

namespace sastle {

FramePump::~FramePump() {
    stopForOta(200);
    if (_netBuf) {
        free(_netBuf);
        _netBuf = nullptr;
    }
}

bool FramePump::begin(const Deps& deps, const SourceConfig& cfg) {
    _d = deps;
    if (!_d.image) {
        Serial.println("[FramePump] ERROR: ImageManager required");
        return false;
    }
    if (!_netBuf) {
        _netBufSize = kNetBufSize;
        _netBuf = (uint8_t*)ps_malloc(_netBufSize);
        if (!_netBuf) {
            Serial.println("[FramePump] ERROR: PSRAM reassembly buffer allocation failed");
            return false;
        }
    }
    _reasm.begin(_netBuf, _netBufSize);

    SourceArbiter::Mode mode = SourceArbiter::Mode::Auto;
    if (!SourceArbiter::parseMode(cfg.mode.c_str(), mode)) {
        Serial.printf("[FramePump] WARN: unknown source.mode \"%s\" -> auto\n", cfg.mode.c_str());
    }
    _arb.configure(mode, cfg.idle_timeout_ms, 1000000UL / kSoloFps);
    _idleBlank = cfg.idle_blank;

    Serial.printf("[FramePump] mode=%s idle_timeout=%lums idle_blank=%d udp=%s local_period=%lums\n",
                  SourceArbiter::modeName(mode), (unsigned long)cfg.idle_timeout_ms, _idleBlank ? 1 : 0,
                  _d.udp ? "yes" : "no", (unsigned long)(1000 / kSoloFps));
    return true;
}

bool FramePump::startTask(uint8_t core, uint8_t priority, uint32_t stackSize) {
    if (_task) {
        return true;
    }
    _stopRequested = false;
    _running = true;
    BaseType_t r = xTaskCreatePinnedToCore(taskFunc, "frame_pump", stackSize, this, priority, &_task, core);
    if (r != pdPASS) {
        Serial.println("[FramePump] Failed to create task");
        _task = nullptr;
        _running = false;
        return false;
    }
    Serial.printf("[FramePump] task pinned to core %u (prio %u, stack %lu)\n", core, priority,
                  (unsigned long)stackSize);
    return true;
}

void FramePump::stopForOta(uint32_t waitMs) {
    if (_task) {
        _stopRequested = true;
        for (uint32_t i = 0; i < waitMs / 10 && _running; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (_running) {
            // 抜けてこない (デコード中に固まっている等)。最後の手段。
            Serial.println("[FramePump] WARN: task did not stop in time - forcing delete");
            vTaskDelete(_task);
            _running = false;
        }
        _task = nullptr;
    }
    if (_d.udp) {
        _d.udp->end();
    }
}

bool FramePump::setSourceMode(SourceArbiter::Mode m) {
    if (m == _arb.mode()) return true;
    Log.printf("[SRC] mode %s -> %s\n", SourceArbiter::modeName(_arb.mode()), SourceArbiter::modeName(m));
    _arb.setMode(m);
    return true;
}

SourceArbiter::Active FramePump::activeSource() const {
    const bool playing = _d.player && _d.player->state() == SoloPlayer::State::Playing;
    return _arb.active(millis(), playing);
}

FramePump::Stats FramePump::stats() const {
    Stats s;
    s.netFrames = _netFrames;
    s.netDecodeErrors = _netDecodeErrors;
    s.udpDatagrams = _udpDatagrams;
    s.reasmDropped = _reasm.framesDropped();
    s.netFps = _netFps;
    s.lastNetMs = _arb.lastNetMs();
    s.stackMinWords = _stackMin;
    return s;
}

// ---------------------------------------------------------------------------
// タスク
// ---------------------------------------------------------------------------

void FramePump::taskFunc(void* param) {
    static_cast<FramePump*>(param)->loop();
}

void FramePump::onNetIdle() {
    // 配信が途切れた: ローカル締切を置き直して (miss を溜めない) 続きから再生する。
    // 再生中でなければ黒 (最後の配信フレームを残さない。idle_blank=false なら保持)。
    _arb.rearm(esp_timer_get_time());
    const bool playing = _d.player && _d.player->state() == SoloPlayer::State::Playing;
    Log.printf("[SRC] network -> %s (idle)\n", playing ? "local" : (_idleBlank ? "black" : "hold"));
    if (!playing && _idleBlank) {
        _d.image->requestBlack();
    }
}

void FramePump::loop() {
    Serial.printf("[FramePump] task started on core %d\n", xPortGetCoreID());
    _arb.rearm(esp_timer_get_time());
    _netFpsTs = millis();

    for (;;) {
        if (_stopRequested) {
            break;
        }

        const uint32_t nowMs = millis();
        const bool live = _arb.netLive(nowMs);
        if (!live) {
            // 黒要求 (停止/削除/エラー) は配信が表示を握っていない間だけ実行する
            _d.image->serviceBlankRequest();
        }

        // 次のローカル締切まで UDP を待つ (受信器が無ければ単に待つ)
        const uint32_t waitUs = _arb.waitUs(esp_timer_get_time());
        TickType_t ticks = pdMS_TO_TICKS(waitUs / 1000);
        if (waitUs > 0 && ticks == 0) ticks = 1;

        bool got = false;
        if (_d.udp && _d.udp->listening()) {
            got = _d.udp->recv(_dg, ticks);
        } else if (ticks > 0) {
            vTaskDelay(ticks);
        }

        if (got) {
            _udpDatagrams++;
            if (_arb.mode() != SourceArbiter::Mode::Local) {
                size_t jpegSize = 0;
                if (_reasm.addChunk(_dg.data, _dg.len, jpegSize)) {
                    const bool wasLive = _arb.netLive(millis());
                    if (_d.image->submitJpegFrame(_netBuf, jpegSize)) {
                        _netFrames++;
                        _netFpsCount++;
                        _arb.onNetFrame(millis());
                        if (!wasLive) {
                            Log.printf("[SRC] %s -> network (udp frame %u B from %s)\n",
                                       (_d.player && _d.player->state() == SoloPlayer::State::Playing) ? "local" : "idle",
                                       (unsigned)jpegSize, _d.udp->remoteIP().toString().c_str());
                        }
                    } else {
                        _netDecodeErrors++;
                    }
                    // 生配信中はローカル締切を追従させる (miss を数えない)。デコード直後は
                    // 1 tick 譲る: 15fps で連続デコードすると core0 の IDLE が飢えて TASK_WDT
                    // になる (派生元で実測)。
                    _arb.rearm(esp_timer_get_time());
                    vTaskDelay(1);
                }
            }
            // Local モード中は読み捨て (lwIP のキューを詰まらせない)

            const uint32_t t = millis();
            if (t - _netFpsTs >= 1000) {
                _netFps = _netFpsCount * 1000.0f / (float)(t - _netFpsTs);
                _netFpsCount = 0;
                _netFpsTs = t;
            }
            continue;
        }

        // 締切 (または受信器なしの待ち明け)
        uint32_t missed = 0;
        if (_arb.localDue(esp_timer_get_time(), missed)) {
            const uint32_t t = millis();
            if (_arb.takeNetIdleEdge(t)) {
                onNetIdle();
                _netFps = 0.0f;
                continue;   // 置き直した締切で次へ
            }
            if (_arb.localAllowed(t) && _d.player) {
                if (missed) _d.player->addDeadlineMisses(missed);
                _d.player->tick();
            }
            _stackMin = uxTaskGetStackHighWaterMark(nullptr);
        }
    }

    Serial.println("[FramePump] task exiting (cooperative stop)");
    _running = false;
    vTaskDelete(nullptr);
}

}  // namespace sastle
