/**
 * @file SerialConsole.h
 * @brief シリアルコンソール: Web UI に繋げない状況でも状態確認と操作ができる復帰経路
 *
 * 1 行コマンド (改行終端): help | status | play | pause | stop | led sphere|test|off
 *                          | bri N | server on|off | reboot
 */

#ifndef __SERIAL_CONSOLE_H__
#define __SERIAL_CONSOLE_H__

#include <Arduino.h>

#include "DeviceController.h"

namespace sastle {

class SerialConsole {
public:
    void begin(DeviceController& ctl, const String& apSsid) {
        _ctl = &ctl;
        _apSsid = apSsid;
    }

    /// loop() から呼ぶ (Serial.available をノンブロッキングで消費する)
    void poll();

private:
    void execute(char* cmd);

    DeviceController* _ctl = nullptr;
    String _apSsid;
    char _line[64];
    size_t _len = 0;
};

}  // namespace sastle

#endif  // __SERIAL_CONSOLE_H__
