/**
 * @file JpegScan.h
 * @brief raw MJPEG (JPEG連結) からの1フレーム境界検出パーサー
 *
 * solo モードは LittleFS 上の raw MJPEG (Baseline JPEG を区切りなしで連結したもの)
 * を再生する。フレーム境界を「0xFFD9 を線形探索」で求めると、APPn セグメント
 * (EXIF サムネイル等) に含まれる 0xFFD9 を誤検出するため、マーカー構造を辿って
 * SOI..EOI の長さを確定させる。
 *
 * Arduino / LittleFS に依存しない純粋関数として実装してあり、native env で
 * 単体テストできる (test/test_jpeg_scan)。
 */

#ifndef __JPEG_SCAN_H__
#define __JPEG_SCAN_H__

#include <stddef.h>
#include <stdint.h>

namespace sastle {
namespace jpegscan {

/// parse() の結果
enum class Result : uint8_t {
    Ok,        ///< バッファ先頭に完全な1フレームがある (Info::length が確定)
    NeedMore,  ///< フレーム途中でバッファが尽きた (追記して再試行する)
    Invalid,   ///< JPEGとして壊れている / SOIで始まっていない
};

/// 検出した1フレームの情報
struct Info {
    size_t length;     ///< SOI..EOI を含むフレーム長 (bytes)
    uint16_t width;    ///< SOFn から読んだ幅 (0 = SOF未検出)
    uint16_t height;   ///< SOFn から読んだ高さ
    bool baseline;     ///< true = SOF0 (Baseline sequential, Huffman)

    Info() : length(0), width(0), height(0), baseline(false) {}
};

/**
 * @brief buf 先頭の1 JPEGフレームを解析する
 * @param buf   解析対象 (フレーム先頭 = SOI である必要がある)
 * @param len   buf の有効バイト数
 * @param out   結果 (Ok のとき length/width/height/baseline が有効)
 * @return Result
 *
 * @note TJpg_Decoder は Baseline (SOF0) のみデコードできる。progressive (SOF2) は
 *       out.baseline=false で返すので、呼び出し側で拒否する。
 */
inline Result parse(const uint8_t* buf, size_t len, Info& out) {
    out = Info();
    if (!buf) {
        return Result::Invalid;
    }
    if (len < 2) {
        return Result::NeedMore;
    }
    if (buf[0] != 0xFF || buf[1] != 0xD8) {
        return Result::Invalid;  // SOI で始まっていない
    }

    size_t pos = 2;
    bool sawSOS = false;

    for (;;) {
        // マーカー位置へ。0xFF はフィルバイトとして連続してよい (JPEG B.1.1.2)。
        if (pos + 2 > len) {
            return Result::NeedMore;
        }
        if (buf[pos] != 0xFF) {
            return Result::Invalid;  // マーカー境界にゴミがある
        }
        while (pos + 2 <= len && buf[pos + 1] == 0xFF) {
            pos++;  // フィルバイトを読み飛ばす
        }
        if (pos + 2 > len) {
            return Result::NeedMore;
        }
        const uint8_t marker = buf[pos + 1];
        pos += 2;

        if (marker == 0xD8) {
            return Result::Invalid;  // SOI の入れ子 = 前フレームの終端を見落としている
        }
        if (marker == 0xD9) {  // EOI
            if (!sawSOS) {
                return Result::Invalid;  // 画像データを持たない
            }
            out.length = pos;
            return Result::Ok;
        }
        // 長さフィールドを持たないマーカー (TEM / RSTn)
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            continue;
        }

        // ここから先は [長さ2バイト][ペイロード] を持つセグメント
        if (pos + 2 > len) {
            return Result::NeedMore;
        }
        const size_t segLen = ((size_t)buf[pos] << 8) | (size_t)buf[pos + 1];
        if (segLen < 2) {
            return Result::Invalid;
        }

        // SOFn: フレームサイズを拾う (0xC4=DHT, 0xC8=JPG, 0xCC=DAC は SOF ではない)
        const bool isSOF = (marker >= 0xC0 && marker <= 0xCF) &&
                           marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
        if (isSOF) {
            // ペイロード: [precision1][height2][width2][ncomp1]...
            if (segLen < 8) {
                return Result::Invalid;
            }
            if (pos + segLen > len) {
                return Result::NeedMore;
            }
            out.height = ((uint16_t)buf[pos + 3] << 8) | (uint16_t)buf[pos + 4];
            out.width = ((uint16_t)buf[pos + 5] << 8) | (uint16_t)buf[pos + 6];
            out.baseline = (marker == 0xC0);
        }

        if (marker != 0xDA) {  // SOS 以外はセグメントごと読み飛ばす
            if (pos + segLen > len) {
                return Result::NeedMore;
            }
            pos += segLen;
            continue;
        }

        // SOS: ヘッダの後ろはエントロピー符号データ。0xFF00 (バイトスタッフ) と
        // RSTn は本体の一部なので跨ぎ、それ以外のマーカーで抜ける。
        sawSOS = true;
        if (pos + segLen > len) {
            return Result::NeedMore;
        }
        size_t p = pos + segLen;
        bool markerFound = false;
        while (p + 2 <= len) {
            if (buf[p] != 0xFF) {
                p++;
                continue;
            }
            const uint8_t m = buf[p + 1];
            if (m == 0x00 || (m >= 0xD0 && m <= 0xD7)) {
                p += 2;  // スタッフィング / リスタートマーカーは画像データの一部
                continue;
            }
            if (m == 0xFF) {
                p++;  // フィルバイト
                continue;
            }
            markerFound = true;
            break;
        }
        if (!markerFound) {
            return Result::NeedMore;
        }
        pos = p;  // 次のマーカー (通常は EOI) から外側ループを続ける
    }
}

}  // namespace jpegscan
}  // namespace sastle

#endif  // __JPEG_SCAN_H__
