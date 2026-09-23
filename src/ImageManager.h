/**
 * @file ImageManager.h
 * @brief JPEGデコードとトリプルバッファ管理クラス
 * @author sastle-com
 *
 * フレームの供給元 (solo では SoloPlayer が LittleFS から読む) は submitJpegFrame()
 * でここへ JPEG を渡す。デコード結果 (RGB565) は PSRAM 上のトリプルバッファ
 * (display/ready/decode) に置かれ、描画タスク (LEDManager, 別コア) が
 * adoptReadyFrame() で受け取る。
 */

#pragma once

#include <Arduino.h>
#include <TJpg_Decoder.h>
#include "ConfigManager.h"
#include "FrameBufferPool.h"    // display/ready/decode トリプルバッファ

namespace sastle {

/**
 * @struct ImageStats
 * @brief 画像処理統計情報
 */
struct ImageStats {
    uint32_t frames_decoded;     ///< デコード成功フレーム数
    uint32_t frames_dropped;     ///< ドロップフレーム数 (表示前に上書き)
    uint32_t decode_errors;      ///< デコードエラー数
    float fps;                   ///< 現在のFPS
    unsigned long last_frame_time; ///< 最終フレーム時刻
    size_t last_jpeg_size;       ///< 最終JPEG画像サイズ
    uint32_t decode_time_us;     ///< 最後のJPEGデコード所要時間 (μs)
};

/**
 * @class ImageManager
 * @brief JPEGデコードとトリプルバッファ管理クラス
 */
class ImageManager {
public:
    ImageManager();
    ~ImageManager();

    /**
     * @brief 画像マネージャーを初期化 (PSRAMにトリプルバッファを確保)
     * @param config 設定マネージャー参照
     * @return true 初期化成功, false 初期化失敗
     */
    bool begin(ConfigManager& config);

    /**
     * @brief リソースを解放
     */
    void end();

    /**
     * @brief 表示待ちの完成フレームがあれば表示バッファに採用する (render側が毎パス呼ぶ)
     * @note これによりレンダリングは連続駆動(IMU再マッピング)しつつ、フレーム差し替えを
     *       独立に行える。新フレームが無ければ現在の表示バッファを維持。
     */
    void adoptReadyFrame();

    /**
     * @brief JPEGを1フレームとしてデコードし、表示待ち(ready)に公開する
     * @param jpeg JPEGデータ先頭
     * @param size JPEGバイト数
     * @return true デコード成功, false 解像度不一致/デコード失敗
     * @warning TJpg_Decoder はグローバル単一インスタンスなので、複数タスクから
     *          同時に呼んではいけない (FramePump タスクのみが呼ぶ。ローカル再生も
     *          UDP 配信も同じタスクが順番に処理する)。
     */
    bool submitJpegFrame(const uint8_t* jpeg, size_t size);

    /**
     * @brief 全画素 0 のフレームを公開する (停止・動画削除・エラー時に LED を消灯させる)
     * @note 描画タスクは publish 済みフレームを再マッピングし続けるため、停止しても
     *       最後の映像が残る。デコードと同じバッファに書くため、呼べるのはデコードを行う
     *       タスク (FramePump) のみ。他のタスクは requestBlack() で要求する。
     */
    void publishBlack();

    /// 消灯を要求する (任意のタスクから可)。FramePump が serviceBlankRequest() で実行する
    void requestBlack() { _blankRequested = true; }
    /// 保留中の消灯要求があれば実行する (FramePump タスクのみ)
    void serviceBlankRequest() {
        if (_blankRequested) {
            _blankRequested = false;
            publishBlack();
        }
    }
    bool blankPending() const { return _blankRequested; }

    /**
     * @brief 指定座標のピクセル色を取得 (RGB)
     * @param x X座標 (0 ~ width-1)
     * @param y Y座標 (0 ~ height-1)
     * @param r 赤成分 (0-255) (出力)
     * @param g 緑成分 (0-255) (出力)
     * @param b 青成分 (0-255) (出力)
     * @return true 取得成功, false 座標範囲外
     */
    bool getPixel(uint16_t x, uint16_t y, uint8_t& r, uint8_t& g, uint8_t& b);

    uint16_t getWidth() const { return _width; }
    uint16_t getHeight() const { return _height; }

    ImageStats getStats() const;
    bool isInitialized() const { return _initialized; }
    /// ドロップ数 (render が追いつかず表示前に破棄された数)
    uint32_t getDropped() const { return _framesDropped; }

    void printStats();

private:
    bool _initialized;           ///< 初期化状態

    // 画像パラメータ
    uint16_t _width;             ///< デコード後の画像幅
    uint16_t _height;            ///< デコード後の画像高さ
    size_t _bufferSize;          ///< 1バッファのサイズ (bytes)

    // 描画/デコードのトリプルバッファ (display/ready/decode)。差し替えは FrameBufferPool。
    FrameBufferPool _pool;
    uint16_t* _displayBuffer = nullptr;  ///< getPixel が読む (= _pool.displayBuffer() のキャッシュ)
    uint16_t* _decodeBuffer = nullptr;   ///< tjpgが書く (= _pool.decodeBuffer() のキャッシュ)

    // 統計情報
    uint32_t _framesDecoded;     ///< デコード成功数
    uint32_t _framesDropped;     ///< ドロップ数
    uint32_t _decodeErrors;      ///< デコードエラー数
    unsigned long _lastFrameTime; ///< 最終フレーム時刻
    unsigned long _fpsTimestamp;  ///< FPS計測開始時刻
    uint32_t _fpsFrameCount;     ///< FPS計測用フレーム数
    float _currentFPS;           ///< 現在のFPS
    size_t _lastJpegSize;        ///< 最終JPEGサイズ
    uint32_t _lastDecodeUs = 0;  ///< 最後のJPEGデコード所要時間 (μs)
    volatile bool _blankRequested = false;  ///< requestBlack() の保留フラグ

    /**
     * @brief PSRAMにバッファを確保
     */
    bool allocateBuffers();

    /**
     * @brief バッファを解放
     */
    void freeBuffers();

    /**
     * @brief JPEGをデコードして decode バッファに展開
     */
    bool decodeJPEG(const uint8_t* jpeg_data, size_t jpeg_size);

    /**
     * @brief decode完成フレームを ready に公開 (トリプルバッファ)
     */
    void publishFrame();

    /**
     * @brief FPSを計算
     */
    void calculateFPS();

    /**
     * @brief TJpg_Decoder出力コールバック (静的)
     */
    static bool tjpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);

    /// インスタンスポインタ (TJpg_Decoderコールバック用)
    static ImageManager* _instance;

    /// TJpg_Decoder実行時のターゲットバッファ
    uint16_t* _tjpgTargetBuffer;
};

} // namespace sastle
