/**
 * @file MjpegAssembler.h
 * @brief 順次読み出しのバイト列から raw MJPEG の 1 フレームを組み立てる (Arduino 非依存)
 *
 * MjpegReader (LittleFS) のバッファ管理をここに切り出し、native env で単体テストできるようにした。
 * 読み出し源 Source は以下を満たすこと:
 *   size_t read(uint8_t* dst, size_t maxBytes);  // 0 = EOF
 *   void   rewind();                              // 先頭へ戻す
 *
 * 契約: next() が Ok を返したとき、フレームは buf[0 .. outSize) に置かれ、**次に next() を呼ぶまで
 * そのまま保たれる**。先読みしてしまった後続バイトはフレームの後ろに残し、次回呼び出しの冒頭で
 * 先頭へ寄せる。確定時に寄せると返したフレームの先頭が次フレームで上書きされる (実機で発生した不具合)。
 */

#ifndef __MJPEG_ASSEMBLER_H__
#define __MJPEG_ASSEMBLER_H__

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "JpegScan.h"

namespace sastle {
namespace mjpeg {

/**
 * @brief メモリ上のバイト列を読み出し源にする (PSRAM に載せた動画の再生用)
 *
 * 再生中にフラッシュを一切読まないための Source。LittleFS 読み出しは 1 回ごとに両コアの
 * キャッシュを止めるため、描画 (別コア) とデコード (同コア) の両方を遅らせていた (実機 2026-09-23:
 * /api/status のフラッシュ走査と重なると 1 フレームのデコードが 400ms まで伸び、締切落ちが 13〜37%)。
 */
struct MemorySource {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;

    size_t read(uint8_t* dst, size_t maxBytes) {
        if (!data || pos >= size) return 0;
        size_t n = size - pos;
        if (n > maxBytes) n = maxBytes;
        memcpy(dst, data + pos, n);
        pos += n;
        return n;
    }
    void rewind() { pos = 0; }
};

/// next() の結果
enum class Status : uint8_t {
    Ok,         ///< 1フレーム取得 (buf 先頭に置かれている)
    Empty,      ///< 有効フレームが 1 つも無い (ループ時: 巻き戻し直後に再度 EOF)
    Corrupt,    ///< JPEG 構造が壊れている
    TooLarge,   ///< 1フレームがバッファ容量を超える
    Eof,        ///< (非ループ時のみ) 末尾に到達し、残りバイトは無い
    Truncated,  ///< (非ループ時のみ) 末尾に完結しないフレームが残っている
};

template <typename Source>
class Assembler {
public:
    Assembler() : _src(nullptr), _buf(nullptr), _cap(0), _fill(0), _consumed(0),
                  _readChunk(2048), _loop(true), _loops(0) {}

    /**
     * @param src        読み出し源 (呼び出し側所有)
     * @param buf        組み立てバッファ (呼び出し側所有)
     * @param cap        buf の容量 = 受理する 1 フレームの上限
     * @param readChunk  1 回の read() 上限
     * @param loop       EOF で先頭へ巻き戻す (再生用)。false なら Eof/Truncated を返す (検証用)
     */
    void begin(Source* src, uint8_t* buf, size_t cap, size_t readChunk, bool loop) {
        _src = src;
        _buf = buf;
        _cap = cap;
        _fill = 0;
        _consumed = 0;
        _readChunk = readChunk ? readChunk : 1;
        _loop = loop;
        _loops = 0;
    }

    void reset() { begin(nullptr, nullptr, 0, _readChunk, _loop); }

    /**
     * @brief 次の完全な 1 フレームを buf 先頭に置く
     * @param outSize  フレーム長 (bytes)
     * @param wrapped  EOF に達して先頭へ巻き戻した直後のフレームなら true (ループ時のみ)
     */
    Status next(size_t& outSize, bool& wrapped) {
        outSize = 0;
        wrapped = false;
        if (!_src || !_buf) {
            return Status::Empty;
        }

        // 前回返したフレームは呼び出し側が使い終えている。ここで初めて先読み分を先頭へ寄せる。
        if (_consumed > 0) {
            if (_fill > _consumed) {
                memmove(_buf, _buf + _consumed, _fill - _consumed);
                _fill -= _consumed;
            } else {
                _fill = 0;
            }
            _consumed = 0;
        }

        int wraps = 0;
        for (;;) {
            jpegscan::Info info;
            const jpegscan::Result r = jpegscan::parse(_buf, _fill, info);

            if (r == jpegscan::Result::Ok) {
                outSize = info.length;
                _consumed = info.length;  // 先読み分はフレームの後ろに残したまま返す
                _last = info;
                return Status::Ok;
            }
            if (r == jpegscan::Result::Invalid) {
                return Status::Corrupt;
            }

            // NeedMore: 追記読みして再判定
            if (_fill >= _cap) {
                return Status::TooLarge;
            }
            size_t want = _cap - _fill;
            if (want > _readChunk) {
                want = _readChunk;
            }
            const size_t got = _src->read(_buf + _fill, want);
            if (got > 0) {
                _fill += got;
                continue;
            }

            // EOF
            if (!_loop) {
                return (_fill == 0) ? Status::Eof : Status::Truncated;
            }
            if (wraps >= 1) {
                // 巻き戻した直後にまた EOF = 有効フレームを含まない
                return (_fill == 0) ? Status::Empty : Status::Corrupt;
            }
            // 末尾に完結しないフレームが残っている = 途中で切れたファイル。破棄して先頭へ戻る。
            _fill = 0;
            wraps++;
            wrapped = true;
            _loops++;
            _src->rewind();
        }
    }

    size_t fill() const { return _fill; }
    uint32_t loops() const { return _loops; }
    /// 直前に Ok を返したフレームの解析結果 (幅・高さ・baseline)
    const jpegscan::Info& lastInfo() const { return _last; }

private:
    Source* _src;
    uint8_t* _buf;
    size_t _cap;
    size_t _fill;      ///< buf 内の有効バイト数 (返したフレーム + 先読み)
    size_t _consumed;  ///< 前回返したフレーム長 (次回冒頭で寄せる)
    size_t _readChunk;
    bool _loop;
    uint32_t _loops;
    jpegscan::Info _last;
};

}  // namespace mjpeg
}  // namespace sastle

#endif  // __MJPEG_ASSEMBLER_H__
