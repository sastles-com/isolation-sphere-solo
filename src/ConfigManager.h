/**
 * @file ConfigManager.h
 * @brief JSON設定ファイル管理クラス
 * @author sastle-com
 * @date 2025-12-01
 */

#ifndef __CONFIG_MANAGER_H__
#define __CONFIG_MANAGER_H__

#include "common.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include "FileManager.h"

namespace sastle {

/**
 * @struct SystemConfig
 * @brief システム基本設定
 */
struct SystemConfig {
    bool debug;          ///< デバッグモード有効化
    bool PSRAM;          ///< PSRAM使用フラグ
};

/**
 * @struct OTAConfig
 * @brief OTA (Over-The-Air) アップデート設定
 */
struct OTAConfig {
    bool enabled;        ///< OTA有効化
    String username;     ///< OTA認証ユーザー名
    String password;     ///< OTA認証パスワード
    int listen_port;     ///< OTAリッスンポート
};

/**
 * @struct PathsConfig
 * @brief ファイルシステムパス設定
 */
struct PathsConfig {
    String config;       ///< 設定ファイルパス
    String layout;       ///< LEDレイアウトファイルパス
    String logs;         ///< ログディレクトリパス
};

/**
 * @struct SoloConfig
 * @brief solo 動作設定 (SoftAP / Web UI / 動画ファイル)
 */
struct SoloConfig {
    String video_path;   ///< 再生する raw MJPEG のパス
    uint16_t http_port;  ///< Web UI のポート
    String ap_ssid;      ///< SoftAP の SSID
    String ap_password;  ///< SoftAP のパスフレーズ (8文字未満はオープンAP)
    String ap_ip;        ///< SoftAP 自身の IP
};

/**
 * @struct ImageConfig
 * @brief 画像設定
 */
struct ImageConfig {
    int width;           ///< 画像幅
    int height;          ///< 画像高さ
    String format;       ///< 画像フォーマット (例: RGB565)
    String type;         ///< 画像タイプ (例: JPEG)
};

/**
 * @struct LCDConfig
 * @brief LCDディスプレイ設定
 */
struct LCDConfig {
    int width;           ///< LCD幅
    int height;          ///< LCD高さ
    int rotation;        ///< 画面回転角度
    int offset[2];       ///< 表示オフセット [x, y]
    int color_depth;     ///< 色深度 (ビット)
    bool switch_enabled; ///< LCD切替有効化
    bool debug;          ///< LCDデバッグ表示
};

/**
 * @struct SphereConfig
 * @brief 球体デバイス固有設定
 */
struct SphereConfig {
    String id;           ///< デバイスID
    bool LED_enabled;    ///< LED制御有効化
    String IMU_type;     ///< IMUセンサータイプ (例: BNO055)
    LCDConfig lcd;       ///< LCD設定
};

/**
 * @class ConfigManager
 * @brief JSON設定ファイルの読み込み・管理クラス
 * 
 * LittleFSから設定ファイル(config.json)を読み込み、
 * 構造化されたデータとしてアクセスを提供します。
 */
class ConfigManager {
public:
    ConfigManager();
    virtual ~ConfigManager();

    /**
     * @brief 設定ファイルをロード
     * @param path 設定ファイルパス (デフォルト: "/config.json")
     * @return true ロード成功, false ロード失敗
     */
    bool loadConfig(const char* path = "/config.json");
    
    /**
     * @brief 設定ファイルを保存
     * @param path 設定ファイルパス (デフォルト: "/config.json")
     * @return true 保存成功, false 保存失敗
     */
    bool saveConfig(const char* path = "/config.json");
    
    /**
     * @brief JSONドキュメントを取得
     * @return DynamicJsonDocument参照
     */
    DynamicJsonDocument& getDocument() { return doc; }
    
    /**
     * @brief システム設定を取得
     * @return SystemConfig構造体
     */
    SystemConfig getSystemConfig();
    
    /**
     * @brief OTA設定を取得
     * @return OTAConfig構造体
     */
    OTAConfig getOTAConfig();
    
