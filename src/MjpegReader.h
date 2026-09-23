/**
 * @file MjpegReader.h
 * @brief raw MJPEG を1フレームずつ読み出すリーダー (読み出し源: PSRAM 上のコピー、または LittleFS)
 * @note フレーム境界の判定は JpegScan.h (マーカー追跡) に委譲する。
 *       読み込みバッファ境界を跨ぐマーカーは NeedMore → 追記読み で解決する。
 *
 * 通常の再生は openMemory() で PSRAM 上のコピーから読む (再生中にフラッシュを読まない)。
 * open() の LittleFS 直読みは PSRAM が確保できないときの退避経路。
 */

#ifndef __MJPEG_READER_H__
#define __MJPEG_READER_H__

#include <Arduino.h>
#include <LittleFS.h>

#include "MjpegAssembler.h"

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
 * @brief raw MJPEG の順次読み出し (EOFで先頭へループ)
 *
 * フレームバッファは呼び出し側が確保して渡す (PSRAM を想定)。読み出したフレームは
 * そのバッファ先頭に置かれ、次に next() を呼ぶまで保たれる。先読みしたバイトはフレームの
 * 後ろに保持し、次回呼び出しの冒頭で先頭へ寄せる (バッファ管理は MjpegAssembler.h、
 * native env で単体テスト済み)。
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

    MjpegReader() : _fileBytes(0), _open(false), _fromMemory(false) { _src.file = &_file; }
    ~MjpegReader() { close(); }

    /**
     * @brief MJPEGファイルを LittleFS から直接読む形で開く (退避経路)
     * @param path      ファイルパス
     * @param frameBuf  フレーム組み立てバッファ (呼び出し側所有)
     * @param frameCap  frameBuf の容量 = 受理する1フレームの上限サイズ
     */
    bool open(const char* path, uint8_t* frameBuf, size_t frameCap);

    /**
     * @brief メモリ (PSRAM) 上の MJPEG を開く。再生中にフラッシュを読まない
     * @param data  MJPEG 全体 (呼び出し側所有。close() まで有効であること)
     * @param size  バイト数
     */
    bool openMemory(const uint8_t* data, size_t size, uint8_t* frameBuf, size_t frameCap);

    void close();
    bool isOpen() const { return _open; }
    bool fromMemory() const { return _open && _fromMemory; }

    /**
     * @brief 次の完全な1フレームをバッファ先頭に読み出す
     * @param outSize  フレーム長 (bytes)
     * @param wrapped  EOFに達して先頭へ巻き戻した場合 true
     */
    Status next(size_t& outSize, bool& wrapped);

    size_t fileBytes() const { return _fileBytes; }
    uint32_t loops() const { return _fromMemory ? _asmMem.loops() : _asm.loops(); }

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

    /// validate() のメモリ版 (PSRAM に読み込んだ後の検証。フラッシュを読み直さない)
    static bool validateMemory(const uint8_t* data, size_t size, uint16_t expectWidth,
                               uint16_t expectHeight, size_t maxFrameBytes, uint8_t* scratch,
                               size_t scratchCap, MjpegInfo& out, const char** errorOut);

private:
    /// LittleFS の File を MjpegAssembler の読み出し源として包む
    struct FileSource {
        fs::File* file;
        size_t read(uint8_t* dst, size_t maxBytes) { return file ? file->read(dst, maxBytes) : 0; }
        void rewind() { if (file) file->seek(0); }
    };

    template <typename Source>
    static bool validateWith(Source& src, size_t totalBytes, size_t readChunk, uint16_t expectWidth,
                             uint16_t expectHeight, size_t maxFrameBytes, uint8_t* scratch,
                             size_t scratchCap, MjpegInfo& out, const char** errorOut);

    fs::File _file;
    FileSource _src;
    mjpeg::Assembler<FileSource> _asm;
    mjpeg::MemorySource _memSrc;
    mjpeg::Assembler<mjpeg::MemorySource> _asmMem;
    size_t _fileBytes;
    bool _open;
    bool _fromMemory;
};

}  // namespace sastle

#endif  // __MJPEG_READER_H__
