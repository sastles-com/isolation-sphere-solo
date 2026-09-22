/**
 * @file AttitudeStepDiag.h
 * @brief 描画に渡った姿勢の健全性診断 (純粋、Arduino 非依存)
 *
 * LEDManager::updateLEDBuffer の毎フレーム診断ブロックから抽出 (2026-09)。
 *   - ortho: 3 軸基底を回した後の相互内積の最大 |dot|。正規な回転なら 0。0 でなければ
 *            渡っている quat が壊れている (非正規化 / torn read)
 *   - norm : |q|² と 1 の差
 *   - step : 前フレームからの姿勢変化 [deg]。滑らかな回転なら一定、レート差で飛び飛びなら
 *            1step/2step が交互に出る (ビート検出)。q と -q は同じ回転なので |dot|
 * 呼び手 (LEDManager) が持つ最大値を更新する。[QDIAG] ログが 2 秒ごとにリセットする。
 */
#pragma once
#include <math.h>
#include "../SphereMap.h"

namespace sastle {

class AttitudeStepDiag {
public:
    void observe(float qw, float qx, float qy, float qz,
                 float& orthoMax, float& normMax, float& stepMax) {
        const float n2 = qw*qw + qx*qx + qy*qy + qz*qz;
        float e[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
        for (int k = 0; k < 3; k++) {
            sphere::rotateByQuaternion(e[k][0], e[k][1], e[k][2], qw, qx, qy, qz);
        }
        const float d01 = e[0][0]*e[1][0] + e[0][1]*e[1][1] + e[0][2]*e[1][2];
        const float d12 = e[1][0]*e[2][0] + e[1][1]*e[2][1] + e[1][2]*e[2][2];
        const float d20 = e[2][0]*e[0][0] + e[2][1]*e[0][1] + e[2][2]*e[0][2];
        float ortho = fabsf(d01);
        if (fabsf(d12) > ortho) ortho = fabsf(d12);
        if (fabsf(d20) > ortho) ortho = fabsf(d20);
        if (ortho > orthoMax) orthoMax = ortho;
        const float normErr = fabsf(n2 - 1.0f);
        if (normErr > normMax) normMax = normErr;

        const float dot = fabsf(qw*_prevW + qx*_prevX + qy*_prevY + qz*_prevZ);
        const float dc = dot > 1.0f ? 1.0f : dot;
        const float stepDeg = 2.0f * acosf(dc) * 57.2957795f;
        if (stepDeg > stepMax) stepMax = stepDeg;
        _prevW = qw; _prevX = qx; _prevY = qy; _prevZ = qz;
    }

private:
    float _prevW = 1.0f, _prevX = 0.0f, _prevY = 0.0f, _prevZ = 0.0f;   ///< 前フレームの姿勢
};

} // namespace sastle
