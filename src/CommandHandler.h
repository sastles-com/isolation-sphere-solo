/**
 * @file CommandHandler.h
 * @brief MQTT コマンド (sphere/all|<id>/command/<type>) → DeviceController への薄いアダプタ
 * @author sastle-com
 *
 * 派生元 core の CommandHandler から移植。トピック解釈と JSON パースだけを持ち、
 * 実際の適用 (LED / 再生 / IMU / 永続化) は DeviceController に委譲する
 * (Web UI の /api/* と同じ経路を通るので、両方から操作しても状態が食い違わない)。
 * 処理するトピック: params / playback / led / system
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "DeviceController.h"

namespace sastle {

class CommandHandler {
public:
    bool begin(DeviceController* ctl);

    /**
     * @brief MQTTメッセージを処理
     * @param topic トピック名
     * @param payload ペイロード (JSON文字列)
     * @param length ペイロード長
     * @return true 処理成功, false 処理失敗
     */
    bool handleMessage(const char* topic, const uint8_t* payload, unsigned int length);

    /// retained state (sphere/<id>/state) の組み立て (DeviceController::getStateJson)
    bool getState(char* buffer, size_t bufferSize) { return _ctl ? _ctl->getStateJson(buffer, bufferSize) : false; }

private:
    bool _handleParams(const char* payload);
    bool _handlePlayback(const char* payload);
    bool _handleLed(const char* payload);
    bool _handleSystem(const char* payload);
    static const char* _extractCommandType(const char* topic);

    DeviceController* _ctl = nullptr;
};

} // namespace sastle
