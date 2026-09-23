/**
 * @file TimeSync.cpp
 * @brief TimeSync の実装
 * @author sastle-com
 * @date 2026-07-08
 */

#include "TimeSync.h"
#include <ArduinoJson.h>

namespace sastle {

int64_t TimeSync::monotonicMs() {
    // millis() は 32bit で約49.7日でラップする。呼び出しの度に前回値と比較し、
    // 減少していればラップしたとみなして上位カウンタを進める。update()/syncedNow()
    // がラップ周期より十分短い間隔で呼ばれる前提 (ビーコン1秒周期・loop 毎で満たす)。
    uint32_t now = millis();
    if (now < _lastMillis) {
        _wrapCount++;
    }
    _lastMillis = now;
    return ((int64_t)_wrapCount << 32) | (int64_t)now;
}

void TimeSync::applySample(int64_t sample) {
    if (!_synced) {
        // 初回は即確定
        _offsetMs = sample;
        _synced = true;
        _outlierCount = 0;
        return;
    }

    int64_t diff = sample - _offsetMs;
    int64_t absDiff = diff < 0 ? -diff : diff;

    if (absDiff > OUTLIER_THRESHOLD_MS) {
        // 外れ値: WiFi ジッタのスパイクを弾く。ただし連続して外れ続ける場合は
        // サーバー時刻が本当に飛んだ (再起動等) とみなして追従再同期する。
        if (++_outlierCount < OUTLIER_LIMIT) {
            return;
        }
        _offsetMs = sample;
        _outlierCount = 0;
        return;
    }

    // EMA ローパス: offset += (sample - offset) * α  (α = 1/2^EMA_SHIFT)
    _offsetMs += diff >> EMA_SHIFT;
    _outlierCount = 0;
}

bool TimeSync::onClockMessage(const char* payload) {
    StaticJsonDocument<128> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        return false;
    }
    if (!doc.containsKey("epoch_ms")) {
        return false;
    }

    int64_t epochMs = doc["epoch_ms"].as<int64_t>();
    _lastSeq = doc["seq"] | _lastSeq;

    // サンプル = 受信時点で「サーバー時刻 - 自分の単調時刻」。
    applySample(epochMs - monotonicMs());
    return true;
}

int64_t TimeSync::syncedNow() {
    return _offsetMs + monotonicMs();
}

} // namespace sastle
