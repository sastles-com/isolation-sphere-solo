/**
 * @file AttitudeValidator.h
 * @brief 読み取った姿勢の妥当性判定: ノルム窓 + 連続性ガード + 強制受理 (純粋)
 *
 * IMUManager::_updateOnce のガード部分から抽出 (2026-09)。数値は旧実装と同一に
 * なるよう、積和は double で行ってから float に落とす (quatDotD)。
 *
 * - ノルム窓 0.85〜1.15: この BNO055 個体はノルム² 0.92〜0.98 を常用するため、
 *   「単位でない=破棄」だと正常値の半分を捨てる。範囲内なら正規化して受理。
 * - 連続性: 前回受理値との姿勢差が maxAngle = 0.26 rad (15°) + gyro[rad/s]×dt×1.5
 *   を超えたら棄却。gyro は deg/s で渡す (BNO055 の単位)。aux OFF 中は 360°/s で代用。
 *   dt は前回受理からの経過で 0.01〜0.5 s にクランプ。
 * - 強制受理: 連続棄却が kForceAfterDiscards に達したら次を受理する (再初期化直後や
 *   本当に姿勢が飛んだときの復帰用)。forceNextAccept() で外から即時化できる。
 *
 * スレッド: IMU タスク専用 (forceNextAccept も IMU タスクの pending 処理から呼ぶ)。
 */
#pragma once
#include <stdint.h>
#include <math.h>
#include "Quat.h"

namespace sastle {

struct AttitudeVerdict {
    bool  ok = false;      ///< 受理 (q は正規化済み)
    bool  forced = false;  ///< 連続棄却による強制受理だった
    float n2 = 0.0f;       ///< 生値のノルム² (診断用)
    Quat  q;               ///< 受理値 (ok のときのみ有効)
};

class AttitudeValidator {
public:
    // 0.85〜1.15 → 0.97〜1.03 (solo 2026-09-23)。派生元は「この個体はノルム² 0.92〜0.98 を常用」と
    // したが、それは化け値を含んだ観測だった。読み直し後の健全な出力は 0.999〜1.000 に張り付く。
    static constexpr float   kNormMin = 0.97f;
    static constexpr float   kNormMax = 1.03f;
    static constexpr float   kBaseAngleRad = 0.26f;      ///< 基本 15°
    static constexpr float   kGyroMargin = 1.5f;
    static constexpr float   kAuxOffGyroDegS = 360.0f;   ///< gyro が読めないときの想定最大角速度
    static constexpr float   kDtMinS = 0.01f, kDtMaxS = 0.5f;
    static constexpr uint8_t kForceAfterDiscards = 10;

    /**
     * @param raw          読み取った姿勢 (readOk=false なら無視される)
     * @param readOk       I2C 読みが成功したか
     * @param prevAccepted 前回受理値
     * @param gyroDegSum   |gx|+|gy|+|gz| [deg/s]
     * @param auxOn        gyro が更新されているか (false なら kAuxOffGyroDegS を使う)
     * @param nowMs        現在時刻 [ms]
     */
    AttitudeVerdict evaluate(const Quat& raw, bool readOk, const Quat& prevAccepted,
                             float gyroDegSum, bool auxOn, uint32_t nowMs) {
        AttitudeVerdict v;
        v.q = raw;
        v.n2 = quatNorm2D(raw);
        v.ok = readOk && (v.n2 > kNormMin && v.n2 < kNormMax);
        if (v.ok && fabsf(v.n2 - 1.0f) > 1e-4f) {
            const float inv = 1.0f / sqrtf(v.n2);
            // 旧実装: imu::Quaternion(q.w()*inv, ...) = double×float。float に丸めて保持する
            v.q = Quat{(float)((double)raw.w * inv), (float)((double)raw.x * inv),
                       (float)((double)raw.y * inv), (float)((double)raw.z * inv)};
        }
        if (v.ok) {
            const float dot = fabsf(quatDotD(v.q, prevAccepted));   // q と -q は同じ回転
            float dts = (nowMs - _lastAcceptMs) * 0.001f;
            if (dts < kDtMinS) dts = kDtMinS;
            if (dts > kDtMaxS) dts = kDtMaxS;
            const float gyroDeg = auxOn ? gyroDegSum : kAuxOffGyroDegS;
            const float gyroRad = gyroDeg * 0.0174532925f;
            const float maxAngle = kBaseAngleRad + gyroRad * dts * kGyroMargin;
            const float dotClamped = dot > 1.0f ? 1.0f : dot;
            const float ang = 2.0f * acosf(dotClamped);
            if (ang > maxAngle && _consecDiscards < kForceAfterDiscards) {
                v.ok = false;
            } else {
                if (ang > maxAngle) v.forced = true;
                _lastAcceptMs = nowMs;
            }
        }
        // 連続棄却カウンタ: 受理でリセット、読めた上での棄却で加算 (読み失敗は数えない)。
        // uint8_t のまま (旧実装と同じ型・同じ折り返し挙動)。
        if (v.ok) _consecDiscards = 0;
        else if (readOk) _consecDiscards++;
        return v;
    }

    /// 次のサンプルを連続性ガードに関係なく受理させる (再初期化直後など)
    void forceNextAccept() { _consecDiscards = kForceAfterDiscards; }

    uint8_t consecutiveDiscards() const { return _consecDiscards; }
    uint32_t lastAcceptMs() const { return _lastAcceptMs; }

private:
    uint32_t _lastAcceptMs = 0;
    uint8_t  _consecDiscards = 0;
};

} // namespace sastle
