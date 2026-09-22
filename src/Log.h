/**
 * @file Log.h
 * @brief ログ出力先 (`sastle::Log`) の転送ヘッダ
 *
 * 実体は RemoteLog.h。sink (MQTT) を登録しなければ Serial のみへ出す (solo 単独)。
 * 呼び出し側は Print インターフェースなので print/println/printf がそのまま使える。
 */

#ifndef __LOG_H__
#define __LOG_H__

#include "RemoteLog.h"

#endif /* __LOG_H__ */
