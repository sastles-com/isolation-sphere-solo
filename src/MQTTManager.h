/**
 * @file MQTTManager.h
 * @brief MQTT通信管理クラス (派生元 core から移植)
 * @author sastle-com
 *
 * solo での変更点:
 *   - begin() は WiFi 接続を前提にしない。loop() が STA 接続を見て接続/再接続する
 *     (統合ファームでは STA は非ブロッキングで後から繋がる)
 *   - LogSink を実装し、RemoteLog の送信先になる (sphere/<id>/log)
 *   - 未接続時の publish は静かに失敗する (10Hz の IMU publish がログを埋めないため)
 *   - connect() で時刻ビーコン (sphere/all/clock) も購読する
 */

#pragma once

#include <WiFi.h>
#include <PubSubClient.h>
#include "ConfigManager.h"
#include "RemoteLog.h"

namespace sastle {

/// PubSubClient の送受信共用バッファサイズ。受信側の最大は led コマンドの
/// pixels 配列で決まる。メッセージ退避バッファ (main.cpp の mqttCallback) も
/// この値に合わせる必要があるため公開している。
constexpr size_t kMqttBufferSize = 2048;

class MQTTManager : public LogSink {
public:
    MQTTManager();
    ~MQTTManager();

    /**
     * @brief ブローカー/クライアント ID を設定する (接続は loop() が STA 接続後に行う)
     * @return false = broker 未設定
     */
    bool begin(ConfigManager& config);

    /// 接続を試みる (STA 未接続なら何もしない)
    bool connect();
    bool isConnected();
    /// 切断する (offline を retained で publish してから)
    void disconnect();

    /// keep-alive・受信処理・自動再接続 (メインループから呼ぶ)
    void loop();

    bool publish(const char* topic, const char* payload, bool retained = false);
    /// デバイス固有トピック "sphere/<id>/<suffix>" へパブリッシュ
    bool publishDevice(const char* suffix, const char* payload, bool retained = false);
    bool subscribe(const char* topic);
    bool unsubscribe(const char* topic);

    void setCallback(void (*callback)(char*, uint8_t*, unsigned int));

    // LogSink
    bool logConnected() override { return isConnected(); }
    bool publishLog(const char* suffix, const char* line) override { return publishDevice(suffix, line, false); }

    void printStatus();
    const char* getClientId() const { return _clientId; }
    const char* getBroker() const { return _broker; }
    uint16_t getPort() const { return _port; }
    bool isEnabled() const { return _initialized; }
    int state() { return _mqttClient.state(); }
    uint32_t connectCount() const { return _connects; }
    uint32_t publishSkipped() const { return _publishSkipped; }

private:
    WiFiClient _wifiClient;
    PubSubClient _mqttClient;

    char _broker[64];
    uint16_t _port;
    char _clientId[32];

    bool _initialized;
    bool _wasConnected = false;
    unsigned long _lastReconnectAttempt;
    static constexpr unsigned long kReconnectIntervalMs = 5000;
    uint32_t _connects = 0;
    uint32_t _publishSkipped = 0;

    bool _reconnect();
    void _deviceTopic(const char* suffix, char* out, size_t len);
};

} // namespace sastle
