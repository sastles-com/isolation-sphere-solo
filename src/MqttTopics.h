#ifndef __MQTT_TOPICS_H__
#define __MQTT_TOPICS_H__

// MQTT トピック定義の集約ヘッダー。
// サーバー側 (server/app/core/config.py) のトピック定義と対応している。
//
// コマンドの宛先は2系統ある (どちらも MQTTManager::connect() で購読):
//   sphere/all/command/<type>   ... 全 core 宛のブロードキャスト (操作対象=ALL)
//   sphere/<id>/command/<type>  ... 自機宛 (WebUI で操作対象 core を選んだ場合)
// <id> は config.json の spheres[] から自機 MAC で解決した sphere.id。
// ID をハードコードしないこと (ConfigManager::getSphereConfig 参照)。

namespace sastle {
namespace topics {

// 購読 (サーバー → デバイス)
constexpr const char* kAllClock          = "sphere/all/clock";   // 時刻同期ビーコン (1秒周期)
constexpr const char* kAllCommandWild    = "sphere/all/command/#";
constexpr const char* kCommandParams     = "sphere/all/command/params";
constexpr const char* kCommandPlayback   = "sphere/all/command/playback";
constexpr const char* kCommandLed        = "sphere/all/command/led";
constexpr const char* kCommandSystem     = "sphere/all/command/system";

// 配信 (デバイス → サーバー) と自機宛コマンドはデバイス ID を含むため定数にしない。
// MQTTManager::publishDevice("<suffix>", ...) が config.json の sphere.id から
// "sphere/<id>/<suffix>" を実行時生成する。使用中の suffix:
//   imu / state / gesture / ui_mode / status / log
// サーバー側 (server/app/services/mqtt_service.py) も sphere.id を読んで対応する。

} // namespace topics
} // namespace sastle

#endif /* __MQTT_TOPICS_H__ */
