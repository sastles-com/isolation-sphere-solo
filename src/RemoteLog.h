/**
 * @file RemoteLog.h
 * @brief Serial と (任意で) リモート sink の双方へ出力する tee ロガー
 * @author sastle-com
 *
 * 派生元 feat/ui-v2 の RemoteLog を移植し、solo でも同じ `sastle::Log` として使う。
 *   - sink 未登録 (solo 単独): Serial のみ。従来の Log と同じ挙動。
 *   - sink 登録 (server モード: MQTTManager が LogSink を実装): 行 ('\n') 単位で退避し、
 *     loop() で接続中に少しずつ publish する。USB が届かない (球体封止) 状態でも
 *     デバッグログを MQTT 経由で Web UI から確認できる。
 *
 * solo での変更点:
 *   - MQTTManager への直接依存を LogSink インターフェースに置き換え (Phase 5 で実装が入る)
 *   - backlog (12KB) を PSRAM に置く (内部 RAM を食わない)
 *   - write() をミューテックスで保護する。solo では loopTask と httpd タスクの両方が Log を
 *     呼ぶため (派生元は「loopTask だけがログを出す」規約だった)。取れなければ Serial のみ。
 *   - IMU タスクは従来どおり Log を呼ばず、診断スナップショットを loop 側が吐く。
 */

#ifndef __REMOTE_LOG_H__
#define __REMOTE_LOG_H__

#include <Arduino.h>
#include <stddef.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace sastle {

/// ログ行の送信先。MQTTManager が実装する (Phase 5)。
class LogSink {
public:
    virtual ~LogSink() {}
    /// 送信できる状態か (未接続なら loop() は退避のまま待つ)
    virtual bool logConnected() = 0;
    /// 1 行を送信する。false = 送れなかった (退避に残す)
    virtual bool publishLog(const char* suffix, const char* line) = 0;
};

class RemoteLog : public Print {
public:
    /**
     * @brief 退避バッファとミューテックスを用意する。setup() の Serial.begin() 直後に 1 回呼ぶ。
     * @note 呼ばなくても Serial への出力は動く (退避なし・ロックなし)。
     */
    void begin();

    /**
     * @brief 送信先 sink とトピックサフィックスを登録する (接続前でも可)
     * @param sink   LogSink 実装 (nullptr で解除)
     * @param suffix デバイストピックの末尾 (例 "log")
     */
    void setSink(LogSink* sink, const char* suffix);

    // Print インターフェース: 全出力を Serial にミラーしつつ行を組み立てる
    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buffer, size_t size) override;

    /**
     * @brief 退避済みログを sink へフラッシュする (メインループから呼ぶ)
     */
    void loop();

    size_t backlogLen() const { return _backlogLen; }
    bool hasSink() const { return _sink != nullptr; }

private:
    void putc(uint8_t c);              ///< ロック済み前提の 1 バイト処理
    bool publishLine(const char* line);
    void pushBacklog(const char* line, size_t len);
    bool lock();
    void unlock();

    LogSink* _sink = nullptr;
    const char* _suffix = nullptr;
    SemaphoreHandle_t _mutex = nullptr;

    char _line[240];           ///< 組み立て中の1行
    size_t _lineLen = 0;

    // 12KB: 起動シーケンス + [DUMP] 500 サンプル (63 行 × ~200B) を取りこぼさない容量。
    static constexpr size_t kBacklogCapacity = 12288;
    static constexpr uint8_t kFlushPerLoop = 3;    ///< loop() 1 回で publish する最大行数
    char* _backlog = nullptr;         ///< 未送信ログ ('\n' 区切り, 起動初期を優先保持)。PSRAM
    size_t _backlogLen = 0;
    bool _backlogDropped = false;     ///< 容量超過で破棄が発生したか
    bool _busy = false;               ///< publish 経路からの再入防止
};

extern RemoteLog Log;

} // namespace sastle

#endif /* __REMOTE_LOG_H__ */
