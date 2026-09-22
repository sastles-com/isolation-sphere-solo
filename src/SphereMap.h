/**
 * @file SphereMap.h
 * @brief LED 座標の球面→画像 UV 変換とクォータニオン回転 (純粋、Arduino 非依存)
 *
 * LEDManager::sphereToUV / rotateByQuaternion から抽出 (2026-09)。LEDManager 側は
 * これを呼ぶだけの転送になる。WebUI デジタルツイン
 * (server/frontend/src/components/sphere/HoloSphere.jsx) と同一式・同一量子化
 * trunc(u*(w-1)) にすること。
 */
#pragma once
#include "FastMath.h"

namespace sastle {
namespace sphere {

/**
 * @brief 球面座標変換 (x,y,z) → (u,v)。極軸=Z の標準正距円筒 (equirectangular)。
 *   u → px (画像の幅): 経度 -180..+180° (XY平面, 継ぎ目で wrap)
 *   v → py (画像の高さ): 極角 0°(北極=+Z, 上端) .. 180°(南極=-Z, 下端) (clamp)
 */
static inline void toUV(float x, float y, float z, float& u, float& v) {
    float len = _sqrt(x * x + y * y + z * z);
    if (len < 0.0001f) {
        u = 0.5f;
        v = 0.5f;
        return;
    }
    float nx = x / len;
    float ny = y / len;
    float nz = z / len;

    // 経度 (XY平面, -180..180°) → u → px (幅全域)。_atan2 は -1.0〜1.0 を返す
    u = (_atan2(nx, ny) + 1.0f) / 2.0f;

    // 極角 (+Zから 0..180°) → v → py (高さ全域)。第1引数 ≥ 0 なので _atan2 ∈ [0,1]
    float horizontal_dist = _sqrt(nx * nx + ny * ny);
    v = _atan2(horizontal_dist, nz);

    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
}

/// ベクトルをクォータニオンで回転: v' = v + 2·cross(q.xyz, cross(q.xyz, v) + q.w·v)
static inline void rotateByQuaternion(float& x, float& y, float& z,
                                      float qw, float qx, float qy, float qz) {
    float cross1_x = qy * z - qz * y;
    float cross1_y = qz * x - qx * z;
    float cross1_z = qx * y - qy * x;
    cross1_x += qw * x;
    cross1_y += qw * y;
    cross1_z += qw * z;
    float cross2_x = qy * cross1_z - qz * cross1_y;
    float cross2_y = qz * cross1_x - qx * cross1_z;
    float cross2_z = qx * cross1_y - qy * cross1_x;
    x += 2.0f * cross2_x;
    y += 2.0f * cross2_y;
    z += 2.0f * cross2_z;
}

} // namespace sphere
} // namespace sastle
