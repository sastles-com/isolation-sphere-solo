/**
 * @file NetworkManager.h
 * @brief SoftAP (アクセスポイント) 管理クラス
 * @author sastle-com
 *
 * 基本は球体自身が SoftAP を立て、iPhone が直接そこへ繋いで Web UI を開く。
 * 加えて任意で STA (AP+STA 同時) を有効にできる。目的は開発時の OTA 書き込み:
 * STA で普段の LAN に居れば、PC の Wi-Fi を球体の AP へ切り替えずに espota できる。
 * STA の資格情報は NVS に置く (config.json に書くとリポジトリに混入するため)。
 */

#ifndef __NETWORK_MANAGER_H__
#define __NETWORK_MANAGER_H__

#include "common.h"
#include <Arduino.h>
#include <WiFi.h>
#include "ConfigManager.h"

namespace sastle {

class NetworkManager {
public:
    NetworkManager();
    virtual ~NetworkManager();

    /**
     * @brief SoftAP を起動する
     * @param ssid     AP の SSID
     * @param password WPA2 パスフレーズ (8文字未満なら暗号化なしのオープンAPで起動)
     * @param ip       AP 自身の IP (= ゲートウェイ)。サブネットは /24 固定
     * @return true 起動成功
     */
    bool beginSoftAP(const String& ssid, const String& password, const IPAddress& ip);

    /**
     * @brief NVS に資格情報があれば STA を併用して接続を開始する (非ブロッキング)
     * @param hostname DHCP / mDNS 用のホスト名
     * @return true STA 接続を開始した (接続完了は staConnected() で確認)
     * @note beginSoftAP の後に呼ぶ。ESP32 は AP と STA で無線を共有するため、
     *       AP のチャンネルは STA 側に追従する。
     */
    bool beginStaFromStore(const String& hostname);

    /// STA の資格情報を NVS に保存する (空 SSID で無効化)。次回起動から反映。
    static bool saveStaCredentials(const String& ssid, const String& password);
    /// NVS に保存された STA の SSID (無ければ空文字)
    static String storedStaSsid();

    /// 接続状態の変化をログに出す (loop から呼ぶ)
    void poll();

    bool staEnabled() const { return _staEnabled; }
    bool staConnected() const { return _staEnabled && WiFi.status() == WL_CONNECTED; }
    IPAddress staIP() const { return WiFi.localIP(); }
    String staSsid() const { return _staSsid; }

    /**
     * @brief SoftAP を停止する
     */
    void stop();

    bool isSoftAP() const { return _started; }

    /// AP 自身の IP アドレス
    IPAddress apIP() const { return WiFi.softAPIP(); }
    /// 接続中のクライアント台数
    uint8_t clientCount() const { return WiFi.softAPgetStationNum(); }

    void printStatus() const;

    /**
     * @brief Wi-Fi 接続用 QR コードの文字列を作る (iOS カメラ / Android 標準対応)
     * @param ssid     SSID
     * @param password パスフレーズ (8文字未満は beginSoftAP と同様にオープン扱い)
     * @return "WIFI:T:WPA;S:<ssid>;P:<pass>;;" または "WIFI:T:nopass;S:<ssid>;;"
     * @note 特殊文字 (\ ; , : ") はバックスラッシュでエスケープする。
     */
    static String wifiQrText(const String& ssid, const String& password);

private:
    bool _started;
    bool _staEnabled = false;
    bool _staWasConnected = false;
    String _staSsid;
};

}  // namespace sastle

#endif  // __NETWORK_MANAGER_H__
