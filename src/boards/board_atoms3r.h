#ifndef BOARD_ATOMS3R_H
#define BOARD_ATOMS3R_H

// ============================================================
//  M5AtomS3R (ESP32-S3) ボード定義
//  デバイス個別の物理ハード設定 (ピン・有無) をここに集約する。
//  変更時は配線図 (FPC-isolation-sphere/kiban/core-M5atom-FPC) と
//  core/doc/ の仕様書との整合を確認すること。
// ============================================================

#include <stdint.h>

// --- ボード機能フラグ (プリプロセッサ: M5固有コードの有効/無効に使用) ---
#define BOARD_NAME       "M5AtomS3R"
#define BOARD_HAS_LCD    1   // 内蔵 128x128 LCD (M5Unified 経由)
#define BOARD_HAS_BUZZER 1   // 圧電スピーカー LS1 (G39)

// --- ストリップ本数 (V2: 5ストリップ × 160 = 800 LED) ---
// FastLED の addLeds<> はピンがコンパイル時定数のため、本数もコンパイル時に決まる。
// build_flags で -D BOARD_NUM_STRIPS=4 等を指定すれば上書き可能。
#ifndef BOARD_NUM_STRIPS
#define BOARD_NUM_STRIPS 5
#endif

namespace sastle {

// --- LED ---
// FastLED の addLeds<> はピンがテンプレート引数のため、実行時値にできない。
// (config.json には出せず、コンパイル時定数として持つ必要がある)
// core-M5atom-FPC の ESP32-01 コネクタ (J17) が GPIO008,007,006,005,038 の
// 降順で配線されており、mother-ring 側 (J9: GPIO01..05 昇順, LINE01..05へ直結) と
// 逆順に嵌合する。実機の LINE01=シルク印刷 で色テストした結果 (2026-08-15):
// LINE01=G8, LINE02=G7, LINE03=G6, LINE04=G5, LINE05=G38 と確定。
// Strip N = LINE0(N+1) となるよう、配線の実態に合わせてピン割り当てを反転済み。
// 物理配線を作り直す場合はこの反転も併せて見直すこと。
constexpr uint8_t  kLedPin0 = 8;    // Strip 0 -> LINE01 (実GPIO 8)
constexpr uint8_t  kLedPin1 = 7;    // Strip 1 -> LINE02 (実GPIO 7)
constexpr uint8_t  kLedPin2 = 6;    // Strip 2 -> LINE03 (実GPIO 6)
constexpr uint8_t  kLedPin3 = 5;    // Strip 3 -> LINE04 (実GPIO 5)
constexpr uint8_t  kLedPin4 = 38;   // Strip 4 -> LINE05 (実GPIO 38, 反転の影響なし)
constexpr uint8_t  kNumStrips = BOARD_NUM_STRIPS;
constexpr uint16_t kMaxLeds = 800;
constexpr uint8_t  kTargetFps = 30;
constexpr uint32_t kFrameDelayMs = 1000 / kTargetFps;
constexpr uint8_t  kLedDefaultBrightness = 128;  // デフォルト輝度 50%
// 電源保護: FastLED のパワーリミット (5V 系の合計電流上限)。
// 全LED白点灯のような高負荷フレームでも FastLED が輝度を自動スケールして
// この上限内に収める。実測で電源容量が確定したら見直すこと。
constexpr uint32_t kLedMaxPowerMa = 2000;

// --- IMU (BNO055) I2C ---
constexpr uint8_t kImuI2cSda = 2;  // Grove SDA
constexpr uint8_t kImuI2cScl = 1;  // Grove SCL

// --- ブザー ---
constexpr uint8_t kBuzzerGpio = 39;  // G39 -> LS1 SPEAKER

} // namespace sastle

#endif /* BOARD_ATOMS3R_H */
