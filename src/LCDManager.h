/**
 * @file LCDManager.h
 * @brief LCD表示管理クラス
 * 
 * M5AtomS3の内蔵LCD(128x128)に受信画像を表示する
 * ConfigManagerのLCDデバッグフラグで表示ON/OFFを制御
 */

#ifndef LCDMANAGER_H
#define LCDMANAGER_H

#include <Arduino.h>
#include "BoardConfig.h"
#if BOARD_HAS_LCD
#include <M5Unified.h>
#else
// LCD非搭載ボード: 関数シグネチャ維持のための色定数フォールバック (RGB565)
#ifndef TFT_WHITE
#define TFT_WHITE 0xFFFF
#endif
#ifndef TFT_BLACK
#define TFT_BLACK 0x0000
#endif
#ifndef TFT_GREEN
#define TFT_GREEN 0x07E0
#endif
#endif
#include "ConfigManager.h"
#include "ImageManager.h"

namespace sastle {

/**
 * @struct LcdStatus
 * @brief アイドル/無信号時にLCDへ表示する device 生存情報
 *
 * device 自前のライブ値(増え続ける uptime / 動く IMU)を表示することで
 * 「映像は来ていないが device は生きている」ことを示す。
 */
struct LcdStatus {
    uint32_t uptime_s;   ///< 起動からの経過秒 (増え続ける=生存証明)
    const char* ip;      ///< IPアドレス文字列
    uint8_t clients;     ///< SoftAP に接続中の端末数
    uint32_t free_heap;  ///< 空きヒープ [bytes]
    float fps;           ///< 描画FPS
    float qw, qx, qy, qz;///< IMUクォータニオン
    bool imu_ok;         ///< IMU有効
};

/**
 * @class LCDManager
 * @brief LCD表示管理
 *
 * sphere.features.LCD.debugフラグでデバッグ表示を制御
 * ON時は受信した画像をLCDに表示
 */
class LCDManager {
public:
    /**
     * @brief コンストラクタ
     */
    LCDManager();
    
    /**
     * @brief デストラクタ
     */
    ~LCDManager();
    
    /**
     * @brief LCD初期化
     * @param config ConfigManagerインスタンス
     * @return true 初期化成功, false 失敗
     */
    bool begin(ConfigManager* config);
    
    /**
     * @brief LCD表示更新
     * @param imageManager 画像データソース
     * 
     * getLCDDebugEnabled()がtrueの場合のみ描画を実行
     */
    void update(ImageManager* imageManager);
    
    /**
     * @brief デバッグテキスト表示
     * @param text 表示するテキスト
     * @param x X座標
     * @param y Y座標
     * @param textSize テキストサイズ (1-7)
     * @param color 色 (RGB565)
     */
    void drawText(const char* text, int16_t x, int16_t y, uint8_t textSize = 2, uint16_t color = TFT_WHITE);

    /**
     * @brief アイドル/無信号時のステータス画面を描画
     * @param s 表示する device 生存情報
     *
     * 映像が一定時間届かない時に呼ぶ。内部で ~3Hz にスロットルし、
     * ハートビート点滅 + uptime/IP/RSSI/heap/fps/IMU を sprite で描画(ちらつき防止)。
     */
    void drawStatus(const LcdStatus& s);

    /**
     * @brief solo モード: iPhone を SoftAP に接続させるための Wi-Fi QR を描画
     * @param wifiQrText  "WIFI:T:WPA;S:...;P:...;;" 形式の文字列 (カメラアプリで読むと接続候補が出る)
     * @param ssid        画面下部に表示する SSID
     * @param url         画面下部に表示する URL
     *
     * 端末が1台も接続していない間に呼ぶ。内部で ~2Hz にスロットルし、QR は sprite に
     * 描いて一括転送する。文字列長に応じて QR バージョンを選ぶ。
     */
    void drawWifiQr(const char* wifiQrText, const char* ssid, const char* url);

    /**
     * @brief 画面クリア
     * @param color 背景色 (RGB565)
     */
    void clear(uint16_t color = TFT_BLACK);
    
    /**
     * @brief デバッグ表示が有効かチェック
     * @return true 有効, false 無効
     */
    bool isDebugEnabled() const { return _debugEnabled; }

private:
    ConfigManager* _config;      ///< ConfigManager参照
    bool _debugEnabled;          ///< デバッグ表示フラグ
    bool _initialized;           ///< 初期化完了フラグ
    
    uint16_t _lcdWidth;          ///< LCD幅
    uint16_t _lcdHeight;         ///< LCD高さ
    uint8_t _rotation;           ///< LCD回転角度 (0/1/2/3)
    uint16_t* _lcdBuf = nullptr; ///< 一括転送用フレームバッファ (RGB565)
    unsigned long _lastUpdateMs = 0;  ///< 最終LCD更新時刻 (スロットル用)
    unsigned long _lastStatusMs = 0;  ///< 最終ステータス描画時刻 (スロットル用)
    bool _heartbeat = false;          ///< ステータス画面のハートビート点滅状態
#if BOARD_HAS_LCD
    M5Canvas* _statusCanvas = nullptr; ///< ステータス画面用スプライト (遅延生成)
#endif
};

} // namespace sastle

#endif // LCDMANAGER_H
