/**
 * @file SoloPlayer.h
 * @brief ローカル動画再生 (LittleFS raw MJPEG → ImageManager)
 *
 * フレームの供給は FramePump (単一タスク) が担う。本クラスはファイルの状態管理と、
 * FramePump から 100ms 締切ごとに呼ばれる tick() で 1 フレーム読んで
 * ImageManager::submitJpegFrame() に渡すだけ。UDP 配信が生きている間は FramePump が
 * tick() を呼ばない (ファイル位置は保持され、配信が途切れると続きから再開する)。
 * 描画 (LEDManager レンダタスク / IMU 再マッピング) は供給元を区別しないので共用。
 */

#ifndef __SOLO_PLAYER_H__
#define __SOLO_PLAYER_H__

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "ConfigManager.h"
#include "ImageManager.h"
#include "MjpegReader.h"

namespace sastle {

/// 再生フレームレート (固定値。ファイル側の fps メタデータは参照しない)
constexpr uint32_t kSoloFps = 10;
/// 1フレームの上限バイト数。UDP経路の再構成バッファ (65507B) と同等。
constexpr size_t kSoloMaxFrameBytes = 65536;

/**
 * @class SoloPlayer
 * @brief LittleFS 上の raw MJPEG を固定 fps でループ再生する
 *
 * 状態遷移:
 *   NoVideo   : 有効な動画ファイルが無い (UI からアップロード可)
 *   Playing   : 再生中 (起動時に有効な動画があれば自動でここへ)
 *   Paused    : 一時停止 (現在のフレームを表示したまま止まる。play で続きから)
 *   Stopped   : 停止 (LED は消灯。play で続きから再開)
 *   Uploading : アップロード受信中 (再生停止・ファイルクローズ)
 *   Error     : 動画ファイルが壊れている等 (UI から再アップロードで復旧)
 *
 * 消灯は ImageManager::requestBlack() で要求し、FramePump が (配信が表示を握っていない
 * ときに) 実行する。httpd タスクから直接デコードバッファに触らないため。
 */
class SoloPlayer {
public:
    enum class State : uint8_t { NoVideo, Playing, Paused, Stopped, Uploading, Error };

    struct Stats {
        uint32_t frames;          ///< 公開したフレーム数
        uint32_t loops;           ///< ファイル先頭へ巻き戻した回数
        uint32_t deadlineMisses;  ///< 100ms 締切に間に合わずスキップした回数
        uint32_t decodeErrors;    ///< submitJpegFrame 失敗数
        uint32_t lastFrameBytes;  ///< 最後に読んだフレームのサイズ
        uint32_t lastReadUs;      ///< 最後のファイル読み出し所要時間
        uint32_t lastTickUs;      ///< 最後の read+decode 合計所要時間
        float fps;                ///< 実効フレームレート
    };

    SoloPlayer();
    ~SoloPlayer();

    /**
     * @brief 初期化。PSRAM にフレームバッファを確保し、動画ファイルを開く。
     * @return true 初期化成功 (動画の有無は問わない)
     */
    bool begin(ConfigManager& config, ImageManager& image);

    /**
     * @brief 1 フレーム読んでデコードに渡す (FramePump が締切ごとに呼ぶ。Playing 以外は何もしない)
     * @warning TJpg_Decoder は単一インスタンス。FramePump タスク以外から呼ばないこと。
     */
    void tick();
    /// FramePump が締切超過を検出したときに加算する
    void addDeadlineMisses(uint32_t n) { _deadlineMisses += n; }

    void play();
    void stop();
    /// 一時停止: フレーム送りだけ止め、表示は現在のフレームのまま
    void pause();

    /**
     * @brief 動画ファイルを再オープンして検証する。有効なら再生を開始する。
     * @return true 有効な動画を開けた
     */
    bool reload();

    /**
     * @brief アップロード開始: 再生を止めてファイルを閉じ、Uploading にする
     * @return false 既に Uploading 中
     */
    bool beginUpload();

    /**
     * @brief アップロード終了: reload() して状態を復帰する
     */
    void endUpload();

    /**
     * @brief アップロード済みファイルを検証する (Uploading 中のみ。内部バッファを走査に使う)
     */
    bool validateFile(const char* path, MjpegInfo& info, const char** errorOut);

    State state() const { return _state; }
    const char* stateName() const;
    const char* lastError() const { return _lastError; }
    bool hasVideo() const { return _videoFrames > 0; }
    size_t videoBytes() const { return _videoBytes; }
    uint32_t videoFrames() const { return _videoFrames; }
    const String& videoPath() const { return _videoPath; }
    uint16_t width() const { return _width; }
    uint16_t height() const { return _height; }
    size_t maxFrameBytes() const { return _frameCap; }
    /// 動画を PSRAM から再生しているか (false = PSRAM 確保失敗でフラッシュ直読み)
    bool playsFromMemory() const { return _reader.fromMemory(); }
    /// 直近の PSRAM 読み込みにかかった時間 [ms]
    uint32_t loadMs() const { return _loadMs; }
    Stats stats() const;

private:
    // 以下 *Locked は _mutex 取得済みで呼ぶ
    bool openVideoLocked();
    void closeVideoLocked();
    void setErrorLocked(const char* msg);

    ConfigManager* _config;
    ImageManager* _image;

    String _videoPath;
    uint16_t _width;
    uint16_t _height;

    uint8_t* _frameBuf;   ///< PSRAM 上のフレーム組み立てバッファ
    size_t _frameCap;
    uint8_t* _videoMem = nullptr;   ///< 動画全体の PSRAM コピー (再生中にフラッシュを読まない)
    size_t _videoMemCap = 0;
    uint32_t _loadMs = 0;
    MjpegReader _reader;

    SemaphoreHandle_t _mutex;

    volatile State _state;
    const char* _lastError;
    size_t _videoBytes;
    uint32_t _videoFrames;

    // 統計
    volatile uint32_t _frames;
    volatile uint32_t _loops;
    volatile uint32_t _deadlineMisses;
    volatile uint32_t _decodeErrors;
    volatile uint32_t _lastFrameBytes;
    volatile uint32_t _lastReadUs;
    volatile uint32_t _lastTickUs;
    volatile float _fps;
    uint32_t _fpsCount;
    unsigned long _fpsTimestamp;
};

}  // namespace sastle

#endif  // __SOLO_PLAYER_H__
