/**
 * @file Bno055QuatReader.h
 * @brief BNO055 のクォータニオン / 6 バイトベクタを、この個体の I2C 事情に合わせて読む
 *
 * IMUManager::_updateOnce / _readVector6 から抽出 (2026-09)。この層が知っていること:
 *   - 2 バイト×ワードの分割読み (8B 一括は 3 バイト目以降が 0xFF になる個体がある)
 *   - ワード単位の MSB 欠落 (0xFF/0x00) 判定と、そのワードだけの読み直し (最大 3 回)
 *   - x,y,z の後に w を再読みして融合更新境界をまたいだ読みを捨てる (straddle)
 *   - 全ゼロ / 末尾埋め (8B 一括読みのとき) の検出
 * 知らないこと: 妥当性判定 (AttitudeValidator)、公開状態、ログ。
 * sastle::Log は呼ばない (スレッド安全でない。IMU タスクから呼ぶと core0 が止まる)。
 *
 * Wire.begin / setTimeOut は呼び手 (IMUManager::begin) が行い、ここは既に初期化済みの
 * TwoWire を借りる。全トランザクションは I2cLock の下で行う。
 */
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <utility/imumaths.h>
#include "Quat.h"
#include "Bno055Codec.h"
#include "ImuDiag.h"
#include "I2cLock.h"

namespace sastle {

class Bno055QuatReader {
public:
    static constexpr uint8_t kRegQuat = 0x20;   ///< QUA_DATA_W_LSB
    static constexpr int kAttempts = 3;         ///< サンプル全体の試行回数
    static constexpr int kWordRetries = 3;      ///< 化けたワード 1 個の読み直し回数
    static constexpr int kVecAttempts = 2;

    Bno055QuatReader(TwoWire& wire, uint8_t addr, I2cLock& lock)
        : _wire(wire), _addr(addr), _lock(lock) {}

    /**
     * @brief クォータニオンを 1 サンプル読む
     * @param raw          読んだ生 8 バイト (失敗時も途中まで入る。診断用)
     * @param out          デコード結果 (成功時のみ有効)
     * @param prevAccepted 前回受理値 (MSB 欠落判定の基準)
     * @param wordRead     true = 2B×4 分割読み、false = 8B 一括読み (実験用)
     * @param cnt          partialReads / zeroReads / straddles を加算する
     * @param straddles    この呼び出しで w 再読み不一致が何回起きたか
     * @return 3 試行以内に読めたか
     */
    bool readQuat(uint8_t raw[8], Quat& out, const Quat& prevAccepted, bool wordRead, int partialTolLsb,
                  ImuCounters& cnt, uint8_t& straddles);

    /**
     * @brief 6 バイトのベクタレジスタ (accel 0x08 / gyro 0x14 / euler 0x1A) を読む
     * @param slot 8B 一括読みで末尾埋めを検出したときの生バイト退避先
     * @return 読めたか (読めなければ out は変更しない)
     */
    bool readVector6(uint8_t reg, float scale, imu::Vector<3>& out, bool wordRead,
                     VecPartialSlot& slot);

    /// I2C クロック変更 (IMU タスクから、トランザクションの隙間で呼ぶ)
    void setClock(uint32_t hz) {
        I2cGuard guard(_lock);
        _wire.setClock(hz);
    }

private:
    /// 2 バイト 1 ワードを読む。false = I2C エラー
    bool _readWord(uint8_t reg, uint8_t& lo, uint8_t& hi);

    TwoWire& _wire;
    const uint8_t _addr;
    I2cLock& _lock;
};

} // namespace sastle
