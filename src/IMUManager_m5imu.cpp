#include "IMUManager.h"
#include <Arduino.h>
#include "Log.h"

// M5 内蔵 IMU (BMI270 6 軸 + ソフト Madgwick) バックエンド。
//
// !! QUARANTINED (2026-09): この env (atoms3r_m5imu) はリンクが通らない。
//   共通部 (IMUManager.cpp の taskFunction) が呼ぶ _updateOnce とそのフェーズ関数
//   (_applyPendingClock/_reinitAndResync/_publishAccepted/_readAuxIfDue/_serviceWatchdog)
//   が未実装で、update() を loop() から呼ぶ旧方式のまま。検証できる実機も無い。
//   復活させるなら、Bno055QuatReader に相当する「1 サンプル読む」層と _updateOnce を実装するか、
//   struct ImuBackend { begin(); sampleQuat(raw, Quat&); readAux(); } を切って両バックエンドを
//   載せ替える。sphere001/002 は BNO055 (IMUManager_bno055.cpp)。
#if defined(IMU_SENSOR_M5IMU)

namespace sastle {

IMUManager::IMUManager()
    : _initialized(false),
      _lastUpdate(0),
      _ahrs(0.1f),
      _lastMicros(0),
      _biasReady(false),
      _gyroBias{0.0f, 0.0f, 0.0f},
      _reader(Wire, 0x28, _i2c) {
}
bool IMUManager::begin(ConfigManager& config, uint8_t sda, uint8_t scl) {
    (void)sda; (void)scl;  // 内蔵IMUは内部I2Cバスを使うため外部ピン指定は不使用
    Serial.println("\n=== IMU Manager Initialization (M5 internal IMU) ===");

    // I2C 排他用 (BNO055 側と同じ理由: IMUタスクと loop の同時アクセス防止)
    if (!_i2c.begin()) {
        Serial.println("[IMU] I2C mutex creation failed");
        return false;
    }

    // M5Unified 本体初期化。LCDManager は LCDデバッグ無効時に M5.begin を呼ばないため、
    // IMU 側で確実に内部I2C/IMU電源を立ち上げておく (二重 begin は M5Unified が許容)。
    auto cfg = M5.config();
    M5.begin(cfg);

    if (!M5.Imu.begin()) {
        Serial.println("M5 internal IMU not detected!");
        return false;
    }

    const char* name = "unknown";
    switch (M5.Imu.getType()) {
        case m5::imu_bmi270:  name = "BMI270";  break;
        case m5::imu_mpu6886: name = "MPU6886"; break;
        case m5::imu_mpu6050: name = "MPU6050"; break;
        case m5::imu_mpu9250: name = "MPU9250"; break;
        case m5::imu_sh200q:  name = "SH200Q";  break;
        default: break;
    }
    Serial.printf("M5 internal IMU detected: %s (6-axis fusion)\n", name);

    _initialized = true;
    _biasReady = false;   // ジャイロバイアスは初回 update() で静止データから推定
    _lastMicros = micros();
    Serial.println("M5 internal IMU initialized (gyro bias pending)");

    return true;
}

bool IMUManager::update() {
    if (!_initialized) {
        return false;
    }

    unsigned long now = millis();
    if (now - _lastUpdate < UPDATE_INTERVAL) {
        return true; // まだ更新タイミングではない
    }
    _lastUpdate = now;

    {
        I2cGuard guard(_i2c);   // loop 側の getTemperature() と排他
        M5.Imu.update();
    }

    // 全初期化 (LCDManager の M5.begin を含む) 完了後の最初の機会にバイアスを推定。
    // begin() 直後に行うと LCDManager の後続 M5.begin で IMU が再初期化され無効化される。
    if (!_biasReady) {
        calibrateGyroBias();
        _lastMicros = micros();  // 校正に要した時間は dt から除外
        return true;
    }

    float ax, ay, az;   // 加速度 [G]
    float gx, gy, gz;   // 角速度 [deg/s]
    M5.Imu.getAccel(&ax, &ay, &az);
    M5.Imu.getGyro(&gx, &gy, &gz);

    // バイアス除去 (deg/s)
    float cgx = gx - _gyroBias[0];
    float cgy = gy - _gyroBias[1];
    float cgz = gz - _gyroBias[2];

    // dt 算出 (異常値はガード)
    unsigned long nowUs = micros();
    float dt = (nowUs - _lastMicros) * 1e-6f;
    _lastMicros = nowUs;
    if (dt <= 0.0f || dt > 0.5f) {
        dt = (float)UPDATE_INTERVAL * 1e-3f;
    }

    // Madgwick 6軸融合 (ジャイロは rad/s 入力)
    const float DEG2RAD = 0.0174532925199433f;
    _ahrs.updateIMU(cgx * DEG2RAD, cgy * DEG2RAD, cgz * DEG2RAD,
                    ax, ay, az, dt);

    // 公開 API 互換のため imu:: 型へ詰め替え。単位は BNO055 に合わせる:
    //   quat=正規化, euler=度, accel=m/s², gyro=deg/s
    const imu::Quaternion qRaw(_ahrs.w(), _ahrs.x(), _ahrs.y(), _ahrs.z());
    const Quat qs = _smoother.push(Quat{(float)_ahrs.w(), (float)_ahrs.x(),
                                        (float)_ahrs.y(), (float)_ahrs.z()});
    _quat = qRaw;
    _quatOut = imu::Quaternion(qs.w, qs.x, qs.y, qs.z);
    float heading, roll, pitch;
    _ahrs.getEulerDeg(heading, roll, pitch);
    _euler = imu::Vector<3>(heading, roll, pitch);

    const float G = 9.80665f;
    _accel = imu::Vector<3>(ax * G, ay * G, az * G);
    _gyro  = imu::Vector<3>(cgx, cgy, cgz);

    return true;
}

void IMUManager::calibrateGyroBias() {
    Serial.println("[IMU] Calibrating gyro bias... keep the device still (~1s)");

    // M5 の自動オフセット補正は自前のバイアス推定と二重補正になるため無効化
    M5.Imu.setCalibration(0, 0, 0);

    const int kSamples = 200;
    double sx = 0.0, sy = 0.0, sz = 0.0;
    int got = 0;
    for (int i = 0; i < kSamples; ++i) {
        M5.Imu.update();
        float gx, gy, gz;
        if (M5.Imu.getGyro(&gx, &gy, &gz)) {
            sx += gx; sy += gy; sz += gz;
            ++got;
        }
        delay(5);  // 約1秒で 200サンプル
    }

    if (got > 0) {
        _gyroBias[0] = (float)(sx / got);
        _gyroBias[1] = (float)(sy / got);
        _gyroBias[2] = (float)(sz / got);
    } else {
        _gyroBias[0] = _gyroBias[1] = _gyroBias[2] = 0.0f;
    }
    _biasReady = true;

    Serial.printf("[IMU] Gyro bias [deg/s]: x=%.4f y=%.4f z=%.4f (n=%d)\n",
                  _gyroBias[0], _gyroBias[1], _gyroBias[2], got);
}
bool IMUManager::_reinitSensor() {
    return false;   // 内蔵 IMU は M5Unified 管理。個別リセットは未対応
}
void IMUManager::displayCalibrationStatus() {
    if (!_initialized) {
        Serial.println("IMU not initialized!");
        return;
    }

    Serial.println("\n=== IMU Status (M5 internal, 6-axis) ===");
    Serial.printf("Gyro bias: %s [deg/s] x=%.4f y=%.4f z=%.4f\n",
                  _biasReady ? "ready" : "pending",
                  _gyroBias[0], _gyroBias[1], _gyroBias[2]);
    Serial.println("(no magnetometer: heading drifts over time)");
    Serial.println("========================================");
}

void IMUManager::getCalibration(uint8_t& sys, uint8_t& gyro, uint8_t& accel, uint8_t& mag) {
    // BNO055 互換の 0..3 スケールに写像。地磁気を持たないため mag は常に 0。
    uint8_t level = (_initialized && _biasReady) ? 3 : 0;
    sys = level;
    gyro = level;
    accel = level;
    mag = 0;
}

int8_t IMUManager::getTemperature() {
    if (!_initialized) {
        return 0;
    }
    float t = 0.0f;
    I2cGuard guard(_i2c);
    M5.Imu.getTemp(&t);
    return (int8_t)t;
}

uint8_t IMUManager::getOperationMode() {
    return 0xFF;
}

} // namespace sastle

#endif // IMU_SENSOR_M5IMU
