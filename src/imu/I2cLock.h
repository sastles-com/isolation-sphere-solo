/**
 * @file I2cLock.h
 * @brief I2C バスの排他 (再帰ミューテックス + RAII ガード)
 *
 * IMU タスク (100Hz で quat を読む) と loop タスク (診断 getter: getCalibration /
 * getOperationMode / getTemperature) が同じ Wire を使う。ESP32 の I2C HAL はロックを
 * API 呼び出しごとにしか取らないため、
 *     endTransmission(false) → repeated start で STOP を出さない
 *     requestFrom(...)
 * の"間"に別コアのトランザクションが割り込むと双方が誤ったレジスタ窓を読む
 * (実測: mode が 8 と 61 を交互に返し、静止中の加速度が 2.2 m/s² になった)。
 * 複数トランザクションの並びを 1 単位として保護する。再帰なのは printStatus() 等が
 * 他の公開 getter を呼ぶため。IMUManager から抽出 (2026-09)。
 */
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace sastle {

class I2cLock {
public:
    /// ミューテックスを作る (複数回呼んでも 1 つ)。false = 作成失敗
    bool begin() {
        if (!_m) _m = xSemaphoreCreateRecursiveMutex();
        return _m != nullptr;
    }
    void lock()   { if (_m) xSemaphoreTakeRecursive(_m, portMAX_DELAY); }
    void unlock() { if (_m) xSemaphoreGiveRecursive(_m); }
private:
    SemaphoreHandle_t _m = nullptr;
};

/// RAII ガード。early return が多い読みループで都度 unlock を書かずに済ませる。
struct I2cGuard {
    I2cLock& l;
    explicit I2cGuard(I2cLock& lk) : l(lk) { l.lock(); }
    ~I2cGuard() { l.unlock(); }
    I2cGuard(const I2cGuard&) = delete;
    I2cGuard& operator=(const I2cGuard&) = delete;
};

} // namespace sastle
