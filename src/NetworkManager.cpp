#include "NetworkManager.h"

#include <Preferences.h>

namespace {
// STA 資格情報の保管先 (NVS)。config.json に書かないのは、リポジトリに
// 自宅 Wi-Fi のパスワードが混入するのを避けるため。
constexpr const char* kNvsNamespace = "solo";
constexpr const char* kKeySsid = "sta_ssid";
constexpr const char* kKeyPass = "sta_pass";
}  // namespace

namespace sastle {

NetworkManager::NetworkManager() : _started(false) {}

NetworkManager::~NetworkManager() {
    stop();
}

bool NetworkManager::beginSoftAP(const String& ssid, const String& password, const IPAddress& ip) {
    Serial.println("\n=== Network Manager: SoftAP ===");

    if (ssid.length() == 0) {
        Serial.println("ERROR: SoftAP SSID is empty");
        return false;
    }

    // STA 資格情報があれば最初から AP+STA で立ち上げる (後から mode を変えると AP が落ちる)
    WiFi.mode(storedStaSsid().length() > 0 ? WIFI_AP_STA : WIFI_AP);
    // モデム省電力を無効化。省電力中はビーコン間でスリープし、AP としての応答が
    // 遅れて Web UI の操作感が落ちる。
    WiFi.setSleep(false);

    const IPAddress subnet(255, 255, 255, 0);
    if (!WiFi.softAPConfig(ip, ip, subnet)) {
        Serial.println("ERROR: softAPConfig failed");
        return false;
    }

    // WPA2 は 8 文字以上のパスフレーズが必須。短い場合は起動に失敗するので
    // オープン AP にフォールバックし、その旨を明示する。
    const bool open = password.length() < 8;
    if (open && password.length() > 0) {
        Serial.println("WARN: AP password shorter than 8 chars -> starting as OPEN network");
    }
    if (!WiFi.softAP(ssid.c_str(), open ? nullptr : password.c_str())) {
        Serial.println("ERROR: softAP start failed");
        return false;
    }

    _started = true;
    Serial.printf("SoftAP started: SSID=%s (%s)\n", ssid.c_str(), open ? "open" : "WPA2");
    Serial.printf("  AP IP:  %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("  AP MAC: %s\n", WiFi.softAPmacAddress().c_str());
    return true;
}

bool NetworkManager::saveStaCredentials(const String& ssid, const String& password) {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, false)) {
        return false;
    }
    bool ok;
    if (ssid.length() == 0) {
        prefs.remove(kKeySsid);
        prefs.remove(kKeyPass);
        ok = true;
    } else {
        ok = prefs.putString(kKeySsid, ssid) > 0;
        prefs.putString(kKeyPass, password);  // 空パスワード (オープン AP) も許容
    }
    prefs.end();
    return ok;
}

String NetworkManager::storedStaSsid() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, true)) {
        return String();
    }
    String ssid = prefs.getString(kKeySsid, "");
    prefs.end();
    return ssid;
}

bool NetworkManager::beginStaFromStore(const String& hostname) {
    Preferences prefs;
    String ssid, pass;
    if (prefs.begin(kNvsNamespace, true)) {
        ssid = prefs.getString(kKeySsid, "");
        pass = prefs.getString(kKeyPass, "");
        prefs.end();
    }
    if (ssid.length() == 0) {
        Serial.println("STA: not configured (POST /api/wifi to enable OTA over LAN)");
        return false;
    }
    _staSsid = ssid;
    _staEnabled = true;
    if (hostname.length() > 0) {
        WiFi.setHostname(hostname.c_str());
    }
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);
    Serial.printf("STA: connecting to \"%s\" (non-blocking; AP stays up)\n", ssid.c_str());
    return true;
}

void NetworkManager::poll() {
    if (!_staEnabled) {
        return;
    }
    const bool now = (WiFi.status() == WL_CONNECTED);
    if (now == _staWasConnected) {
        return;
    }
    _staWasConnected = now;
    if (now) {
        Serial.printf("STA: connected to \"%s\"  IP=%s  (OTA: pio run -e atoms3r_lan_ota -t upload)\n",
                      _staSsid.c_str(), WiFi.localIP().toString().c_str());
    } else {
        Serial.printf("STA: disconnected from \"%s\" (auto-reconnect enabled)\n", _staSsid.c_str());
    }
}

void NetworkManager::stop() {
    if (_started) {
        WiFi.softAPdisconnect(true);
        _started = false;
        Serial.println("SoftAP stopped");
    }
}

void NetworkManager::printStatus() const {
    Serial.println("\n=== Network Status ===");
    Serial.printf("SoftAP:  %s\n", _started ? "running" : "stopped");
    if (_started) {
        Serial.printf("SSID:    %s\n", WiFi.softAPSSID().c_str());
        Serial.printf("IP:      %s\n", WiFi.softAPIP().toString().c_str());
        Serial.printf("Clients: %u\n", (unsigned)WiFi.softAPgetStationNum());
    }
}

String NetworkManager::wifiQrText(const String& ssid, const String& password) {
    auto escape = [](const String& in) {
        String out;
        out.reserve(in.length() + 4);
        for (size_t i = 0; i < in.length(); i++) {
            const char c = in[i];
            if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') {
                out += '\\';
            }
            out += c;
        }
        return out;
    };
    String qr = "WIFI:";
    if (password.length() < 8) {
        qr += "T:nopass;S:" + escape(ssid) + ";;";
    } else {
        qr += "T:WPA;S:" + escape(ssid) + ";P:" + escape(password) + ";;";
    }
    return qr;
}

}  // namespace sastle
