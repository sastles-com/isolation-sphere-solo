#include "OtaManager.h"
#include "Log.h"
#include "LEDManager.h"
#include "SoloPlayer.h"

#include <ArduinoOTA.h>

namespace sastle {

namespace {
// OTA のホスト名とパスワード。
// espota は IP 指定 (upload_port=192.168.49.101) で書き込むため、
// ホスト名は mDNS 上の識別用。パスワードは upload_flags=--auth と一致させる。
constexpr const char* kOtaHostname = "isolation-sphere";
constexpr const char* kOtaPassword = "isolation-sphere-ota";
}

bool OtaManager::begin(LEDManager* led, SoloPlayer* player) {
    _led = led;
    _player = player;

    ArduinoOTA.setHostname(kOtaHostname);
    ArduinoOTA.setPassword(kOtaPassword);

    ArduinoOTA.onStart([this]() {
        const bool isFs = (ArduinoOTA.getCommand() == U_SPIFFS);
        Log.printf("\n[OTA] Start: %s update\n", isFs ? "filesystem" : "firmware");
        // 再生を先に止める。再生中は Core0 が 100ms 中 60ms をデコードに使い、
        // LittleFS も読み続けるため、OTA の受信とフラッシュ書き込みが間に合わない
        // (実機で転送 0% のまま失敗した)。
        if (_player) {
            _player->stop();
        }
        // 描画タスクを止めて Core1 とフラッシュ操作ロックを解放する。
        // stopRenderTask() は協調停止 (show() の途中で殺さない) であること。
        if (_led) {
            _led->stopRenderTask();
            _led->fillSolid(0, 0, 0);
            _led->show();
        }
    });

    ArduinoOTA.onEnd([]() {
        Log.println("[OTA] Complete - rebooting");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static int lastPct = -1;
        int pct = total ? (int)((progress * 100UL) / total) : 0;
        if (pct != lastPct && pct % 10 == 0) {
            lastPct = pct;
            Log.printf("[OTA] Progress: %d%%\n", pct);
        }
    });

    ArduinoOTA.onError([](ota_error_t error) {
        const char* msg = "Unknown";
        switch (error) {
            case OTA_AUTH_ERROR:    msg = "Auth failed";    break;
            case OTA_BEGIN_ERROR:   msg = "Begin failed";   break;
            case OTA_CONNECT_ERROR: msg = "Connect failed"; break;
            case OTA_RECEIVE_ERROR: msg = "Receive failed"; break;
            case OTA_END_ERROR:     msg = "End failed";     break;
        }
        Log.printf("[OTA] Error[%u]: %s\n", error, msg);
    });

    ArduinoOTA.begin();
    _started = true;
    Log.printf("[OTA] Ready (hostname=%s, port=3232)\n", kOtaHostname);
    return true;
}

void OtaManager::handle() {
    if (_started) {
        ArduinoOTA.handle();
    }
}

} // namespace sastle
