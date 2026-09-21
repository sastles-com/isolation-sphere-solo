/**
 * @file MjpegReader.h
 * @brief LittleFS上の raw MJPEG を1フレームずつ読み出すリーダー
 * @note フレーム境界の判定は JpegScan.h (マーカー追跡) に委譲する。
 *       読み込みバッファ境界を跨ぐマーカーは NeedMore → 追記読み で解決する。
 */

#ifndef __MJPEG_READER_H__
#define __MJPEG_READER_H__

#include <Arduino.h>
#include <LittleFS.h>

namespace sastle {

/// ファイル全体を走査して得た MJPEG の素性
struct MjpegInfo {
    uint32_t frames;        ///< 総フレーム数
    size_t maxFrameBytes;   ///< 最大フレームサイズ
    size_t fileBytes;       ///< ファイルサイズ
    uint16_t width;         ///< 解像度 (全フレーム共通であることを検証済み)
    uint16_t height;

    MjpegInfo() : frames(0), maxFrameBytes(0), fileBytes(0), width(0), height(0) {}
};

/**
 * @class MjpegReader
 * @brief raw MJPEG ファイルの順次読み出し (EOFで先頭へループ)
 *
 * フレームバッファは呼び出し側が確保して渡す (PSRAM を想定)。読み出したフレームは
 * そのバッファ先頭に置かれ、次フレーム分まで先読みされたバイトはバッファ内に保持して
 * 次回呼び出しで再利用する。
 */
class MjpegReader {
public:
    /// next() の結果
    enum class Status : uint8_t {
        Ok,        ///< 1フレーム取得
        NotOpen,   ///< open されていない
        Empty,     ///< ファイルに有効なフレームが1つも無い
        Corrupt,   ///< JPEG構造が壊れている
        TooLarge,  ///< 1フレームがバッファ上限を超える
    };

    MjpegReader() : _buf(nullptr), _cap(0), _fill(0), _fileBytes(0), _loops(0), _open(false) {}
    ~MjpegReader() { close(); }

    /**
     * @brief MJPEGファイルを開く
     * @param path      ファイルパス
     * @param frameBuf  フレーム組み立てバッファ (呼び出し側所有)
     * @param frameCap  frameBuf の容量 = 受理する1フレームの上限サイズ
     */
    bool open(const char* path, uint8_t* frameBuf, size_t frameCap);

    void close();
    bool isOpen() const { return _open; }

    /**
     * @brief 次の完全な1フレームをバッファ先頭に読み出す
     * @param outSize  フレーム長 (bytes)
     * @param wrapped  EOFに達して先頭へ巻き戻した場合 true
     */
    Status next(size_t& outSize, bool& wrapped);

    size_t fileBytes() const { return _fileBytes; }
    uint32_t loops() const { return _loops; }

    /**
     * @brief ファイル全体を走査して受理可否を判定する (アップロード検証用)
     * @param path           検証対象
     * @param expectWidth    要求解像度 (幅)
     * @param expectHeight   要求解像度 (高さ)
     * @param maxFrameBytes  1フレームの上限サイズ
     * @param scratch        走査用バッファ (>= maxFrameBytes)
     * @param scratchCap     scratch の容量
     * @param out            検証結果
     * @param errorOut       失敗理由 (静的文字列)
     * @return true 受理可能
     *
     * @note 部分ファイル (途中EOF)・解像度違い・progressive JPEG・過大フレーム・
     *       末尾のゴミをすべて不合格にする。
     */
    static bool validate(const char* path, uint16_t expectWidth, uint16_t expectHeight,
                         size_t maxFrameBytes, uint8_t* scratch, size_t scratchCap,
                         MjpegInfo& out, const char** errorOut);

private:
    /// ファイルから buf の空き領域へ追記読み。戻り値=読み込めたバイト数 (0=EOF)
    size_t fillMore();

    fs::File _file;
    uint8_t* _buf;
    size_t _cap;
    size_t _fill;        ///< _buf 内の有効バイト数
    size_t _fileBytes;
    uint32_t _loops;     ///< ループ回数
    bool _open;
};

}  // namespace sastle

#endif  // __MJPEG_READER_H__
