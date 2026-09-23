#include "MQTTManager.h"
#include "MqttTopics.h"
#include <Arduino.h>

namespace sastle {

void MQTTManager::_deviceTopic(const char* suffix, char* out, size_t len) {
    snprintf(out, len, "sphere/%s/%s", _clientId, suffix);
}

bool MQTTManager::publishDevice(const char* suffix, const char* payload, bool retained) {
    char topic[64];
    _deviceTopic(suffix, topic, sizeof(topic));
    return publish(topic, payload, retained);
}

MQTTManager::MQTTManager()
    : _mqttClient(_wifiClient),
      _port(1883),
      _initialized(false),
      _lastReconnectAttempt(0) {
    memset(_broker, 0, sizeof(_broker));
    memset(_clientId, 0, sizeof(_clientId));
}

MQTTManager::~MQTTManager() {
    if (_mqttClient.connected()) {
        _mqttClient.disconnect();
    }
}

bool MQTTManager::begin(ConfigManager& config) {
    Serial.println("\n=== MQTT Manager ===");

    const WiFiConfig wifiConfig = config.getWiFiConfig();
    const SphereConfig sphereConfig = config.getSphereConfig();

    if (wifiConfig.broker.length() == 0) {
        Serial.println("MQTT: broker not configured (wifi.broker) - disabled");
        return false;
    }
    strncpy(_broker, wifiConfig.broker.c_str(), sizeof(_broker) - 1);
    _port = (uint16_t)wifiConfig.mqtt_port;
    strncpy(_clientId, sphereConfig.id.c_str(), sizeof(_clientId) - 1);

    Serial.printf("Broker: %s:%u  Client ID: %s\n", _broker, (unsigned)_port, _clientId);

    _mqttClient.setServer(_broker, _port);
    _mqttClient.setKeepAlive(15);      // 15秒のキープアライブ
    _mqttClient.setSocketTimeout(5);   // 5秒のタイムアウト
    // 送受信で共用されるため、受信最大 (led コマンドの pixels 配列) に合わせる。
    // 1要素 約39B なので 2048B で概ね 50 LED/メッセージまで個別指定できる。
    _mqttClient.setBufferSize(kMqttBufferSize);

    _initialized = true;
    // 接続は loop() が STA 接続を確認してから行う (起動直後は STA がまだ繋がっていない)
    return true;
}

bool MQTTManager::connect() {
    if (!_initialized) {
        return false;
    }
    if (_mqttClient.connected()) {
        return true;
    }
    if (WiFi.status() != WL_CONNECTED) {
        return false;   // STA 未接続。loop() が繋がった後に再試行する
    }

    Serial.printf("[MQTT] connecting to %s:%u as %s...\n", _broker, (unsigned)_port, _clientId);
    if (!_mqttClient.connect(_clientId)) {
        Serial.printf("[MQTT] connection failed, rc=%d\n", _mqttClient.state());
        return false;
    }
    _connects++;
    Serial.println("[MQTT] connected");

    // 購読: 自機宛コマンド、全機宛コマンド、時刻ビーコン
    char topic[64];
    _deviceTopic("command/#", topic, sizeof(topic));
    subscribe(topic);
    subscribe(topics::kAllCommandWild);
    subscribe(topics::kAllClock);

    // ステータス (retained)。サーバーはこれと state を見て配信を始める
    _deviceTopic("status", topic, sizeof(topic));
    char statusPayload[128];
    snprintf(statusPayload, sizeof(statusPayload),
             "{\"status\":\"online\",\"uptime\":%lu,\"timestamp\":%lu}",
             millis() / 1000, millis());
    publish(topic, statusPayload, true);
    return true;
}

bool MQTTManager::_reconnect() {
    const unsigned long now = millis();
    if (now - _lastReconnectAttempt < kReconnectIntervalMs) {
        return false;
    }
    _lastReconnectAttempt = now;
    return connect();
}

bool MQTTManager::isConnected() {
    return _initialized && _mqttClient.connected();
}

void MQTTManager::disconnect() {
    if (_mqttClient.connected()) {
        char topic[64];
        _deviceTopic("status", topic, sizeof(topic));
        publish(topic, "offline", true);
        _mqttClient.disconnect();
        Serial.println("[MQTT] disconnected");
    }
}

void MQTTManager::loop() {
    if (!_initialized) {
        return;
    }
    const bool connected = _mqttClient.connected();
    if (connected != _wasConnected) {
        _wasConnected = connected;
        if (!connected) {
            Serial.printf("[MQTT] connection lost (rc=%d), retrying every %lus\n",
                          _mqttClient.state(), (unsigned long)(kReconnectIntervalMs / 1000));
        }
    }
    if (connected) {
        _mqttClient.loop();
    } else {
        _reconnect();
    }
}

bool MQTTManager::publish(const char* topic, const char* payload, bool retained) {
    if (!_mqttClient.connected()) {
        _publishSkipped++;   // 未接続時は静かに捨てる (10Hz の IMU publish でログを埋めない)
        return false;
    }
    const bool result = _mqttClient.publish(topic, payload, retained);
    if (!result) {
        Serial.printf("[MQTT] publish failed to %s\n", topic);
    }
    return result;
}

bool MQTTManager::subscribe(const char* topic) {
    if (!_mqttClient.connected()) {
        return false;
    }
    const bool result = _mqttClient.subscribe(topic);
    Serial.printf("[MQTT] %s: %s\n", result ? "subscribed" : "subscribe FAILED", topic);
    return result;
}

bool MQTTManager::unsubscribe(const char* topic) {
    if (!_mqttClient.connected()) {
        return false;
    }
    return _mqttClient.unsubscribe(topic);
}

void MQTTManager::setCallback(void (*callback)(char*, uint8_t*, unsigned int)) {
    _mqttClient.setCallback(callback);
}

void MQTTManager::printStatus() {
    Serial.println("\n=== MQTT Status ===");
    Serial.printf("Broker: %s:%u  Client ID: %s\n", _broker, (unsigned)_port, _clientId);
    Serial.printf("Connected: %s (rc=%d, connects=%lu, skipped_publish=%lu)\n",
                  _mqttClient.connected() ? "yes" : "no", _mqttClient.state(),
                  (unsigned long)_connects, (unsigned long)_publishSkipped);
}

} // namespace sastle
