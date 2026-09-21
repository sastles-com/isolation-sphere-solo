#include "IMUManager.h"
#include <Arduino.h>

namespace sastle {

#if defined(IMU_SENSOR_M5IMU)
IMUManager::IMUManager()
    : _initialized(false),
      _lastUpdate(0),
      _ahrs(0.1f),
      _lastMicros(0),
      _biasReady(false),
      _gyroBias{0.0f, 0.0f, 0.0f},
      _lastDriftLog(0) {
}
#else
IMUManager::IMUManager()
    : _initialized(false),
      _lastUpdate(0),
      _bno(55, 0x28) { // BNO055のI2Cアドレス: 0x28 (ADRピンがLOW)
}
#endif

IMUManager::~IMUManager() {
}

#if !defined(IMU_SENSOR_M5IMU)
bool IMUManager::begin(ConfigManager& config, uint8_t sda, uint8_t scl) {
    Serial.println("\n=== IMU Manager Initialization (BNO055) ===");

    // I2C初期化（既に初期化されている場合はスキップ）
    // BNO055 は I2C クロックストレッチが長く、400kHz ではレジスタ読み出しが
    // 化ける (quat の w だけ動き x/y/z=0、ノルム≠1 の壊れた値を実機で確認)。
    // 100kHz (エラー検出+リトライ付き直接読みがあるため、失敗は検出・再試行される)。
    if (!Wire.begin(sda, scl, 100000)) {
        Serial.println("I2C bus initialization failed or already initialized");
    }
    delay(100);

    Serial.printf("I2C initialized (SDA: GPIO%d, SCL: GPIO%d)\n", sda, scl);

    // BNO055初期化
    if (!_bno.begin()) {
        Serial.println("BNO055 not detected! Check wiring.");
        return false;
    }

    Serial.println("BNO055 detected!");

    // センサー情報表示
    sensor_t sensor;
    _bno.getSensor(&sensor);
    Serial.println("------------------------------------");
    Serial.print("Sensor:       "); Serial.println(sensor.name);
    Serial.print("Driver Ver:   "); Serial.println(sensor.version);
    Serial.print("Unique ID:    "); Serial.println(sensor.sensor_id);
    Serial.print("Max Value:    "); Serial.print(sensor.max_value); Serial.println(" xxx");
    Serial.print("Min Value:    "); Serial.print(sensor.min_value); Serial.println(" xxx");
    Serial.print("Resolution:   "); Serial.print(sensor.resolution); Serial.println(" xxx");
    Serial.println("------------------------------------");

    delay(100);

    // 外部クリスタルは使用しない。
    // 注意: 外部水晶が実装されていないボードで true にすると、生センサー値
    // (gyro/accel) は読めるのに fusion 出力 (quat) が単位のまま固まる。
    // 実機で cal=0300 + quat恒久(1,0,0,0) の症状を確認したため false に変更。
    _bno.setExtCrystalUse(false);

    // 動作モードは IMUPLUS (加速度+ジャイロの6軸融合、磁気不使用)。
    // NDOF (9軸) は磁気キャリブレーション未完了時にヨーが「動かない→90°ジャンプ」
    // する上、本機はLED大電流・LiPoが磁気センサー近傍にあり磁気データが常に乱れる
    // ため不採用。IMUPLUS はヨーがジャイロ積分で滑らか (ドリフトは SET ZERO で解消)。
    // 注意: BNO055 は fusionモード間の直接切替を無視するため、必ず CONFIG を経由する
    // (begin() が NDOF にするので、直接 IMUPLUS を書いても効かない)。
    _bno.setMode(OPERATION_MODE_CONFIG);
    delay(25);
    _bno.setMode(OPERATION_MODE_IMUPLUS);
    delay(20);

    // 診断: モード/システム状態を起動ログに残す (mode=8 が IMUPLUS)
    {
        uint8_t sysStatus = 0, selfTest = 0, sysError = 0;
        _bno.getSystemStatus(&sysStatus, &selfTest, &sysError);
        Serial.printf("[IMU] boot diag: mode=%d sys_status=%u selftest=0x%X sys_error=%u\n",
                      (int)_bno.getMode(), sysStatus, selfTest, sysError);
    }

    _initialized = true;
    Serial.println("BNO055 initialized successfully!");

    // 初期キャリブレーション状態表示
    displayCalibrationStatus();

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

    // データ取得: Adafruit の getQuat() は I2C 失敗を無視して古いバッファを返す
    // (実測: 回転中でも約8割のサンプルが停滞→たまに成功してジャンプ)。
    // エラー検出付きの直接レジスタ読みに置き換え、失敗時は前回値を保持する。
    imu::Quaternion q = _quat;  // 読めなければ前回値
    {
        bool readOk = false;
        for (int attempt = 0; attempt < 2 && !readOk; attempt++) {
            Wire.beginTransmission(0x28);
            Wire.write(0x20);  // QUA_DATA_W_LSB
            if (Wire.endTransmission(false) != 0) continue;   // repeated start
            if (Wire.requestFrom(0x28, 8) != 8) continue;
            uint8_t b[8];
            for (int i = 0; i < 8; i++) b[i] = Wire.read();
            const float s = 1.0f / 16384.0f;  // 2^14 LSB/unit
            q = imu::Quaternion(
                (int16_t)((b[1] << 8) | b[0]) * s,
                (int16_t)((b[3] << 8) | b[2]) * s,
                (int16_t)((b[5] << 8) | b[4]) * s,
                (int16_t)((b[7] << 8) | b[6]) * s);
            readOk = true;
        }
        if (!readOk) _wdReadFails++;
        _wdReadTotal++;
        if (_wdReadTotal % 1000 == 0) {
            Serial.printf("[IMU] quat read stats: fails=%lu/%lu\n",
                          (unsigned long)_wdReadFails, (unsigned long)_wdReadTotal);
        }
    }
    // I2C化けガード1: このBNO055個体はノルム0.96〜0.99のquatを常用するため、
    // 「単位でない=破棄」にすると正常値の半分を捨ててしまう (実測 disc=loopの
    // 半分〜全部でカクつきの主因になった)。妥当な範囲なら正規化して受理し、
    // 明らかな化け (ノルムが大きく崩れている) だけ破棄する。
    const float n2 = (float)(q.w()*q.w() + q.x()*q.x() + q.y()*q.y() + q.z()*q.z());
    bool ok = (n2 > 0.5f && n2 < 2.0f);
    if (ok && fabsf(n2 - 1.0f) > 1e-4f) {
        const float inv = 1.0f / sqrtf(n2);
        q = imu::Quaternion(q.w() * inv, q.x() * inv, q.y() * inv, q.z() * inv);
    }
    // I2C化けガード2 (連続性): 1サンプル(10ms)で25°超の姿勢ジャンプは物理的に
    // あり得ない (2500°/s)。ノルムが偶然1に近い化け値もこれで弾く。
    // q と -q は同じ回転なので |dot| で比較する。
    if (ok) {
        const float dot = fabsf((float)(q.w()*_quat.w() + q.x()*_quat.x() +
                                        q.y()*_quat.y() + q.z()*_quat.z()));
        // 動的しきい値: 前回受理からの経過時間×角速度に余裕を掛けた角度まで許容。
        // 固定25°だと「読み損ねの隙間+速い回転」で正常値を誤破棄し追従が止まる。
        static unsigned long lastAcceptMs = 0;
        float dts = (now - lastAcceptMs) * 0.001f;
        if (dts < 0.01f) dts = 0.01f;
        if (dts > 0.5f)  dts = 0.5f;
        const float gyroMag = fabsf((float)_gyro.x()) + fabsf((float)_gyro.y()) +
                              fabsf((float)_gyro.z());          // [rad/s]
        const float maxAngle = 0.26f + gyroMag * dts * 1.5f;    // [rad] 基本15°+速度比例
        const float dotClamped = dot > 1.0f ? 1.0f : dot;
        const float ang = 2.0f * acosf(dotClamped);             // 姿勢差 [rad]
        if (ang > maxAngle && _wdConsecDiscards < 10) {
            ok = false;
        } else {
            lastAcceptMs = now;
        }
    }
    if (ok) {
        // renderタスク (core1) が読む最中の引き裂かれ (torn read) を防ぐ
        taskENTER_CRITICAL(&_quatMux);
        _quat = q;
        taskEXIT_CRITICAL(&_quatMux);
        _wdConsecDiscards = 0;
    } else {
        static unsigned long lastBadLog = 0;
        _wdDiscards++;
        _wdConsecDiscards++;
        if (now - lastBadLog > 5000) {
            lastBadLog = now;
            Serial.printf("[IMU] Discarded corrupt quat (|q|^2=%.3f, discards=%u)\n",
                          n2, _wdDiscards);
        }
    }
    // I2C時間の節約: quat以外は間引いて読む (accel/gyro=2回に1回, euler=10回に1回)
    static uint8_t subCycle = 0;
    subCycle++;
    if ((subCycle & 1) == 0) {
        _accel = _bno.getVector(Adafruit_BNO055::VECTOR_ACCELEROMETER);
        _gyro = _bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE);
    }
    if (subCycle >= 10) {
        subCycle = 0;
        _euler = _bno.getVector(Adafruit_BNO055::VECTOR_EULER);
    }

