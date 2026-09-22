/**
 * @file Bno055Codec.h
 * @brief BNO055 レジスタ生バイトの解釈 (純粋、Arduino 非依存)
 *
 * IMUManager の読みループから抽出 (2026-09)。I2C は触らない。
 */
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <stddef.h>
#include "Quat.h"

namespace sastle {
namespace bno055 {

constexpr float kQuatScale = 1.0f / 16384.0f;   ///< 2^14 LSB / unit
constexpr int   kPartialMaxDeltaLsb = 800;        ///< ≈0.05: 400°/s でも 10ms に動ける量 (≈570 LSB) の余裕込み
constexpr int   kLsbPerMs = 57;                   ///< 400°/s 相当の成分変化 [LSB/ms] (許容幅を経過時間で伸ばす係数)
constexpr int   kPartialMaxDeltaCapLsb = 2000;    ///< 許容幅の上限。4000 だと |v|≈0.15 の成分の MSB 化け (Δ≈2300) が通った (実機 2026-09-23 ダンプ B)
constexpr float kReaderNormTol = 0.015f;          ///< 読み直し判定のノルム² 許容。実機ダンプ B より:
                                                  ///  健全値の最大ずれ |n²-1|=0.0072、化けの最小ずれ 0.019
constexpr int   kStraddleMaxDeltaLsb = 600;       ///< w 再読みの許容差。融合更新 1 回分 (≤570 LSB) は通し、化け/符号反転 (数千) は弾く

/// リトルエンディアン 2 バイト → int16
static inline int16_t decodeLE(uint8_t lo, uint8_t hi) {
    return (int16_t)((hi << 8) | lo);
}

/// 単位クォータニオン成分 → LSB (前回受理値との比較用)
static inline int unitToLsb(float c) {
    return (int)lrintf(c * 16384.0f);
}

/**
 * @brief 2 バイト読みの「MSB 欠落」判定
 *
 * 2026-09-09 の imu_dump で実証: BNO055 が 2 バイト目 (MSB) を返さず 0xFF
 * (ゼロ埋め個体では 0x00) になり、LSB は真値のまま残る (真値 0xF2B0 → 読み 0xFFB2)。
 * 本物の小さい値も MSB は 0xFF/0x00 なので、「前回受理値から 10ms で動ける範囲を
 * 超えて小さくなった」ことで区別する。
 */
static inline bool wordImplausible(uint8_t lo, uint8_t hi, int prevLsb, int tolLsb) {
    if (hi != 0xFF && hi != 0x00) return false;
    return abs((int)decodeLE(lo, hi) - prevLsb) > tolLsb;
}
static inline bool wordImplausible(uint8_t lo, uint8_t hi, int prevLsb) {
    return wordImplausible(lo, hi, prevLsb, kPartialMaxDeltaLsb);
}

/**
 * @brief 前回受理からの経過時間に応じた MSB 欠落判定の許容幅 [LSB]
 *
 * 読み失敗が続くと prevAccepted が古くなり、回転中は本物の小さい値 (MSB が正当に 0xFF/0x00)
 * まで 800 LSB を超えて「化け」扱いになっていた (solo 実機 2026-09-23: 回転中の読み失敗
 * 81 件のうち 80 件が |q|²=1.000 の正常値)。10ms を超えた分だけ 400°/s 相当で伸ばす。
 */
static inline int partialToleranceLsb(uint32_t sinceAcceptMs) {
    int extra = sinceAcceptMs > 10 ? (int)(sinceAcceptMs - 10) * kLsbPerMs : 0;
    int tol = kPartialMaxDeltaLsb + extra;
    return tol > kPartialMaxDeltaCapLsb ? kPartialMaxDeltaCapLsb : tol;
}

/**
 * @brief w 再読みの不一致判定 (融合更新境界をまたいだか)
 *
 * 以前は bit 一致を要求していたが、回転中は 4 ワード読みの間 (100kHz で数 ms) に w が正当に
 * 変わるため、回転中はほぼ毎サンプル再読みになり、3 試行とも不一致で捨てていた。
 * 融合更新 1 回で動ける量 (≤570 LSB) までは同一サンプル扱いにし、化けや q↔−q の符号反転
 * (数千 LSB) だけを弾く。
 */
static inline bool wordsDisagree(uint8_t lo1, uint8_t hi1, uint8_t lo2, uint8_t hi2) {
    return abs((int)decodeLE(lo1, hi1) - (int)decodeLE(lo2, hi2)) > kStraddleMaxDeltaLsb;
}

/// QUA_DATA 8 バイト → ノルム² (単位クォータニオンなら ≈1.0)
static inline float quatNorm2Raw(const uint8_t b[8]) {
    const float w = decodeLE(b[0], b[1]) * kQuatScale, x = decodeLE(b[2], b[3]) * kQuatScale,
                y = decodeLE(b[4], b[5]) * kQuatScale, z = decodeLE(b[6], b[7]) * kQuatScale;
    return w * w + x * x + y * y + z * z;
}

/**
 * @brief 組み立てた 8 バイトが化けを含むか (ノルム² が 1±kReaderNormTol を外れる)
 *
 * MSB 欠落の差分判定は「前回受理値からの距離」しか見ないため、読み出しが遅れて許容幅が
 * 伸びた周期では |v|≈0.15 の成分の化け (0xF6xx→0xFFxx, Δ≈2300 LSB) が通ってしまった
 * (ダンプ B: 受理間 15° 超 15 件の直前サンプルが全て n²=0.98 前後の化け値)。
 * 健全な BNO055 出力の n² は 0.999〜1.000 に張り付くので、ノルムで独立に弾く。
 */
static inline bool quatNormImplausible(const uint8_t b[8]) {
    const float n2 = quatNorm2Raw(b);
    return n2 < 1.0f - kReaderNormTol || n2 > 1.0f + kReaderNormTol;
}

/// QUA_DATA 8 バイト (w,x,y,z 各 LE) → Quat
static inline Quat quatFromRaw(const uint8_t b[8]) {
    return Quat{decodeLE(b[0], b[1]) * kQuatScale,
                decodeLE(b[2], b[3]) * kQuatScale,
                decodeLE(b[4], b[5]) * kQuatScale,
                decodeLE(b[6], b[7]) * kQuatScale};
}

/// 8 バイト全ゼロ (I2C が成功を返しつつ何も入っていない) か
static inline bool allZero8(const uint8_t b[8]) {
    for (int i = 0; i < 8; i++) if (b[i] != 0) return false;
    return true;
}

/// 末尾ワード (z) が v,v で埋まっているか (8B 一括読みの部分読み検出用)
static inline bool tailWordIs(const uint8_t b[8], uint8_t v) {
    return b[6] == v && b[7] == v;
}

/// バイト列を大文字 hex に。out には 2*n+1 バイト必要。戻り値は書いた文字数 (終端除く)
static inline size_t hexEncode(const uint8_t* in, size_t n, char* out) {
    static const char* hx = "0123456789ABCDEF";
    size_t o = 0;
    for (size_t i = 0; i < n; i++) { out[o++] = hx[in[i] >> 4]; out[o++] = hx[in[i] & 15]; }
    out[o] = '\0';
    return o;
}

} // namespace bno055
} // namespace sastle
