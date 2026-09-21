#ifndef __COMMON_H__
#define __COMMON_H__

// グローバルデバッグフラグ (ConfigManager::getSystemConfig() で設定)
extern bool g_debugEnabled;

#include "Log.h"

// デバッグ出力マクロ (system.debug フラグで制御)。出力先は Serial。
#define DEBUG_PRINT(...)    do { if (g_debugEnabled) ::sastle::Log.print(__VA_ARGS__); } while(0)
#define DEBUG_PRINTLN(...)  do { if (g_debugEnabled) ::sastle::Log.println(__VA_ARGS__); } while(0)
#define DEBUG_PRINTF(...)   do { if (g_debugEnabled) ::sastle::Log.printf(__VA_ARGS__); } while(0)

#endif /* __COMMON_H__ */
