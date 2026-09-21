/**
 * @file SoloWebServer.h
 * @brief soloモード用のデバイス内 HTTP サーバー (最小Web UI + 制御API + 動画アップロード)
 *
 * ESP-IDF 同梱の esp_http_server を使う (追加ライブラリ依存なし)。UI の HTML/JS は
 * ファームウェアに埋め込み、CDN や外部フォントには依存しない。LittleFS が壊れて
 * いても管理 UI を出せるようにするため、UI を LittleFS には置かない。
 *
 * API (すべて JSON 応答):
 *   GET  /                    Web UI
 *   GET  /api/status          状態・統計・容量
 *   POST /api/play            再生
 *   POST /api/stop            停止
 *   POST /api/brightness      {"value":0-100}
 *   POST /api/video           動画本体 (application/octet-stream, raw MJPEG)
 *   POST /api/video/delete    保存済み動画の削除
 *   POST /api/reboot          再起動
 */

#ifndef __SOLO_WEB_SERVER_H__
#define __SOLO_WEB_SERVER_H__

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <esp_http_server.h>

#include "ConfigManager.h"
#include "LEDManager.h"
#include "SoloPlayer.h"

namespace sastle {

class SoloWebServer {
public:
    SoloWebServer();
    ~SoloWebServer();

    /**
     * @brief HTTP サーバーを起動する
     * @param config  設定 (モード永続化に使う)
     * @param player  再生制御
     * @param led     明るさ制御
     * @param port    リッスンポート
     */
    bool begin(ConfigManager& config, SoloPlayer& player, LEDManager& led, uint16_t port);
    void end();
    bool isRunning() const { return _server != nullptr; }

    /**
     * @brief loop() から呼ぶ。キャプティブ DNS の応答と、再起動要求の実行。
     */
    void loop();

private:
    // esp_http_server ハンドラ (user_ctx = this)
    static esp_err_t onRoot(httpd_req_t* req);
    static esp_err_t onStatus(httpd_req_t* req);
    static esp_err_t onPlay(httpd_req_t* req);
    static esp_err_t onStop(httpd_req_t* req);
    static esp_err_t onBrightness(httpd_req_t* req);
    static esp_err_t onUpload(httpd_req_t* req);
    static esp_err_t onDelete(httpd_req_t* req);
    static esp_err_t onReboot(httpd_req_t* req);
    static esp_err_t onNotFound(httpd_req_t* req, httpd_err_code_t err);

    esp_err_t doUpload(httpd_req_t* req);
    esp_err_t sendJson(httpd_req_t* req, const char* status, const char* json);
    esp_err_t sendError(httpd_req_t* req, const char* status, const char* message);
    bool readBody(httpd_req_t* req, char* out, size_t cap, size_t& len);
    void applyBrightness(uint8_t percent);
    void scheduleReboot(uint32_t delayMs);
    size_t maxUploadBytes(size_t& freeOut, size_t& existingOut) const;

    httpd_handle_t _server;
    // キャプティブポータル: 全ドメインを AP 自身の IP に解決し、未知パスは "/" へ 302。
    // iPhone は AP 接続直後に captive.apple.com を叩くので、そのまま UI が自動で開く。
    DNSServer _dns;
    bool _dnsStarted = false;
    ConfigManager* _config;
    SoloPlayer* _player;
    LEDManager* _led;

    uint8_t* _rxBuf;              ///< 受信作業バッファ (ヒープ)
    char _jsonBuf[1024];          ///< 応答組み立て (ハンドラは httpd タスクで直列実行)
    uint8_t _brightnessPct;
    volatile uint32_t _rebootAtMs;  ///< 0 = 予約なし
    uint32_t _uploads;
    uint32_t _uploadFailures;
};

}  // namespace sastle

#endif  // __SOLO_WEB_SERVER_H__
