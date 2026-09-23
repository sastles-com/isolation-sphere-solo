/**
 * @file DeviceController.h
 * @brief 制御面 (HTTP / MQTT / シリアルコンソール) が共通で呼ぶ操作の窓口
 *
 * 統合ファームでは Web UI (SoloWebServer)、MQTT (CommandHandler)、シリアルコンソールの
 * 3 つから同じ操作が来る。各制御面は「パース → DeviceController → 応答」だけにして、
 * 実際の適用 (LED / 再生 / IMU / 設定の永続化) はここに 1 か所で書く。
 *   - 明るさ % → LED 値は Gamma.h (γ=2.2) に統一 (派生元 server と同じ見え方)
 *   - 利用者が変えた値は Settings (NVS) に保存 (どの制御面から変えても次回起動で復元)
 *   - 再起動は予約制 (Settings::flush → 応答を返してから loop の tick() で実行)
 *
 * 呼び出し文脈: loopTask と httpd タスクの両方。操作は単純なセッタか、SoloPlayer のように
 * 内部でミューテックスを持つものだけなので、ここでは追加のロックを取らない。
 */

#ifndef __DEVICE_CONTROLLER_H__
#define __DEVICE_CONTROLLER_H__

#include <Arduino.h>
#include <ArduinoJson.h>

#include "ConfigManager.h"
#include "IMUManager.h"
#include "LEDManager.h"
#include "NetworkManager.h"
#include "SoloPlayer.h"

namespace sastle {

class DeviceController {
public:
    struct Deps {
        ConfigManager* config = nullptr;
        SoloPlayer* player = nullptr;
        LEDManager* led = nullptr;
        IMUManager* imu = nullptr;       ///< 未検出なら nullptr
        NetworkManager* net = nullptr;
    };

    enum class PlayResult : uint8_t { Ok, NoVideo, Uploading, Unavailable };
    enum class LedMode : uint8_t { Sphere, Pixels, Off, Test };

    /// 依存を受け取り、起動時の明るさ (NVS > config.params) と軸表示を LED に適用する
    void begin(const Deps& deps);

    /// loop() から呼ぶ: 設定の遅延保存と予約済み再起動の実行
    void tick();

    // --- 再生 ---
    PlayResult play();
    PlayResult pause();
    PlayResult stop();
    PlayResult toggle();
    static const char* playResultMessage(PlayResult r);
    /// MQTT state 用: playing | paused | stopped
    const char* playbackStatus() const;

    // --- 表示パラメータ (params) ---
    void setBrightnessPct(uint8_t pct);        ///< Gamma 適用 + NVS 保存
    uint8_t brightnessPct() const { return _brightness; }
    void setSpeed(uint8_t v) { _speed = v; }
    void setHue(uint16_t v) { _hue = v; }
    void setSaturation(uint8_t v) { _saturation = v; }
    uint8_t speed() const { return _speed; }
    uint16_t hue() const { return _hue; }
    uint8_t saturation() const { return _saturation; }

    // --- LED ---
    /// sphere | pixels | off | test。test は低輝度を強制し、戻るときに params の明るさを再適用
    void setLedMode(LedMode mode);
    static bool parseLedMode(const char* name, LedMode& out);
    LedMode ledMode() const { return _ledMode; }
    const char* ledModeName() const;
    /// strip / strip_id / chase。width は 1..60。false = 名前または幅が不正
    bool setTestPattern(const char* pattern, int width);
    const char* testPatternName() const;
    uint8_t testWidth() const;
    void setAxisIndicator(bool on);            ///< NVS 保存
    bool axisIndicator() const;
    void setImuCompensation(bool on);
    /// pixels モードで [{index,r,g,b},...] を適用。戻り値 = 適用数
    uint16_t applyPixels(JsonArrayConst pixels);

    // --- IMU (実行時スイッチ) ---
    bool imuAvailable() const;
    bool setImuSmooth(int frames);             ///< 1..kSmoothMax。NVS 保存
    bool setImuI2cKhz(int khz);                ///< 50..400
    void setImuAux(bool on);
    void setImuWordRead(bool on);
    void imuReset();
    bool imuDump(int samples);                 ///< 1..2000
    void imuResetTiming();

    // --- ネットワーク / システム ---
    bool saveStaCredentials(const String& ssid, const String& password);
    /// server 接続 (config.json wifi.enabled) を切り替えて保存する。反映は再起動後
    bool setServerEnabled(bool enabled);
    bool serverConfigured();
    /// delayMs 後に再起動する (Settings は即時 flush)。応答を返す猶予を持たせるため予約制
    void scheduleReboot(uint32_t delayMs);
    bool rebootPending() const { return _rebootAtMs != 0; }

    /**
     * @brief MQTT の retained state (sphere/<id>/state) を組み立てる。派生元と同じ鍵。
     * @return false = バッファに収まらない (publish しない)
     */
    bool getStateJson(char* buffer, size_t bufferSize);

    // 依存へのアクセス (制御面が統計を読むため)
    SoloPlayer* player() const { return _d.player; }
    LEDManager* led() const { return _d.led; }
    IMUManager* imu() const { return _d.imu; }
    NetworkManager* net() const { return _d.net; }
    ConfigManager* config() const { return _d.config; }

private:
    void applyBrightnessToLed(uint8_t pct);

    Deps _d;
    uint8_t _brightness = 50;    ///< %
    uint8_t _speed = 50;
    uint16_t _hue = 120;
    uint8_t _saturation = 100;
    LedMode _ledMode = LedMode::Sphere;
    volatile uint32_t _rebootAtMs = 0;   ///< 0 = 予約なし

    /// mode:"test" 突入時に強制する低輝度 (0-255)。点灯/配線確認が目的で眩しさ・電流を抑える
    static constexpr uint8_t kTestBrightness = 24;
};

}  // namespace sastle

#endif  // __DEVICE_CONTROLLER_H__
