/**
 * @file FrameBufferPool.h
 * @brief RGB565 トリプルバッファ (display/ready/decode)
 *
 * デコード(別タスク)と描画(別タスク)を、テアリングなく独立・並列に動かすための
 * 3枚バッファ。差し替えはインデックス操作のみ(画素コピーなし)で portMUX により
 * 一瞬で行うため、大きな臨界区間を作らない。
 *   - decode 側: decodeBuffer() に書き込み、publish() で ready に公開。
 *   - render 側: adopt() で ready を display に採用、displayBuffer() を読む。
 */

#ifndef __FRAME_BUFFER_POOL_H__
#define __FRAME_BUFFER_POOL_H__

#include <Arduino.h>
#include <string.h>

namespace sastle {

class FrameBufferPool {
public:
    /// 3枚を PSRAM に確保 (各 bytes バイト)
    bool allocate(size_t bytes) {
        for (int i = 0; i < 3; i++) {
            _buf[i] = (uint16_t*)ps_malloc(bytes);
            if (!_buf[i]) { freeAll(); return false; }
            memset(_buf[i], 0, bytes);
        }
        _displayIdx = 0;
        _decodeIdx = 1;
        _readyIdx = -1;
        return true;
    }

    void freeAll() {
        for (int i = 0; i < 3; i++) {
            if (_buf[i]) { free(_buf[i]); _buf[i] = nullptr; }
        }
    }

    bool isAllocated() const { return _buf[0] && _buf[1] && _buf[2]; }
    uint16_t* decodeBuffer() const { return _buf[_decodeIdx]; }
    uint16_t* displayBuffer() const { return _buf[_displayIdx]; }

    /// decode側: 書き終えた decode を ready に公開し、次の書込先を確保する。
    /// @return 表示前の ready を上書きした(=フレーム落とした)場合 true
    bool publish() {
        bool dropped;
        portENTER_CRITICAL(&_mux);
        int oldReady = _readyIdx;
        _readyIdx = _decodeIdx;
        if (oldReady >= 0) {
            _decodeIdx = oldReady;  // 表示前に上書きされた旧readyを再利用
            dropped = true;
        } else {
            for (int i = 0; i < 3; i++) {
                if (i != _displayIdx && i != _readyIdx) { _decodeIdx = i; break; }
            }
            dropped = false;
        }
        portEXIT_CRITICAL(&_mux);
        return dropped;
    }

    /// render側: 表示待ちの完成フレームがあれば display に採用する。
    void adopt() {
        portENTER_CRITICAL(&_mux);
        if (_readyIdx >= 0) {
            _displayIdx = _readyIdx;
            _readyIdx = -1;
        }
        portEXIT_CRITICAL(&_mux);
    }

private:
    uint16_t* _buf[3] = {nullptr, nullptr, nullptr};
    volatile int _displayIdx = 0;   ///< 表示中
    volatile int _readyIdx = -1;    ///< 表示待ち (-1=なし)
    volatile int _decodeIdx = 1;    ///< 書込中
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
};

} // namespace sastle

#endif /* __FRAME_BUFFER_POOL_H__ */
