/**
 * @file PeriodicTimer.h
 * @brief 「N ms ごとに 1 回」のゲート (純粋、Arduino 非依存)
 *
 * main.cpp / IMUManager / LEDManager に手書きされていた
 *   if (now - last < INTERVAL) return;  last = now;
 * を置き換える。last=0 で始まるので初回は now ≥ interval で発火する (旧グローバルと同じ)。
 * millis() の 32bit 折り返しは差分計算で吸収される。
 */
#pragma once
#include <stdint.h>

namespace sastle {

struct PeriodicTimer {
    uint32_t intervalMs;
    uint32_t last = 0;

    explicit PeriodicTimer(uint32_t interval) : intervalMs(interval) {}

    /// 期限が来ていれば last を now に進めて true。elapsedMs には前回発火からの経過を返す。
    bool due(uint32_t now, uint32_t* elapsedMs = nullptr) {
        const uint32_t elapsed = now - last;
        if (elapsed < intervalMs) return false;
        if (elapsedMs) *elapsedMs = elapsed;
        last = now;
        return true;
    }
};

} // namespace sastle
