#include "OtaManager.h"
#include "Log.h"
#include "LEDManager.h"
#include "SoloPlayer.h"
#include "FramePump.h"

#include <ArduinoOTA.h>

namespace sastle {

namespace {
// OTA のパスワード。upload_flags=--auth と一致させる。
constexpr const char* kOtaPassword = "isolation-sphere-ota";
constexpr const char* kOtaDefaultHostname = "isolation-sphere";
}

bool OtaManager::begin(LEDManager* led, FramePump* pump, SoloPlayer* player, const char* hostname) {
    _led = led;
    _pump = pump;
    _player = player;

    const char* host = (hostname && hostname[0]) ? hostname : kOtaDefaultHostname;
    ArduinoOTA.setHostname(host);
    ArduinoOTA.setPassword(kOtaPassword);

    ArduinoOTA.onStart([this]() {
        const bool isFs = (ArduinoOTA.getCommand() == U_SPIFFS);
        Log.printf("\n[OTA] Start: %s update\n", isFs ? "filesystem" : "firmware");
        // 供給を先に止める。再生中は Core0 が 100ms 中 60ms をデコードに使い、LittleFS も
        // 読み続けるため、OTA の受信とフラッシュ書き込みが間に合わない (実機で転送 0% のまま
        // 失敗した)。UDP 配信中も同様なので受信器も閉じる (server は OTA 中も送り続ける)。
        if (_player) {
            _player->stop();
        }
        if (_pump) {
            _pump->stopForOta();
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
    Log.printf("[OTA] Ready (hostname=%s, port=3232)\n", host);
    return true;
}

void OtaManager::handle() {
    if (_started) {
        ArduinoOTA.handle();
    }
}

} // namespace sastle
