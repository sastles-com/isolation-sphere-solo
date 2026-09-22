/**
 * @file ImuDiag.h
 * @brief IMU タスク → loop タスク の診断受け渡し (純粋、Arduino 非依存)
 *
 * IMUManager から抽出 (2026-09)。役割は 3 つ:
 *   - ImuCounters : 累計カウンタ (IMU タスクが ++、loop が読む。32bit volatile = Xtensa で atomic)
 *   - *Slot       : 「直近 1 件」のスナップショット。IMU タスクが offer() で置き、loop が take() で
 *                   取り出してログに出す。sastle::Log はスレッド安全でないため IMU タスクからは
 *                   ログを出せない、という制約 (2026-09-06 の core0 停止) から生まれた仕組み。
 *                   フラグは volatile、ペイロードは診断用途なので torn read を許容する。
 *   - RawDumpRing : imu_dump の 12B/サンプル記録。バッファの malloc/free は所有者 (IMUManager、
 *                   loop 側) が行い、リングはポインタを借りるだけ。IMU タスクは armed 中だけ書く。
 *                   レコード形式 (raw8 + flags + straddles + dt_ms LE) と 8 サンプル/行の hex は
 *                   core/tools/imu_dump_analyze.py が読むので変えない。
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "Bno055Codec.h"

namespace sastle {

struct ImuCounters {
    volatile uint32_t readTotal = 0;    ///< quat 読み出し試行 (= _updateOnce の回数)
    volatile uint32_t readFails = 0;    ///< 3 試行すべて失敗
    volatile uint32_t discards = 0;     ///< 読めたが妥当性判定で棄却
    volatile uint32_t zeroReads = 0;    ///< I2C 成功なのに 8B 全ゼロ
    volatile uint32_t partialReads = 0; ///< 部分読み (MSB 欠落 / 末尾埋め) を検出して読み直した回数
    volatile uint32_t straddles = 0;    ///< 読みの途中で融合更新をまたいだ (w 再読み不一致)
};

/// 生バイト N バイトの単発スナップショット
template <size_t N>
struct RawSlot {
    volatile bool fresh = false;
    uint8_t raw[N] = {0};
    void offer(const uint8_t* src) {
        if (fresh) return;              // 未回収なら最初の 1 件を保持
        memcpy(raw, src, N);
        fresh = true;
    }
    bool take(uint8_t* out) {
        if (!fresh) return false;
        memcpy(out, raw, N);
        fresh = false;
        return true;
    }
};

/// 棄却 quat: 生 8B + ノルム²
struct DiscardSlot {
    volatile bool fresh = false;
    uint8_t raw[8] = {0};
    float n2 = 0.0f;
    void offer(const uint8_t* src, float norm2) {
        if (fresh) return;
        memcpy(raw, src, 8); n2 = norm2; fresh = true;
    }
    bool take(uint8_t* out, float& norm2) {
        if (!fresh) return false;
        memcpy(out, raw, 8); norm2 = n2; fresh = false;
        return true;
    }
};

/// 補助データ (accel/gyro/euler) の部分読み: レジスタ + 生 6B
struct VecPartialSlot {
    volatile bool fresh = false;
    uint8_t reg = 0;
    uint8_t raw[6] = {0};
    void offer(uint8_t r, const uint8_t* src) {
        if (fresh) return;
        reg = r; memcpy(raw, src, 6); fresh = true;
    }
    bool take(uint8_t& r, uint8_t* out) {
        if (!fresh) return false;
        r = reg; memcpy(out, raw, 6); fresh = false;
        return true;
    }
};

/// 「起きた」だけを伝えるフラグ (I2C クロック変更など)
struct EventSlot {
    volatile bool fresh = false;
    void raise() { fresh = true; }
    bool take() { if (!fresh) return false; fresh = false; return true; }
};

/// センサー再初期化の結果 (成否、自動か手動か)
struct ResetSlot {
    volatile bool fresh = false;
    bool ok = false;
    bool isAuto = false;
    void offer(bool ok_, bool auto_) { ok = ok_; isAuto = auto_; fresh = true; }  // 最新を優先
    bool take(bool& ok_, bool& auto_) {
        if (!fresh) return false;
        ok_ = ok; auto_ = isAuto; fresh = false;
        return true;
    }
};

/// 診断スナップショット一式
struct ImuDiagSlots {
    RawSlot<8>     readFail;
    DiscardSlot    discard;
    VecPartialSlot vecPartial;
    EventSlot      clockChanged;
    ResetSlot      reset;
};

/**
 * @brief imu_dump の生サンプルリング (バッファは借り物)
 *
 * 1 レコード 12B: raw[8], flags (bit0=readOk, bit1=accepted, bit2=forced), straddles,
 * dt_ms (uint16 LE、前レコードからの経過。先頭は 0)。
 */
class RawDumpRing {
public:
    static constexpr uint8_t kRec = 12;
    static constexpr uint8_t kFlagReadOk = 1, kFlagAccepted = 2, kFlagForced = 4;
    static constexpr uint16_t kSamplesPerLine = 8;   ///< RemoteLog 240 文字に収まる (192 hex + prefix)

    /// 記録開始。armed 中は拒否する (書き手が居る間にバッファを差し替えない)
    bool arm(uint8_t* buf, uint16_t n) {
        if (_armed || !buf || n == 0) return false;
        _buf = buf; _n = n; _count = 0; _readPos = 0; _prevMs = 0;
        _ready = false;
        _armed = true;                   // 最後に立てる (IMU タスクが見る)
        return true;
    }

    bool armed() const { return _armed; }
    bool ready() const { return _ready; }
    const uint8_t* buffer() const { return _buf; }
    uint16_t count() const { return _count; }

    /// IMU タスク: 1 サンプル記録 (armed でなければ何もしない)
    void record(const uint8_t raw[8], uint8_t flags, uint8_t straddles, uint32_t nowMs) {
        if (!_armed || !_buf || _count >= _n) return;
        uint8_t* r = _buf + (size_t)_count * kRec;
        memcpy(r, raw, 8);
        r[8] = flags;
        r[9] = straddles;
        const uint32_t dms = _prevMs ? (nowMs - _prevMs) : 0;
        r[10] = (uint8_t)(dms & 0xFF); r[11] = (uint8_t)((dms >> 8) & 0xFF);
        _prevMs = nowMs;
        _count = _count + 1;
        if (_count >= _n) { _armed = false; _ready = true; }
    }

    /// loop タスク: 出し切ったか (true なら所有者がバッファを解放して disarm() する)
    bool exhausted() const { return _ready && _readPos >= _count; }

    /// loop タスク: 次の最大 8 サンプルを hex 化。無ければ false
    bool takeLine(char* out, size_t cap, uint16_t& idx0) {
        if (!_ready || !_buf || _readPos >= _count) return false;
        uint16_t n = _count - _readPos; if (n > kSamplesPerLine) n = kSamplesPerLine;
        if (cap < (size_t)n * kRec * 2 + 1) return false;
        idx0 = _readPos;
        bno055::hexEncode(_buf + (size_t)_readPos * kRec, (size_t)n * kRec, out);
        _readPos = _readPos + n;
        return true;
    }

    /// 完了・解放後に呼ぶ (ポインタを手放す)
    void disarm() { _ready = false; _armed = false; _buf = nullptr; }

private:
    uint8_t* _buf = nullptr;
    volatile uint16_t _n = 0, _count = 0, _readPos = 0;
    volatile bool _armed = false, _ready = false;
    uint32_t _prevMs = 0;
};

} // namespace sastle
