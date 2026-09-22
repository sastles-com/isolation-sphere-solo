// ホストテスト用の最小 Arduino シム (tools/timesync_test/shim/Arduino.h を踏襲)。
// 純粋ヘッダ (src/imu/*.h 等) は Arduino.h を include しないので通常は不要だが、
// 将来 TimeSync.cpp のような millis() 依存の .cpp をテストに直接リンクするときのために置く。
#pragma once
#include <cstdint>
#include <cstring>

// テスト側が書き換える擬似ミリ秒/マイクロ秒カウンタ (ESP32 の millis() は 32bit)。
extern uint32_t g_fakeMillis;
inline unsigned long millis() { return (unsigned long)g_fakeMillis; }
inline unsigned long micros() { return (unsigned long)g_fakeMillis * 1000UL; }
inline void delay(unsigned long) {}
