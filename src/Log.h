/**
 * @file Log.h
 * @brief ログ出力先 (Serial)
 *
 * solo 構成では外部 server / MQTT が居ないため、ログは Serial のみへ出す。
 * `sastle::Log` は Arduino の Print インターフェースなので print/println/printf が使える。
 */

#ifndef __LOG_H__
#define __LOG_H__

#include <Arduino.h>

namespace sastle {

extern Print& Log;

}  // namespace sastle

#endif /* __LOG_H__ */
