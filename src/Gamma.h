/**
 * @file Gamma.h
 * @brief brightness (%) → LED 輝度 (0-255) の知覚補正 (純粋、Arduino 非依存)
 *
 * CommandHandler から抽出 (2026-09)。FastLED の輝度は PWM 線形で、人の目には 50% が
 * 7 割程度に見える (WebUI の BRT スライダーの位置と見た目が合わない)。γ=2.2 で補正する:
 *   led = 255 × (pct/100)^2.2      例: 25%→12  50%→55  75%→135  100%→255
 * 0% は消灯、1% 以上は最低 1 (完全に消えないようにする)。
 */
#pragma once
#include <stdint.h>
#include <math.h>

namespace sastle {

static inline uint8_t brightnessPercentToLed(uint8_t pct) {
    if (pct == 0) return 0;
    if (pct >= 100) return 255;
    const float v = powf(pct / 100.0f, 2.2f) * 255.0f;
    const int led = (int)lrintf(v);
    return (uint8_t)(led < 1 ? 1 : (led > 255 ? 255 : led));
}

} // namespace sastle
