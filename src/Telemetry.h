/**
 * @file Telemetry.h
 * @brief MQTT への周期 publish (imu / state) と [TIMESYNC] ログ
 *
 * 派生元 core の Telemetry から MQTT 依存の部分だけを移植した。solo の周期ログ
 * ([RATE] [QDIAG] [PERF] [SOLO] [IMU]) は main.cpp 側に既にあるのでそのまま。
 * publish はすべて MQTT 接続中のみ (未接続時は何もしない)。loop タスクから呼ぶ。
 */
#pragma once
#include <Arduino.h>
#include <stdint.h>
#include "PeriodicTimer.h"

namespace sastle {

class IMUManager;
class MQTTManager;
class TimeSync;
class DeviceController;

class Telemetry {
public:
    /**
     * @param imuHz  imu publish レート (0 で無効。config telemetry.imu_hz)
     */
    void begin(IMUManager* imu, MQTTManager* mqtt, TimeSync* ts, DeviceController* ctl, uint8_t imuHz);

    /// imu トピック publish (imuHz)
    void publishImuIfDue(uint32_t now);
    /// 5 秒ごと: retained state publish (サーバーの ready 判定に使われる)
    void publishStateIfDue(uint32_t now);
    /// 5 秒ごと: [TIMESYNC]
    void emitTimeSyncIfDue(uint32_t now);

    uint32_t imuPublished() const { return _imuPublished; }
    uint32_t statePublished() const { return _statePublished; }

private:
    IMUManager* _imu = nullptr;
    MQTTManager* _mqtt = nullptr;
    TimeSync* _ts = nullptr;
    DeviceController* _ctl = nullptr;

    PeriodicTimer _imuPub{100};
    PeriodicTimer _state{5000};
    PeriodicTimer _tsync{5000};
    bool _imuEnabled = false;
    uint32_t _imuPublished = 0;
    uint32_t _statePublished = 0;
    char _stateBuf[768];   // DeviceController::getStateJson の doc (768) と揃える
};

} // namespace sastle
