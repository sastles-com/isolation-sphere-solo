/**
 * @file SourceArbiter.h
 * @brief フレーム供給元 (ローカル再生 / ネットワーク配信) の調停ポリシー (純粋、Arduino 非依存)
 *
 * FramePump が 1 タスクで両方の供給元を処理するための判断だけを持つ:
 *   - network が「生きている」= 最後の完成フレームから idle_timeout 未満
 *   - mode: Auto (network 優先、途切れたら local) / Local (network を読み捨て) / Network (local を止める)
 *   - local の締切管理 (旧 SoloPlayer::taskFunc の単調クロック方式をそのまま移した):
 *     期限を過ぎていたら遅れた周期ぶんを miss として捨て、次の期限に揃える
 * 時刻は呼び手が渡す (ms と us の 2 系統。ms は網の鮮度、us は締切)。
 */
#pragma once
#include <stdint.h>
#include <string.h>

namespace sastle {

class SourceArbiter {
public:
    enum class Mode : uint8_t { Auto, Local, Network };
    enum class Active : uint8_t { None, Local, Network };

    void configure(Mode mode, uint32_t idleTimeoutMs, uint32_t periodUs) {
        _mode = mode;
        _idleTimeoutMs = idleTimeoutMs;
        _periodUs = periodUs ? periodUs : 100000;
    }
    void setMode(Mode m) { _mode = m; }
    Mode mode() const { return _mode; }

    static bool parseMode(const char* name, Mode& out) {
        if (!name) return false;
        if (strcmp(name, "auto") == 0)    { out = Mode::Auto;    return true; }
        if (strcmp(name, "local") == 0)   { out = Mode::Local;   return true; }
        if (strcmp(name, "network") == 0) { out = Mode::Network; return true; }
        return false;
    }
    static const char* modeName(Mode m) {
        return m == Mode::Local ? "local" : m == Mode::Network ? "network" : "auto";
    }
    static const char* activeName(Active a) {
        return a == Active::Local ? "local" : a == Active::Network ? "network" : "none";
    }

    // --- network 側 ---
    /// 完成フレームを 1 枚公開した
    void onNetFrame(uint32_t nowMs) {
        _lastNetMs = nowMs;
        _hasNet = true;
        _netWasLive = true;
    }
    /// network が表示を握っているか (Local モードでは常に false)
    bool netLive(uint32_t nowMs) const {
        return _mode != Mode::Local && _hasNet && (uint32_t)(nowMs - _lastNetMs) < _idleTimeoutMs;
    }
    /// live → idle に落ちた瞬間に 1 回だけ true (ローカル復帰・黒要求のトリガ)
    bool takeNetIdleEdge(uint32_t nowMs) {
        if (_netWasLive && !netLive(nowMs)) {
            _netWasLive = false;
            return true;
        }
        return false;
    }
    bool hasNet() const { return _hasNet; }
    uint32_t lastNetMs() const { return _lastNetMs; }

    // --- local 締切 ---
    /// 次の締切を now + period に置き直す (起動時、network→local 復帰時、生配信中の追従)
    void rearm(int64_t nowUs) { _nextUs = nowUs + _periodUs; }
    /// 次の締切までの待ち時間 [us] (期限切れなら 0)
    uint32_t waitUs(int64_t nowUs) const {
        return nowUs >= _nextUs ? 0u : (uint32_t)(_nextUs - nowUs);
    }
    /**
     * @brief 締切が来ていれば true を返し、次の締切へ進める
     * @param missed 遅れて捨てた周期数 (締切ちょうど〜1 周期未満の遅れは 0)
     */
    bool localDue(int64_t nowUs, uint32_t& missed) {
        missed = 0;
        if (nowUs < _nextUs) return false;
        const int64_t late = nowUs - _nextUs;
        missed = (uint32_t)(late / _periodUs);
        _nextUs += (int64_t)(missed + 1) * (int64_t)_periodUs;
        return true;
    }
    /// local の tick を実行してよいか (Network モードでは常に false、Auto では網が生きていない間)
    bool localAllowed(uint32_t nowMs) const {
        return _mode != Mode::Network && !netLive(nowMs);
    }

    /// 今どちらが表示を握っているか (localPlaying = SoloPlayer が Playing か)
    Active active(uint32_t nowMs, bool localPlaying) const {
        if (netLive(nowMs)) return Active::Network;
        if (_mode != Mode::Network && localPlaying) return Active::Local;
        return Active::None;
    }

private:
    Mode _mode = Mode::Auto;
    uint32_t _idleTimeoutMs = 2000;
    uint32_t _periodUs = 100000;
    bool _hasNet = false;
    bool _netWasLive = false;
    uint32_t _lastNetMs = 0;
    int64_t _nextUs = 0;
};

}  // namespace sastle
