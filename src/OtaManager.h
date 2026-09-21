/**
 * @file OtaManager.h
 * @brief WiFi 経由 (espota / ArduinoOTA) のファームウェア無線更新
 * @author sastle-com
 *
 * ボール封止後など USB が届かない状態でも、P2P 接続先 (GMKTec) から
 *   pio run -e atoms3r_ota -t upload
 * でファーム/LittleFS を無線更新できるようにする。
 * OTA セッション開始時に LED レンダリングタスクを停止し、転送完了まで
 * handle() がブロックするため描画・配信は自然に停止する (要件どおり)。
 */

#ifndef __OTA_MANAGER_H__
#define __OTA_MANAGER_H__

#include <Arduino.h>

namespace sastle {

class LEDManager;  // 前方宣言

class OtaManager {
public:
    /**
     * @brief OTA を初期化する (WiFi 接続確立後に呼ぶ)
     * @param led OTA 開始時にレンダリングタスクを止めるための参照 (nullptr 可)
     * @return true 初期化成功
     */
    bool begin(LEDManager* led);

    /**
     * @brief OTA 要求を処理する (メインループから毎回呼ぶ)
     */
    void handle();

    bool isStarted() const { return _started; }

private:
    bool _started = false;
    LEDManager* _led = nullptr;
};

} // namespace sastle

#endif /* __OTA_MANAGER_H__ */
