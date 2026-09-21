/**
 * @file IMUManager.h
 * @brief IMUセンサー管理クラス (BNO055 / M5Atom 内蔵 BMI270 をビルド時に切替)
 * @author sastle-com
 * @date 2025-12-01
 *
 * バックエンドは build_flags の -D で選択する:
 *   -D IMU_SENSOR_BNO055  : 外部 BNO055 (9軸オンチップ融合, NDOF) ※既定
 *   -D IMU_SENSOR_M5IMU   : M5Atom 内蔵 BMI270 (6軸) + ソフト Madgwick 融合
 * いずれの場合も公開 API (getQuaternion/getEuler/getAccel/getGyro) は同一で、
 * 戻り値型 imu::Quaternion / imu::Vector<3> も共通 (imumaths.h はヘッダオンリ)。
 */

#pragma once

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <utility/imumaths.h>
#include "ConfigManager.h"
#include "BoardConfig.h"

// --- バックエンド選択 ---
//   既定は BNO055。build_flags に -D IMU_SENSOR_M5IMU を足すと、(BNO055 が同時に
//   定義されていても) M5内蔵IMU を優先する。これにより M5IMU 用 env は通常 env を
//   extends したうえで -D IMU_SENSOR_M5IMU を1行足すだけで切替できる。
#if !defined(IMU_SENSOR_BNO055) && !defined(IMU_SENSOR_M5IMU)
#define IMU_SENSOR_BNO055
#endif

#if defined(IMU_SENSOR_M5IMU)
#include <M5Unified.h>
#include "MadgwickAHRS.h"
#else
#include <Adafruit_BNO055.h>
#endif

namespace sastle {

/**
 * @class IMUManager
 * @brief IMUセンサーを管理するクラス
 *
 * クォータニオン、オイラー角、加速度、ジャイロデータの取得機能を提供します。
 * 100Hzでの高速データ更新に対応しています。
 * BNO055 はオンチップ9軸融合、M5IMU は BMI270(6軸)+ソフト融合で姿勢を推定します。
 */
class IMUManager {
public:
    IMUManager();
    ~IMUManager();
    
    /**
     * @brief IMUセンサーを初期化
     * @param config 設定マネージャー参照
     * @param sda I2C SDAピン番号 (デフォルト: kImuI2cSda)
     * @param scl I2C SCLピン番号 (デフォルト: kImuI2cScl)
     * @return true 初期化成功, false 初期化失敗
     */
    bool begin(ConfigManager& config, uint8_t sda = kImuI2cSda, uint8_t scl = kImuI2cScl);
    
    /**
     * @brief センサーデータを更新
     * @return true 更新成功, false 更新失敗
     * @note 100Hz (10ms間隔) で呼び出すことを推奨
     */
    bool update();
    
    /**
     * @brief クォータニオンを取得
     * @return クォータニオンベクトル (w, x, y, z)
     */
    imu::Quaternion getQuaternion();
    
    /**
     * @brief クォータニオンを個別変数で取得
     * @param w クォータニオンw成分 (出力)
     * @param x クォータニオンx成分 (出力)
     * @param y クォータニオンy成分 (出力)
     * @param z クォータニオンz成分 (出力)
     * @return true 取得成功, false 取得失敗
     */
    bool getQuaternion(float& w, float& x, float& y, float& z);
    
    /**
     * @brief オイラー角を取得
     * @return オイラー角ベクトル (heading, roll, pitch)
     */
    imu::Vector<3> getEuler();
    
    /**
     * @brief オイラー角を個別変数で取得
     * @param heading ヘディング角 (yaw, 度) (出力)
     * @param roll ロール角 (度) (出力)
     * @param pitch ピッチ角 (度) (出力)
     * @return true 取得成功, false 取得失敗
     */
    bool getEuler(float& heading, float& roll, float& pitch);
    
    /**
     * @brief 加速度を取得
     * @return 加速度ベクトル (x, y, z) [m/s²]
     */
    imu::Vector<3> getAccel();
    
    /**
     * @brief 加速度を個別変数で取得
     * @param x X軸加速度 [m/s²] (出力)
     * @param y Y軸加速度 [m/s²] (出力)
     * @param z Z軸加速度 [m/s²] (出力)
     * @return true 取得成功, false 取得失敗
     */
    bool getAccel(float& x, float& y, float& z);
    
    /**
     * @brief ジャイロデータを取得
     * @return ジャイロベクトル (x, y, z) [rad/s]
     */
    imu::Vector<3> getGyro();
    
