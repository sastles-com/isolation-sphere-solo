#ifndef __BOARD_CONFIG_H__
#define __BOARD_CONFIG_H__

// ハードウェア構成のエントリポイント。
// 実体はボード別ヘッダ (src/boards/board_*.h) に分離し、
// board.h が build_flags の -D BOARD_* を見て切り替える。
//
// 既存コードは従来どおり #include "BoardConfig.h" で
// sastle::kLedPinN / kImuI2cSda 等を参照できる。
#include "board.h"   // -I src/boards で解決

#endif /* __BOARD_CONFIG_H__ */
