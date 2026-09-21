/**
 * @file NetworkManager.h
 * @brief SoftAP (アクセスポイント) 管理クラス
 * @author sastle-com
 *
 * solo 構成ではルーターや外部 server に接続しない。球体自身が SoftAP を立て、
 * iPhone が直接そこへ繋いで Web UI を開く。STA 接続・UDP 映像受信は持たない。
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
};

}  // namespace sastle

#endif  // __NETWORK_MANAGER_H__
