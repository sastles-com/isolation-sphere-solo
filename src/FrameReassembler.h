/**
 * @file FrameReassembler.h
 * @brief UDP映像チャンクを frame_id 単位で1枚のJPEGに再構成する
 *
 * 320x160 JPEG(数KB〜)はMTUを超えIP断片化し、ESP32では断片化UDPを受信できないため、
 * 送信側がMTU内チャンクに分割して送る(プロトコルの正は docs/protocol_spec.md §4)。
 * 本クラスは受信した各チャンクを chunk_index のオフセットで再構成バッファに並べ、
 * chunk_count 個揃ったら完成とする。frame_id が変わると未完フレームは破棄(ロス耐性)。
 * 単一タスク(デコードタスク)からのみ使う前提でロックは持たない。
 */

#ifndef __FRAME_REASSEMBLER_H__
#define __FRAME_REASSEMBLER_H__

#include <Arduino.h>
#include <string.h>

namespace sastle {

/// マジックナンバー "JPEG"
constexpr uint32_t UDP_IMAGE_MAGIC = 0x4A504547;
/// 1チャンクのJPEGデータ最大長 (MTU内: 1500 - IP/UDP - 16Bヘッダ に余裕)
constexpr size_t MAX_CHUNK_DATA = 1400;
/// 1フレームの最大チャンク数 (再構成バッファ 65507 / 1400 ≒ 46。ビットマスク64ch以内)
constexpr uint16_t MAX_CHUNKS = 46;

/// UDP映像チャンクヘッダー (16B, little-endian)。送信側と必ず一致させること。
struct UDPChunkHeader {
    uint32_t magic;        ///< 0x4A504547 ("JPEG")
    uint32_t frame_id;     ///< フレーム連番
    uint16_t chunk_index;  ///< チャンク番号 (0..chunk_count-1)
    uint16_t chunk_count;  ///< このフレームの総チャンク数
    uint16_t chunk_size;   ///< このチャンクのJPEGバイト数
    uint16_t reserved;     ///< 予約 (アライン)
} __attribute__((packed));

class FrameReassembler {
public:
    /// 再構成先バッファを登録 (所有は呼び出し側)
    void begin(uint8_t* buf, size_t bufSize) {
        _buf = buf;
        _bufSize = bufSize;
        reset();
    }

    /**
     * @brief 1データグラム(チャンク)を投入する
     * @param dgram データグラム先頭 (ヘッダ+JPEGチャンク)
     * @param len   データグラム長
     * @param outSize 完成時に再構成済みJPEGの総バイト数を返す
     * @return フレーム完成で true (_buf[0..outSize] が完全なJPEG)
     */
    bool addChunk(const uint8_t* dgram, size_t len, size_t& outSize) {
        if (!_buf || len < sizeof(UDPChunkHeader)) {
            return false;
        }
        const UDPChunkHeader* h = (const UDPChunkHeader*)dgram;
        if (h->magic != UDP_IMAGE_MAGIC) {
            return false;
        }
        if (h->chunk_count == 0 || h->chunk_count > MAX_CHUNKS ||
            h->chunk_index >= h->chunk_count) {
            return false;
        }
        size_t dataLen = h->chunk_size;
        if (dataLen > MAX_CHUNK_DATA || sizeof(UDPChunkHeader) + dataLen > len) {
            return false;
        }

        // 新フレーム開始。
        // 「再構成中かどうか」は _inProgress で明示的に持つ。以前は _frameId に
        // 0xFFFFFFFF を「未受信」センチネルとして詰めていたが、frame_id は
        // プロトコル上ただの連番なので送信側が 0xFFFFFFFF を使うと衝突し、
        //   1. 完了直後は _frameId==0xFFFFFFFF なので新フレーム判定が false
        //   2. よって _gotMask/_received が前フレームのまま残る
        //   3. chunk_index=0 のビットが既に立っているので memcpy がスキップされ、
        //      直後の (_received==_chunkCount) が前フレームの値で真になる
        // → そのフレームは捨てられ、代わりに前フレームが再度デコードされていた。
        // 実害: 停止時の黒フレーム(旧実装は frame_id=0xFFFFFFFF で送っていた)が
        // 効かず、最後の映像が残り続けていた。
        if (!_inProgress || h->frame_id != _frameId) {
            if (_inProgress && _received < _chunkCount) {
                _dropped++;  // 前フレームは未完のまま破棄
            }
            _frameId = h->frame_id;
            _chunkCount = h->chunk_count;
            _received = 0;
            _totalSize = 0;
            _gotMask = 0;
            _inProgress = true;
        }

        size_t off = (size_t)h->chunk_index * MAX_CHUNK_DATA;
        if (off + dataLen > _bufSize) {
            return false;
        }
        uint64_t bit = 1ULL << h->chunk_index;
        if (!(_gotMask & bit)) {
            memcpy(_buf + off, dgram + sizeof(UDPChunkHeader), dataLen);
            _gotMask |= bit;
            _received++;
            if (h->chunk_index == h->chunk_count - 1) {
                _totalSize = off + dataLen;  // 最終チャンクで全体サイズ確定
            }
        }

        if (_received == _chunkCount && _totalSize > 0) {
            outSize = _totalSize;
            _inProgress = false;  // 完了 → 次フレーム待ち
            return true;
        }
        return false;
    }

    uint32_t framesDropped() const { return _dropped; }

private:
    void reset() {
        _inProgress = false;
        _frameId = 0;
        _chunkCount = 0;
        _received = 0;
        _totalSize = 0;
        _gotMask = 0;
    }

    uint8_t* _buf = nullptr;
    size_t _bufSize = 0;
    bool _inProgress = false;   ///< 再構成中か (frame_id にセンチネルを混ぜない)
    uint32_t _frameId = 0;
    uint16_t _chunkCount = 0;
    uint16_t _received = 0;
    uint32_t _totalSize = 0;
    uint64_t _gotMask = 0;
    uint32_t _dropped = 0;
};

} // namespace sastle

#endif /* __FRAME_REASSEMBLER_H__ */