    /**
     * @brief ジャイロデータを個別変数で取得
     * @param x X軸角速度 [rad/s] (出力)
     * @param y Y軸角速度 [rad/s] (出力)
     * @param z Z軸角速度 [rad/s] (出力)
     * @return true 取得成功, false 取得失敗
     */
    bool getGyro(float& x, float& y, float& z);
    
    /**
     * @brief キャリブレーション状態を表示
     */
    void displayCalibrationStatus();
    
    /**
     * @brief 完全キャリブレーション状態を確認
     * @return true 全センサーがキャリブレーション済み, false 未完了
     */
    bool isFullyCalibrated();
    
    /**
     * @brief キャリブレーション状態を個別取得
     * @param sys システムキャリブレーション値 (0-3) (出力)
     * @param gyro ジャイロキャリブレーション値 (0-3) (出力)
     * @param accel 加速度計キャリブレーション値 (0-3) (出力)
     * @param mag 磁気計キャリブレーション値 (0-3) (出力)
     */
    void getCalibration(uint8_t& sys, uint8_t& gyro, uint8_t& accel, uint8_t& mag);

    /**
     * @brief 現在の動作モードレジスタ値 (BNO055: 8=IMUPLUS, 12=NDOF / M5IMU: 255)
     */
    uint8_t getOperationMode();
    
    /**
     * @brief IMUステータスを表示
     */
    void printStatus();
    
    /**
     * @brief 初期化状態を取得
     * @return true 初期化済み, false 未初期化
     */
    bool isInitialized() { return _initialized; }

    // 計装用カウンタ (main の周期ログでレート計算に使う)
    uint32_t debugReadTotal() const { return _wdReadTotal; }
    uint32_t debugReadFails() const { return _wdReadFails; }
    uint32_t debugDiscards()  const { return _wdDiscards; }
    
    /**
     * @brief センサー温度を取得
     * @return 温度 (℃)
     */
    int8_t getTemperature();
    
private:
    bool _initialized;             ///< 初期化状態フラグ

    imu::Quaternion _quat;         ///< 最新クォータニオン
    imu::Vector<3> _euler;         ///< 最新オイラー角 (x=heading, y=roll, z=pitch)
    imu::Vector<3> _accel;         ///< 最新加速度 [m/s²]
    imu::Vector<3> _gyro;          ///< 最新ジャイロ [rad/s]

    unsigned long _lastUpdate;     ///< 最終更新時刻 (ms)
    const unsigned long UPDATE_INTERVAL = 10; ///< 更新間隔 10ms = 100Hz

    // 凍結検知ウォッチドッグ (BNO055 が電源瞬断等で融合停止しレジスタが
    // 固まる事象への対策)。健全なセンサーはノイズで毎回値が揺らぐため、
    // quat/gyro/accel が全てビット同一のまま長時間続いたら凍結と判定する。
    float _wdPrev[10] = {0};       ///< 前回読み値 (quat4 + gyro3 + accel3)
    unsigned long _wdLastChange = 0; ///< 最後に値が変化した時刻 (ms)
    uint16_t _wdRecoveries = 0;    ///< 復旧試行回数
    uint32_t _wdDiscards = 0;      ///< 化けquat破棄の累計
    uint8_t _wdConsecDiscards = 0; ///< 連続破棄数 (再同期判定用)
    portMUX_TYPE _quatMux = portMUX_INITIALIZER_UNLOCKED; ///< _quat のコア間排他
    uint32_t _wdReadFails = 0;     ///< I2C quat読み出し失敗の累計
    uint32_t _wdReadTotal = 0;     ///< I2C quat読み出し試行の累計
    static constexpr unsigned long kFrozenTimeoutMs = 10000;

#if defined(IMU_SENSOR_M5IMU)
    MadgwickAHRS _ahrs;            ///< ソフト姿勢推定フィルタ (6軸)
    unsigned long _lastMicros;     ///< 前回更新のマイクロ秒 (dt算出用)
    bool _biasReady;               ///< ジャイロバイアス校正済みフラグ
    float _gyroBias[3];            ///< ジャイロゼロ点バイアス [deg/s] (x,y,z)
    unsigned long _lastDriftLog;   ///< ドリフト計測ログの最終出力時刻 (ms)

    /// @brief 起動時に静止状態のジャイロを平均してバイアスを推定
    void calibrateGyroBias();
#else
    Adafruit_BNO055 _bno;          ///< BNO055センサーオブジェクト
#endif
};

} // namespace sastle