    /**
     * @brief パス設定を取得
     * @return PathsConfig構造体
     */
    PathsConfig getPathsConfig();
    
    /**
     * @brief solo 設定を取得
     * @return SoloConfig構造体
     */
    SoloConfig getSoloConfig();
    
    /**
     * @brief 画像設定を取得
     * @return ImageConfig構造体
     */
    ImageConfig getImageConfig();
    
    /**
     * @brief 球体設定を取得
     * @return SphereConfig構造体
     */
    SphereConfig getSphereConfig();
    
    /**
     * @brief PSRAM有効状態を取得
     * @return true 有効, false 無効
     */
    bool isPSRAMEnabled() { return doc["system"]["PSRAM"] | false; }
    
    /**
     * @brief デバッグモード状態を取得
     * @return true 有効, false 無効
     */
    bool isDebugEnabled() { return doc["system"]["debug"] | false; }
    
    String getSphereID() { return doc["sphere"]["id"] | "sphere001"; }
    
    bool isLEDEnabled() { return doc["sphere"]["features"]["LED"] | false; }
    String getIMUType() { return doc["sphere"]["features"]["IMU"] | ""; }
    
    String getLayoutPath() { return doc["system"]["paths"]["layout"] | "/led_layouts-5strip.csv"; }

    // 起動オープニングパターン (LEDManager::playOpening)。スキップは enabled=false。
    /// true = 接続時に OS の「ログイン」画面 (Captive Network Assistant) を開かせる。
    /// 既定 false: iOS の CNA はファイル選択ダイアログが出ないため動画をアップロードできない。
    /// false のときは OS の検出プローブに期待どおりの応答を返し、CNA を開かせない。
    bool getSoloCaptivePortal() { return doc["solo"]["captive_portal"] | false; }
    bool getOpeningActionEnabled() { return doc["system"]["opening_action"]["enabled"] | true; }
    uint16_t getOpeningActionDurationMs() { return doc["system"]["opening_action"]["duration_ms"] | 1200; }
    
    int getImageWidth() { return doc["image"]["width"] | 320; }
    int getImageHeight() { return doc["image"]["height"] | 160; }
    String getImageFormat() { return doc["image"]["format"] | "RGB565"; }
    String getImageType() { return doc["image"]["type"] | "JPEG"; }
    
    /**
     * @brief LCD表示デバッグモードを取得
     * @return true デバッグ表示有効, false 無効
     */
    bool getLCDDebugEnabled() { return doc["sphere"]["features"]["LCD"]["debug"] | false; }

    // params ブロック = 表示パラメータの起動時デフォルト。
    // 明るさは Web UI (POST /api/brightness) で実行時に上書きされる (再起動で戻る)。
    uint8_t getParamBrightness() { return doc["params"]["brightness"] | 50; }

    // LEDカラーのマルチサンプリング (中心+半径R円周上N点を画像空間で平均)。
    // 実機でちらつき低減↔ボケ/負荷のトレードオフを調整するための可変パラメータ。
    bool getLedMultisampleEnabled() { return doc["led"]["multisample"]["enabled"] | true; }
    float getLedMultisampleRadius() { return doc["led"]["multisample"]["radius_px"] | 2.0f; }
    uint8_t getLedMultisamplePoints() { return doc["led"]["multisample"]["points"] | 6; }

    // solo 設定 (未設定時は右側のデフォルト)
    String getSoloVideoPath() { return doc["solo"]["video_path"] | "/video.mjpg"; }
    uint16_t getSoloHttpPort() { return doc["solo"]["http_port"] | 80; }
    String getSoloApSsid() { return doc["solo"]["ap"]["ssid"] | "isolation-sphere"; }
    String getSoloApPassword() { return doc["solo"]["ap"]["password"] | "sphere-solo"; }
    String getSoloApIp() { return doc["solo"]["ap"]["ip"] | "192.168.4.1"; }

    // デバッグ出力
    void printConfig();
    
private:
    DynamicJsonDocument doc{8192};  // 8KB buffer for config
    bool parseJSON(const String& jsonStr);
};

} // namespace sastle

#endif // __CONFIG_MANAGER_H__
