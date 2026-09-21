/**
 * @file MadgwickAHRS.h
 * @brief 6軸 (加速度+ジャイロ) Madgwick 姿勢推定フィルタ
 * @author sastle-com
 * @date 2026-06-16
 *
 * M5Atom 内蔵 IMU (BMI270) の生データから姿勢クォータニオンを算出するための
 * 自己完結ヘッダオンリ実装。地磁気を使わない 6軸版のため、ロール/ピッチは重力で
 * 補正され安定する一方、ヨー(方位)はジャイロ積分のみで経時ドリフトする。
 *
 * 参考: S.O.H. Madgwick, "An efficient orientation filter for inertial and
 *       inertial/magnetic sensor arrays" (2010).
 */

#pragma once

#include <math.h>

namespace sastle {

class MadgwickAHRS {
public:
    /**
     * @param beta フィルタゲイン。大きいほど加速度補正が強く(応答↑/ノイズ↑)、
     *             小さいほどジャイロ追従(滑らか/バイアス影響↑)。0.1 が標準。
     */
    explicit MadgwickAHRS(float beta = 0.1f)
        : _beta(beta), _q0(1.0f), _q1(0.0f), _q2(0.0f), _q3(0.0f) {}

    void setBeta(float beta) { _beta = beta; }

    /**
     * @brief 6軸更新 (地磁気なし)
     * @param gx,gy,gz 角速度 [rad/s]
     * @param ax,ay,az 加速度 [任意単位。正規化されるので g でも m/s² でも可]
     * @param dt       前回からの経過時間 [s]
     */
    void updateIMU(float gx, float gy, float gz,
                   float ax, float ay, float az, float dt) {
        float q0 = _q0, q1 = _q1, q2 = _q2, q3 = _q3;

        // ジャイロによるクォータニオン変化率
        float qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
        float qDot2 = 0.5f * ( q0 * gx + q2 * gz - q3 * gy);
        float qDot3 = 0.5f * ( q0 * gy - q1 * gz + q3 * gx);
        float qDot4 = 0.5f * ( q0 * gz + q1 * gy - q2 * gx);

        // 加速度が有効な場合のみフィードバック補正 (自由落下/無重力時はスキップ)
        if (!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {
            float recipNorm = invSqrt(ax * ax + ay * ay + az * az);
            ax *= recipNorm;
            ay *= recipNorm;
            az *= recipNorm;

            float _2q0 = 2.0f * q0;
            float _2q1 = 2.0f * q1;
            float _2q2 = 2.0f * q2;
            float _2q3 = 2.0f * q3;
            float _4q0 = 4.0f * q0;
            float _4q1 = 4.0f * q1;
            float _4q2 = 4.0f * q2;
            float _8q1 = 8.0f * q1;
            float _8q2 = 8.0f * q2;
            float q0q0 = q0 * q0;
            float q1q1 = q1 * q1;
            float q2q2 = q2 * q2;
            float q3q3 = q3 * q3;

            // 目的関数の勾配 (gradient descent)
            float s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
            float s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay
                       - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
            float s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay
                       - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
            float s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;
            recipNorm = invSqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
            s0 *= recipNorm;
            s1 *= recipNorm;
            s2 *= recipNorm;
            s3 *= recipNorm;

            qDot1 -= _beta * s0;
            qDot2 -= _beta * s1;
            qDot3 -= _beta * s2;
            qDot4 -= _beta * s3;
        }

        // 積分してクォータニオンを更新
        q0 += qDot1 * dt;
        q1 += qDot2 * dt;
        q2 += qDot3 * dt;
        q3 += qDot4 * dt;

        // 正規化
        float recipNorm = invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
        _q0 = q0 * recipNorm;
        _q1 = q1 * recipNorm;
        _q2 = q2 * recipNorm;
        _q3 = q3 * recipNorm;
    }

    // クォータニオン取得 (w, x, y, z)
    float w() const { return _q0; }
    float x() const { return _q1; }
    float y() const { return _q2; }
    float z() const { return _q3; }

    /**
     * @brief オイラー角を BNO055 VECTOR_EULER 互換で取得
     * @param heading ヨー [度] (0..360)
     * @param roll    ロール [度] (-180..180)
     * @param pitch   ピッチ [度] (-90..90)
     */
    void getEulerDeg(float& heading, float& roll, float& pitch) const {
        const float RAD2DEG = 57.29577951308232f;
        // ZYX (yaw-pitch-roll) 順
        float yaw   = atan2f(2.0f * (_q1 * _q2 + _q0 * _q3),
                             _q0 * _q0 + _q1 * _q1 - _q2 * _q2 - _q3 * _q3);
        float pit   = asinf(constrain1(-2.0f * (_q1 * _q3 - _q0 * _q2)));
        float rol   = atan2f(2.0f * (_q0 * _q1 + _q2 * _q3),
                             _q0 * _q0 - _q1 * _q1 - _q2 * _q2 + _q3 * _q3);
        heading = yaw * RAD2DEG;
        if (heading < 0.0f) heading += 360.0f;  // BNO055 は 0..360
        roll  = rol * RAD2DEG;
        pitch = pit * RAD2DEG;
    }

private:
    static float invSqrt(float x) {
        // 安定性優先で標準 sqrt を使用 (ESP32-S3 は FPU 搭載で十分高速)
        return 1.0f / sqrtf(x);
    }
    static float constrain1(float v) {
        if (v > 1.0f) return 1.0f;
        if (v < -1.0f) return -1.0f;
        return v;
    }

    float _beta;
    float _q0, _q1, _q2, _q3;  // クォータニオン状態 (w, x, y, z)
};

} // namespace sastle