    // 融合停止ウォッチドッグ (電源瞬断で BNO055 の fusion が止まり、quat が
    // 姿勢に追従しなくなる事象への対策)。2秒ごとに SYS_STATUS を確認し、
    // 「fusion動作中(=5)」以外なら再初期化する。加えて「回転している
    // (gyroが大きい) のに quat が動かない」場合も融合停止と判定する。
    if (now - _wdLastChange > 2000) {
        _wdLastChange = now;

        uint8_t sysStatus = 0, selfTest = 0, sysError = 0;
        _bno.getSystemStatus(&sysStatus, &selfTest, &sysError);

        const float gyroMag = fabsf((float)_gyro.x()) + fabsf((float)_gyro.y()) +
                              fabsf((float)_gyro.z());
        const float dq = fabsf((float)_quat.w() - _wdPrev[0]) +
                         fabsf((float)_quat.x() - _wdPrev[1]) +
                         fabsf((float)_quat.y() - _wdPrev[2]) +
                         fabsf((float)_quat.z() - _wdPrev[3]);
        _wdPrev[0] = (float)_quat.w(); _wdPrev[1] = (float)_quat.x();
        _wdPrev[2] = (float)_quat.y(); _wdPrev[3] = (float)_quat.z();

        const bool fusionDead = (sysStatus != 5) || (gyroMag > 0.3f && dq < 0.001f);
        if (fusionDead && false) {  // 調査中は自動復旧を無効化 (thrashing防止)
            _wdRecoveries++;
            Serial.printf("[IMU] Fusion dead (sys=%u err=%u gyro=%.2f dq=%.4f) — recover #%u\n",
                          sysStatus, sysError, gyroMag, dq, _wdRecoveries);
            if (_wdRecoveries % 3 != 0) {
                // まずモード入れ直し (軽量)
                _bno.setMode(OPERATION_MODE_CONFIG);
                delay(25);
                _bno.setMode(OPERATION_MODE_IMUPLUS);
                delay(20);
            } else {
                // 3回に1回はフル再初期化 (リセット込み)
                Serial.println("[IMU] Mode cycle insufficient — full re-begin");
                if (_bno.begin()) {
                    delay(50);
                    _bno.setExtCrystalUse(false);  // begin() と同じ設定 (外部水晶なし)
                    // begin() は NDOF にするため CONFIG 経由で IMUPLUS へ
                    _bno.setMode(OPERATION_MODE_CONFIG);
                    delay(25);
                    _bno.setMode(OPERATION_MODE_IMUPLUS);
                    delay(20);
                }
            }
        }
    }

