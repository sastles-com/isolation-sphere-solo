#include "Bno055QuatReader.h"

namespace sastle {

bool Bno055QuatReader::_readWord(uint8_t reg, uint8_t& lo, uint8_t& hi) {
    _wire.beginTransmission(_addr);
    _wire.write(reg);
    if (_wire.endTransmission(false) != 0) return false;   // repeated start
    if (_wire.requestFrom(_addr, (uint8_t)2) != 2) return false;
    lo = _wire.read();
    hi = _wire.read();
    return true;
}

bool Bno055QuatReader::readQuat(uint8_t raw[8], Quat& out, const Quat& prevAccepted,
                                bool wordRead, int partialTolLsb, ImuCounters& cnt, uint8_t& straddles) {
    // endTransmission(false) と requestFrom() は repeated start で 1 つのシーケンスを成す。
    // その間に他コアの I2C が割り込むと別のレジスタ窓を読んでしまうため、全体を排他する。
    I2cGuard guard(_lock);
    for (int attempt = 0; attempt < kAttempts; attempt++) {
        if (wordRead) {
            // 2 バイト×4 トランザクション (w,x,y,z を個別に読む)。
            // 前回受理値 (LSB 単位) はワード単位の部分読み判定の基準。
            const int prevLsb[4] = {
                bno055::unitToLsb(prevAccepted.w), bno055::unitToLsb(prevAccepted.x),
                bno055::unitToLsb(prevAccepted.y), bno055::unitToLsb(prevAccepted.z) };
            bool wOk = true;
            for (int k = 0; k < 4 && wOk; k++) {
                // 化けたワードはそのワードだけ読み直す。以前はサンプル全体を 5 トランザクション
                // やり直していたため、回転中 (1 サンプルおきに化ける) は 3 試行が全滅する周期が
                // 4〜5 割あり、姿勢保持→追いつきのカクつきになった (2026-09-09)。
                bool gotWord = false;
                for (int rep = 0; rep < kWordRetries && !gotWord; rep++) {
                    uint8_t lo, hi;
                    if (!_readWord((uint8_t)(kRegQuat + 2 * k), lo, hi)) { wOk = false; break; }
                    // MSB 欠落判定: 2026-09-09 の imu_dump で実証。2 バイト目が 0xFF (ゼロ埋め個体では
                    // 0x00) になり LSB は真値のまま残る (真値 0xF2B0 → 0xFFB2)。潰れた成分が
                    // |v|=0.2〜0.38 だとノルム² 0.85〜0.95 でノルム検査を通過し、以後の真値が連続性
                    // ガードで棄却され続けて「特定の角度でジャンプ」になっていた。
                    if (bno055::wordImplausible(lo, hi, prevLsb[k], partialTolLsb)) {
                        cnt.partialReads++;
                        continue;
                    }
                    raw[2 * k] = lo; raw[2 * k + 1] = hi;
                    gotWord = true;
                }
                if (!gotWord) wOk = false;
            }
            if (!wOk) continue;
            // 5 本目: w を読み直して「4 ワードが同一サンプルか」を確認する。BNO055 は融合更新の
            // たびに全成分の符号を反転させることがあり (q と -q は同じ回転)、~5ms の読みが 10ms
            // の更新境界をまたぐと「w は反転前、x,y,z は反転後」の混合値 = 逆回転 (ノルム 1)
            // になる。更新が入れば w は bit 単位で必ず変わるので、一致しなければ読み直す。
            uint8_t w2lo, w2hi;
            if (!_readWord(kRegQuat, w2lo, w2hi)) continue;
            // bit 一致ではなく差分で判定する (Bno055Codec::wordsDisagree のコメント参照)
            if (bno055::wordsDisagree(raw[0], raw[1], w2lo, w2hi)) { cnt.straddles++; straddles++; continue; }
        } else {
            // 8 バイト一括読み (実験用: imu_wordread=false)。この個体では 3 バイト目以降が 0xFF に
            // なることが多く既定では使わないが、切り分けのため残す。
            _wire.beginTransmission(_addr);
            _wire.write(kRegQuat);
            if (_wire.endTransmission(false) != 0) continue;
            if (_wire.requestFrom(_addr, (uint8_t)8) != 8) continue;
            for (int i = 0; i < 8; i++) raw[i] = _wire.read();
        }

        // I2C が「成功」を返しつつ 8 バイト全ゼロを返すことがある。単位クォータニオンが
        // 全ゼロになることは原理的に無いので読み失敗として再試行する。
        if (bno055::allZero8(raw)) { cnt.zeroReads++; continue; }
        // 8B 一括読みの部分読み (先頭だけ有効で残りが 0x00 / 0xFF 埋め) は z の 2 バイトで検出。
        // 分割読みでは 1 ワード単位なので起きず、z=0 や -1 LSB は正常値の可能性の方が高い。
        if (!wordRead && bno055::tailWordIs(raw, 0x00)) { cnt.partialReads++; continue; }
        if (!wordRead && bno055::tailWordIs(raw, 0xFF)) { cnt.partialReads++; continue; }
        // 組み立て後のノルム検査。ワード単位の差分判定を抜けた化け (前回受理から遠くない
        // 小さい値への MSB 化け) をここで捕まえ、同じ周期内に読み直す (Bno055Codec.h 参照)。
        if (bno055::quatNormImplausible(raw)) { cnt.partialReads++; continue; }

        out = bno055::quatFromRaw(raw);
        return true;
    }
    return false;
}

bool Bno055QuatReader::readVector6(uint8_t reg, float scale, imu::Vector<3>& out,
                                   bool wordRead, VecPartialSlot& slot) {
    I2cGuard guard(_lock);
    for (int attempt = 0; attempt < kVecAttempts; attempt++) {
        uint8_t b[6];
        if (wordRead) {
            // 2B×3 トランザクション (quat と同じ理由。6B 一括は 0xFF 化する個体がある)
            bool wOk = true;
            for (int k = 0; k < 3 && wOk; k++) {
                if (!_readWord((uint8_t)(reg + 2 * k), b[2 * k], b[2 * k + 1])) wOk = false;
            }
            if (!wOk) continue;
        } else {
            _wire.beginTransmission(_addr);
            _wire.write(reg);
            if (_wire.endTransmission(false) != 0) continue;
            if (_wire.requestFrom(_addr, (uint8_t)6) != 6) continue;
            for (int i = 0; i < 6; i++) b[i] = _wire.read();
        }
        // 末尾ワードの埋め検出は一括読みのときだけ。分割読みでは 1 ワードが 0xFFFF (= -1 LSB、
        // 静止時の gyro z で頻出) でも読み失敗と区別できず、正常値を捨ててしまう。
        // partialReads には数えない (quat 専用の指標に保つ)。再試行しても同じなら今回は諦めて
        // 前回値を維持する (診断用データなので欠けても支障はない)。
        if (!wordRead && ((b[4] == 0 && b[5] == 0) || (b[4] == 0xFF && b[5] == 0xFF))) {
            slot.offer(reg, b);
            continue;
        }
        out = imu::Vector<3>(bno055::decodeLE(b[0], b[1]) * scale,
                             bno055::decodeLE(b[2], b[3]) * scale,
                             bno055::decodeLE(b[4], b[5]) * scale);
        return true;
    }
    return false;
}

} // namespace sastle
