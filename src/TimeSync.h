/**
 * @file TimeSync.h
 * @brief サーバー配布の時刻ビーコンから共通タイムベースを維持するクラス
 * @author sastle-com
 * @date 2026-07-08
 */

#pragma once

#include <Arduino.h>

namespace sastle {

/**
 * @class TimeSync
 * @brief 複数コアで共通のエポック時刻 (ms) を維持する。
 *
 * サーバーが sphere/all/clock で 1 秒周期にブロードキャストする epoch_ms を受け、
 * offset = epoch_ms - monotonicMs() を EMA でローパスして保持する。以降は
 * syncedNow() = offset + monotonicMs() で全コア共通のエポック ms を返す。
 *
 * 設計の詳細は core/doc/time_sync_show.md を参照。
 *
 * - millis() の 32bit ラップは内部で 64bit 単調時刻に拡張して吸収する
 *   (update()/syncedNow() が定期的に呼ばれる前提。ビーコン 1 秒周期で十分)。
 * - WiFi ジッタがそのまま offset に注入されるのを防ぐため、EMA 平滑化と
 *   外れ値棄却を行う。サーバー再起動等でクロックが大きく飛んだ場合は、
 *   外れ値が連続したら追従して再同期する。
 */
class TimeSync {
public:
    TimeSync() = default;

    /**
     * @brief 時刻ビーコン (sphere/all/clock) の JSON ペイロードを処理する。
     * @param payload 例: {"epoch_ms":1751940000123,"seq":42}
     * @return パース成功で true
     */
    bool onClockMessage(const char* payload);

    /**
     * @brief 同期済みのエポック時刻 (ms) を返す。
     * @note 未同期のうちは monotonicMs() (起動からの経過ms) を返す。
     *       同期済みかは isSynced() で確認すること。
     */
    int64_t syncedNow();

    /// 一度でもビーコンを受信し offset が確立していれば true
    bool isSynced() const { return _synced; }

    /// 現在の offset (診断用, ms)
    int64_t offset() const { return _offsetMs; }

    /// 最後に適用したビーコンの seq (診断用)
    uint32_t lastSeq() const { return _lastSeq; }

private:
    /// millis() を 64bit 単調時刻に拡張して返す (32bit ラップを吸収)
    int64_t monotonicMs();

    /// offset のサンプルを EMA/外れ値棄却で反映する
    void applySample(int64_t sample);

    bool _synced = false;
    int64_t _offsetMs = 0;      ///< 平滑化済み offset (epoch_ms - monotonicMs)
    uint32_t _lastSeq = 0;      ///< 最後に適用したビーコン seq
    int _outlierCount = 0;      ///< 連続外れ値カウント

    // millis() ラップ拡張用
    uint32_t _lastMillis = 0;
    uint32_t _wrapCount = 0;

    // 調整パラメータ
    static constexpr int64_t OUTLIER_THRESHOLD_MS = 200;  ///< これを超える差は外れ値候補
    static constexpr int OUTLIER_LIMIT = 5;               ///< 連続外れ値がこの数に達したら追従再同期
    static constexpr int EMA_SHIFT = 2;                   ///< EMA 係数 α = 1/4 (>>2)
};

} // namespace sastle