    return true;
}

#else  // IMU_SENSOR_M5IMU
bool IMUManager::begin(ConfigManager& config, uint8_t sda, uint8_t scl) {
    (void)sda; (void)scl;  // 内蔵IMUは内部I2Cバスを使うため外部ピン指定は不使用
    Serial.println("\n=== IMU Manager Initialization (M5 internal IMU) ===");

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

    M5.Imu.update();

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
    _quat = imu::Quaternion(_ahrs.w(), _ahrs.x(), _ahrs.y(), _ahrs.z());
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
#endif

uint8_t IMUManager::getOperationMode() {
#if !defined(IMU_SENSOR_M5IMU)
    return _initialized ? (uint8_t)_bno.getMode() : 0xFE;
#else
    return 0xFF;
#endif
}

imu::Quaternion IMUManager::getQuaternion() {
    return _quat;
}

bool IMUManager::getQuaternion(float& w, float& x, float& y, float& z) {
    if (!_initialized) {
        return false;
    }

    // センサー→LED座標系の変換: BNO055 の quaternion は本システムの規約と
    // 回転の向きが逆 (実機テスト 2026-08-23: 生値では X/Y/Z 全軸で補正方向が逆)。
    // 完全共役 q → q* = (w,-x,-y,-z) で逆回転に変換する。
    // ここで変換することで LED描画・MQTT配信(ツイン)・ジェスチャすべてに
    // 一括適用される。読み出しはコア間排他の下でコピーする (torn read防止)。
    taskENTER_CRITICAL(&_quatMux);
    const float qw = (float)_quat.w(), qx = (float)_quat.x(),
                qy = (float)_quat.y(), qz = (float)_quat.z();
    taskEXIT_CRITICAL(&_quatMux);
    w = qw;
    x = -qx;
    y = -qy;
    z = -qz;

    return true;
}

imu::Vector<3> IMUManager::getEuler() {
    return _euler;
}

bool IMUManager::getEuler(float& heading, float& roll, float& pitch) {
    if (!_initialized) {
        return false;
    }
    
    heading = _euler.x();
    roll = _euler.y();
    pitch = _euler.z();
    
    return true;
}

imu::Vector<3> IMUManager::getAccel() {
    return _accel;
}

bool IMUManager::getAccel(float& x, float& y, float& z) {
    if (!_initialized) {
        return false;
    }
    
    x = _accel.x();
    y = _accel.y();
    z = _accel.z();
    
    return true;
}

imu::Vector<3> IMUManager::getGyro() {
    return _gyro;
}

bool IMUManager::getGyro(float& x, float& y, float& z) {
    if (!_initialized) {
        return false;
    }
    
    x = _gyro.x();
    y = _gyro.y();
    z = _gyro.z();
    
    return true;
}

#if !defined(IMU_SENSOR_M5IMU)
void IMUManager::displayCalibrationStatus() {
    if (!_initialized) {
        Serial.println("IMU not initialized!");
        return;
    }

    uint8_t system, gyro, accel, mag = 0;
    _bno.getCalibration(&system, &gyro, &accel, &mag);

    Serial.println("\n=== Calibration Status ===");
    Serial.print("System: "); Serial.print(system, DEC);
    Serial.print(" Gyro: "); Serial.print(gyro, DEC);
    Serial.print(" Accel: "); Serial.print(accel, DEC);
    Serial.print(" Mag: "); Serial.println(mag, DEC);
    Serial.println("(0=uncalibrated, 3=fully calibrated)");
    Serial.println("==========================");
}

bool IMUManager::isFullyCalibrated() {
    if (!_initialized) {
        return false;
    }

    uint8_t system, gyro, accel, mag = 0;
    _bno.getCalibration(&system, &gyro, &accel, &mag);

    return (system == 3 && gyro == 3 && accel == 3 && mag == 3);
}

void IMUManager::getCalibration(uint8_t& sys, uint8_t& gyro, uint8_t& accel, uint8_t& mag) {
    if (_initialized) {
        _bno.getCalibration(&sys, &gyro, &accel, &mag);
    } else {
        sys = gyro = accel = mag = 0;
    }
}

int8_t IMUManager::getTemperature() {
    if (!_initialized) {
        return 0;
    }
    return _bno.getTemp();
}

#else  // IMU_SENSOR_M5IMU
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

bool IMUManager::isFullyCalibrated() {
    // 6軸構成にオンチップ校正レベルは無いため、ジャイロバイアス推定済みを完了とみなす
    return _initialized && _biasReady;
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
    M5.Imu.getTemp(&t);
    return (int8_t)t;
}
#endif

void IMUManager::printStatus() {
    Serial.println("\n=== IMU Status ===");
    
    if (!_initialized) {
        Serial.println("Status: Not initialized");
        Serial.println("==================");
        return;
    }
    
    Serial.println("Status: Initialized");
    Serial.printf("Temperature: %d°C\n", getTemperature());
    
    // キャリブレーション状態
    uint8_t system, gyro, accel, mag = 0;
    getCalibration(system, gyro, accel, mag);
    Serial.printf("Calibration - Sys:%d Gyro:%d Accel:%d Mag:%d\n", 
                  system, gyro, accel, mag);
    
    // クォータニオン
    Serial.printf("Quaternion - w:%.3f x:%.3f y:%.3f z:%.3f\n",
                  _quat.w(), _quat.x(), _quat.y(), _quat.z());
    
    // オイラー角
    Serial.printf("Euler - Heading:%.2f° Roll:%.2f° Pitch:%.2f°\n",
                  _euler.x(), _euler.y(), _euler.z());
    
    Serial.println("==================");
}

} // namespace sastle
