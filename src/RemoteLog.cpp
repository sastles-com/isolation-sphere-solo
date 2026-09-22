#include "RemoteLog.h"
#include <string.h>
#include <esp_heap_caps.h>

namespace sastle {

RemoteLog Log;

void RemoteLog::begin() {
    if (!_mutex) {
        _mutex = xSemaphoreCreateMutex();
    }
    if (!_backlog) {
        // 退避は PSRAM に置く (内部 RAM 12KB を空けておく)。PSRAM が無ければ内部ヒープ。
        _backlog = (char*)heap_caps_malloc(kBacklogCapacity, MALLOC_CAP_SPIRAM);
        if (!_backlog) {
            _backlog = (char*)malloc(kBacklogCapacity);
        }
        _backlogLen = 0;
    }
}

void RemoteLog::setSink(LogSink* sink, const char* suffix) {
    _sink = sink;
    _suffix = suffix;
}

bool RemoteLog::lock() {
    if (!_mutex) {
        return true;  // begin() 前 (起動直後、単一タスク) はロック不要
    }
    // 他タスクが書いている最中なら待たない (ログのために処理を止めない)。
    // 呼び出し側は false のとき Serial にだけ出す。
    return xSemaphoreTake(_mutex, pdMS_TO_TICKS(2)) == pdTRUE;
}

void RemoteLog::unlock() {
    if (_mutex) {
        xSemaphoreGive(_mutex);
    }
}

void RemoteLog::putc(uint8_t c) {
    if (c == '\r') {
        return;  // CR は行組み立てに含めない
    }
    if (c == '\n') {
        _line[_lineLen] = '\0';
        if (_lineLen > 0 && _backlog) {
            // 常に退避し、loop() で 1 周あたり最大 kFlushPerLoop 行だけ publish する。
            // 派生元の知見: 1 publish は WiFi 混雑時 (UDP 映像受信中) に 40〜60ms ブロックし、
            // 即時 publish だと周期ログがまとまる周で loop() が 250ms 以上止まった。
            pushBacklog(_line, _lineLen);
        }
        _lineLen = 0;
        return;
    }
    if (_lineLen < sizeof(_line) - 1) {
        _line[_lineLen++] = (char)c;
    }
    // バッファ溢れ時は超過分を捨てる (行頭は保持)
}

size_t RemoteLog::write(uint8_t c) {
    // まず必ず Serial にミラー (USB 接続時はそのまま見える)
    Serial.write(c);
    if (!lock()) {
        return 1;
    }
    putc(c);
    unlock();
    return 1;
}

size_t RemoteLog::write(const uint8_t* buffer, size_t size) {
    Serial.write(buffer, size);
    if (!lock()) {
        return size;
    }
    for (size_t i = 0; i < size; ++i) {
        putc(buffer[i]);
    }
    unlock();
    return size;
}

bool RemoteLog::publishLine(const char* line) {
    if (!_sink || !_suffix) {
        return false;
    }
    if (_busy) {
        // publish 経路が内部でログ出力した場合の再入。
        // Serial には既に出ているので sink は諦める (true=退避不要)。
        return true;
    }
    _busy = true;
    bool ok = false;
    if (_sink->logConnected()) {
        ok = _sink->publishLog(_suffix, line);
    }
    _busy = false;
    return ok;
}

void RemoteLog::pushBacklog(const char* line, size_t len) {
    // '\n' 区切りで追記。容量を超えたら以降は破棄し、
    // 起動シーケンス (最初のログ) を優先的に残す。
    if (_backlogLen + len + 1 > kBacklogCapacity) {
        _backlogDropped = true;
        return;
    }
    memcpy(_backlog + _backlogLen, line, len);
    _backlogLen += len;
    _backlog[_backlogLen++] = '\n';
}

void RemoteLog::loop() {
    if (_backlogLen == 0 || !_backlog || !_sink || !_suffix || !_sink->logConnected()) {
        return;
    }
    if (!lock()) {
        return;
    }
    // 退避済みログを先頭から最大 kFlushPerLoop 行だけ送出し、残りは次の周へ。
    // 1 行 ~50ms × 3 = loop 1 周あたり最大 ~150ms に抑える (平常時は 1 周 0〜1 行)。
    uint8_t sent = 0;
    while (_backlogLen > 0 && sent < kFlushPerLoop) {
        size_t i = 0;
        while (i < _backlogLen && _backlog[i] != '\n') ++i;
        if (i >= _backlogLen) break;            // 改行のない不完全な残り (通常は無い)
        _backlog[i] = '\0';
        if (i > 0) {
            if (!publishLine(_backlog)) {      // 送れなかった (切断) → 残して次回
                _backlog[i] = '\n';
                unlock();
                return;
            }
            ++sent;
        }
        const size_t rest = _backlogLen - (i + 1);
        memmove(_backlog, _backlog + i + 1, rest);
        _backlogLen = rest;
    }
    const bool announceDrop = (_backlogLen == 0 && _backlogDropped);
    _backlogDropped = false;
    unlock();
    if (announceDrop) {
        publishLine("[RemoteLog] (一部のログは容量超過で破棄されました)");
    }
}

} // namespace sastle
