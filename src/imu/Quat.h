/**
 * @file Quat.h
 * @brief 姿勢用の最小クォータニオン型 (float, Arduino 非依存)
 *
 * IMU 周りの純粋ロジック (平滑・妥当性判定・デコード) が Adafruit の
 * imu::Quaternion (double, imumaths.h) に依存しないための境界型。
 * IMUManager が公開 API との境界で相互変換する。ホストの native テストで
 * そのままコンパイルできるように、標準 C ヘッダ以外を include しない。
 */
#pragma once
#include <math.h>

namespace sastle {

struct Quat {
    float w, x, y, z;
    // 明示コンストラクタ: デバイス側は -std=c++11 で、既定メンバ初期化子を持つ
    // 構造体は集成体にならず Quat{...} が使えないため (native は c++14 で通る)。
    Quat() : w(1.0f), x(0.0f), y(0.0f), z(0.0f) {}
    Quat(float w_, float x_, float y_, float z_) : w(w_), x(x_), y(y_), z(z_) {}
};

/// 内積 (float 演算)
static inline float quatDot(const Quat& a, const Quat& b) {
    return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
}

/// 成分を double に上げて内積を取り float に戻す。
/// IMUManager の旧実装は imu::Quaternion (double) 同士で積和してから float に
/// キャストしていたので、ノルム/連続性の判定値をビット単位で揃えるために残す。
static inline float quatDotD(const Quat& a, const Quat& b) {
    return (float)((double)a.w * b.w + (double)a.x * b.x + (double)a.y * b.y + (double)a.z * b.z);
}

static inline float quatNorm2D(const Quat& q) { return quatDotD(q, q); }

static inline Quat quatScaled(const Quat& q, float s) {
    return Quat{q.w * s, q.x * s, q.y * s, q.z * s};
}

static inline Quat quatNegated(const Quat& q) {
    return Quat{-q.w, -q.x, -q.y, -q.z};
}

} // namespace sastle
