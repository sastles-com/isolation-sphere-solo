#include "Telemetry.h"

#include "DeviceController.h"
#include "IMUManager.h"
#include "Log.h"
#include "MQTTManager.h"
#include "TimeSync.h"

namespace sastle {

void Telemetry::begin(IMUManager* imu, MQTTManager* mqtt, TimeSync* ts, DeviceController* ctl, uint8_t imuHz) {
    _imu = imu;
    _mqtt = mqtt;
    _ts = ts;
    _ctl = ctl;
    _imuEnabled = (imuHz > 0);
    if (_imuEnabled) {
        _imuPub.intervalMs = 1000 / imuHz;
    }
    Log.printf("[Telemetry] imu publish %s (%u Hz), state every %lus\n",
               _imuEnabled ? "on" : "off", (unsigned)imuHz, (unsigned long)(_state.intervalMs / 1000));
}

void Telemetry::publishImuIfDue(uint32_t now) {
    if (!_imuEnabled || !_imu || !_mqtt || !_mqtt->isConnected()) return;
    if (!_imuPub.due(now)) return;
    float w, x, y, z;
    if (!_imu->getQuaternion(w, x, y, z)) return;
    char payload[128];
    snprintf(payload, sizeof(payload), "{\"w\":%.4f,\"x\":%.4f,\"y\":%.4f,\"z\":%.4f}", w, x, y, z);
    if (_mqtt->publishDevice("imu", payload, false)) {
        _imuPublished++;
    }
}

void Telemetry::publishStateIfDue(uint32_t now) {
    if (!_ctl || !_mqtt || !_mqtt->isConnected()) return;
    if (!_state.due(now)) return;
    if (_ctl->getStateJson(_stateBuf, sizeof(_stateBuf))) {
        if (_mqtt->publishDevice("state", _stateBuf, true)) {   // retained
            _statePublished++;
        }
    }
}

void Telemetry::emitTimeSyncIfDue(uint32_t now) {
    if (!_ts || !_mqtt || !_mqtt->isEnabled()) return;
    if (!_tsync.due(now)) return;
    if (_ts->isSynced()) {
        Log.printf("[TIMESYNC] synced now=%lld ms offset=%lld ms seq=%lu\n",
                   (long long)_ts->syncedNow(), (long long)_ts->offset(), (unsigned long)_ts->lastSeq());
    } else if (_mqtt->isConnected()) {
        Log.println("[TIMESYNC] not synced yet (no clock beacon received)");
    }
}

} // namespace sastle
