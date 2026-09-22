/**
 * @file QuatSmoother.h
 * @brief 符号合わせ付きクォータニオン移動平均 (純粋、Arduino 非依存)
 *
 * IMUManager::_pushSmooth / setSmoothFrames から抽出 (2026-09)。数値は旧実装と同一:
 * 旧実装も imu::Quaternion(double) に float 値を入れて float で加算していた。
 *
 * 符号合わせが必須な理由: q と -q は同じ回転を表すが算術平均では打ち消し合う。
 * BNO055 は姿勢が w=0 の面をまたぐと符号を反転して返すことがあり、揃えずに足すと
 * 平均のノルムが 0 付近まで落ちて正規化後の姿勢が飛ぶ。直前の格納値と同じ半球へ
 * 揃えれば、リング内の要素は帰納的に互いに整合する。
 *
 * スレッド: push/reset は IMU タスク専用。setWindow は loop タスクから呼ばれる
 * (volatile uint8_t の単一書き込みなので排他不要)。
 */
#pragma once
#include <stdint.h>
#include <math.h>
#include "Quat.h"

namespace sastle {

class QuatSmoother {
public:
    static constexpr uint8_t kMax = 50;   ///< 窓の上限 (WebUI のスライダー上限と揃える)

    /// 窓 (平均するフレーム数) を設定。1 = 平滑なし。リングはクリアしない:
    /// push が「直近 min(count, N) 個」だけを足すので、拡大・縮小は次のサンプルから即座に効く。
    void setWindow(uint8_t n) {
        if (n < 1) n = 1;
        if (n > kMax) n = kMax;
        _n = n;
    }
    uint8_t window() const { return _n; }

    /// 旧姿勢を平均に混ぜたくないとき (センサー再初期化直後) に呼ぶ
    void reset() { _head = 0; _count = 0; }

    /// 1 サンプル積んで、直近 window 個の平均 (正規化済み) を返す。
    /// window==1 のときは符号合わせ後の入力をそのまま返す (旧挙動)。
    Quat push(const Quat& in) {
        uint8_t n = _n;
        if (n < 1) n = 1;
        if (n > kMax) n = kMax;

        float w = in.w, x = in.x, y = in.y, z = in.z;
        if (_count > 0) {
            const Quat& r = _buf[(_head + kMax - 1) % kMax];
            const float d = w * r.w + x * r.x + y * r.y + z * r.z;
            if (d < 0.0f) { w = -w; x = -x; y = -y; z = -z; }
        }

        _buf[_head] = Quat{w, x, y, z};
        _head = (uint8_t)((_head + 1) % kMax);
        if (_count < kMax) _count++;

        if (n == 1) return Quat{w, x, y, z};

        // 走る合計ではなく毎回足し直す (最大 50 要素、丸め誤差の蓄積を避ける)
        const uint8_t m = _count < n ? _count : n;
        float sw = 0.0f, sx = 0.0f, sy = 0.0f, sz = 0.0f;
        for (uint8_t k = 0; k < m; k++) {
            const Quat& e = _buf[(_head + kMax - 1 - k) % kMax];
            sw += e.w; sx += e.x; sy += e.y; sz += e.z;
        }
        const float n2 = sw * sw + sx * sx + sy * sy + sz * sz;
        if (n2 > 1e-6f) {
            const float inv = 1.0f / sqrtf(n2);
            return Quat{sw * inv, sx * inv, sy * inv, sz * inv};
        }
        return Quat{w, x, y, z};   // 符号を揃えているので原理的に起きない
    }

    uint8_t count() const { return _count; }

private:
    Quat _buf[kMax];
    uint8_t _head = 0;
    uint8_t _count = 0;
    volatile uint8_t _n = 1;
};

} // namespace sastle
