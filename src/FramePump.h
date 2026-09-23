/**
 * @file FramePump.h
 * @brief フレーム供給の単一タスク: UDP 配信 (network) とローカル MJPEG 再生 (local) を 1 つの
 *        タスクで順番に処理し、ImageManager::submitJpegFrame() を呼ぶ唯一の場所になる
 *
 * TJpg_Decoder はグローバル単一インスタンスなので、デコードは 1 タスクに限る必要がある。
 * 派生元 core の img_decode タスク (UDP) と solo の solo_play タスク (ローカル) を統合した。
 *
 * ループ 1 反復:
 *   1. network が生きていなければ、保留中の黒要求 (停止/動画削除/エラー) を実行する
 *   2. UDP キューを「次のローカル締切まで」待つ
 *      - データグラム到着 → FrameReassembler → 完成なら submitJpegFrame → network live
 *        (生配信中はローカル締切を追従させて miss を数えない) → vTaskDelay(1) (TASK_WDT 対策)
 *   3. タイムアウト (締切) → network が生きていなければ SoloPlayer::tick()
 *      network が live→idle に落ちた瞬間はローカル締切を置き直し、再生中でなければ黒にする
 * UDP 受信器が無い (server 無効) ときは 2. が単なる待ちになり、従来の solo と同じ動きになる。
 *
 * OTA 時は stopForOta() で協調停止する (デコード中に殺さない)。
 */

#ifndef __FRAME_PUMP_H__
#define __FRAME_PUMP_H__

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ConfigManager.h"
#include "FrameReassembler.h"
#include "ImageManager.h"
#include "SoloPlayer.h"
#include "SourceArbiter.h"
#include "UdpReceiver.h"

namespace sastle {

class FramePump {
public:
    struct Deps {
        ImageManager* image = nullptr;
        SoloPlayer* player = nullptr;
        UdpReceiver* udp = nullptr;     ///< nullptr = network 供給なし
    };

    struct Stats {
        uint32_t netFrames;        ///< 完成して公開した network フレーム数
        uint32_t netDecodeErrors;  ///< network フレームのデコード失敗数
        uint32_t udpDatagrams;     ///< 取り出したデータグラム数
        uint32_t reasmDropped;     ///< 未完のまま捨てたフレーム数 (FrameReassembler)
        float netFps;              ///< network フレームの実効レート
        uint32_t lastNetMs;        ///< 最後の network フレーム時刻 (millis)
        uint32_t stackMinWords;    ///< タスクスタックの最小残り (words)
    };

    ~FramePump();

    /// 再構成バッファ (PSRAM 64KB) を確保し、調停ポリシーを設定する
    bool begin(const Deps& deps, const SourceConfig& cfg);
    bool startTask(uint8_t core = 0, uint8_t priority = 1, uint32_t stackSize = 8192);

    /// OTA 前の協調停止: タスクを止め、UDP 受信も閉じる (最大 waitMs 待つ)
    void stopForOta(uint32_t waitMs = 600);
    bool isRunning() const { return _running; }

    bool setSourceMode(SourceArbiter::Mode m);
    SourceArbiter::Mode sourceMode() const { return _arb.mode(); }
    const char* sourceModeName() const { return SourceArbiter::modeName(_arb.mode()); }
    SourceArbiter::Active activeSource() const;
    const char* activeSourceName() const { return SourceArbiter::activeName(activeSource()); }
    bool netLive() const { return _arb.netLive(millis()); }

    Stats stats() const;

private:
    static void taskFunc(void* param);
    void loop();
    void onNetIdle();

    Deps _d;
    SourceArbiter _arb;
    FrameReassembler _reasm;
    UdpReceiver::Datagram _dg;       ///< 取り出し先 (1.5KB。タスク専用)
    uint8_t* _netBuf = nullptr;      ///< 再構成先 (PSRAM)
    size_t _netBufSize = 0;
    bool _idleBlank = true;

    TaskHandle_t _task = nullptr;
    volatile bool _running = false;
    volatile bool _stopRequested = false;

    // 統計
    volatile uint32_t _netFrames = 0;
    volatile uint32_t _netDecodeErrors = 0;
    volatile uint32_t _udpDatagrams = 0;
    volatile float _netFps = 0.0f;
    uint32_t _netFpsCount = 0;
    uint32_t _netFpsTs = 0;
    volatile uint32_t _stackMin = 0;

    /// 再構成バッファ。UDP ペイロード上限 65507B に合わせる (派生元 MAX_UDP_IMAGE_SIZE)
    static constexpr size_t kNetBufSize = 65507;
};

}  // namespace sastle

#endif  // __FRAME_PUMP_H__
