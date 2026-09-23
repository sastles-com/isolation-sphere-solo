/**
 * @file MjpegReader.cpp
 * @brief MjpegReader実装
 */

#include "MjpegReader.h"

#include <string.h>

#include "JpegScan.h"

namespace sastle {

namespace {
/// 1回のファイル読み出し単位。大きすぎると1フレームあたりの先読みが増え、
/// 小さすぎると read 回数が増える。
constexpr size_t kReadChunk = 2048;
/// メモリ源の読み出し単位。memcpy (PSRAM→PSRAM) なので大きめでよいが、フレーム確定後に
/// 先読み分を先頭へ寄せる memmove が増えるので程々に。
constexpr size_t kMemReadChunk = 4096;
}  // namespace

bool MjpegReader::open(const char* path, uint8_t* frameBuf, size_t frameCap) {
    close();

    if (!path || !frameBuf || frameCap < 64) {
        return false;
    }

    _file = LittleFS.open(path, "r");
    if (!_file) {
        return false;
    }
    if (_file.isDirectory()) {
        _file.close();
        return false;
    }

    _fileBytes = _file.size();
    _src.file = &_file;
    _asm.begin(&_src, frameBuf, frameCap, kReadChunk, /*loop=*/true);
    _open = true;
    _fromMemory = false;
    return true;
}

bool MjpegReader::openMemory(const uint8_t* data, size_t size, uint8_t* frameBuf, size_t frameCap) {
    close();
    if (!data || size == 0 || !frameBuf || frameCap < 64) {
        return false;
    }
    _memSrc.data = data;
    _memSrc.size = size;
    _memSrc.pos = 0;
    _fileBytes = size;
    _asmMem.begin(&_memSrc, frameBuf, frameCap, kMemReadChunk, /*loop=*/true);
    _open = true;
    _fromMemory = true;
    return true;
}

void MjpegReader::close() {
    if (_open && !_fromMemory) {
        _file.close();
    }
    _open = false;
    _fromMemory = false;
    _asm.reset();
    _asmMem.reset();
    _memSrc = mjpeg::MemorySource();
    _fileBytes = 0;
}

MjpegReader::Status MjpegReader::next(size_t& outSize, bool& wrapped) {
    outSize = 0;
    wrapped = false;
    if (!_open) {
        return Status::NotOpen;
    }
    const mjpeg::Status st = _fromMemory ? _asmMem.next(outSize, wrapped) : _asm.next(outSize, wrapped);
    switch (st) {
        case mjpeg::Status::Ok:       return Status::Ok;
        case mjpeg::Status::Empty:    return Status::Empty;
        case mjpeg::Status::TooLarge: return Status::TooLarge;
        case mjpeg::Status::Corrupt:
        case mjpeg::Status::Eof:        // ループ再生では返らない
        case mjpeg::Status::Truncated:  // 同上
        default:                      return Status::Corrupt;
    }
}

template <typename Source>
bool MjpegReader::validateWith(Source& src, size_t totalBytes, size_t readChunk, uint16_t expectWidth,
                               uint16_t expectHeight, size_t maxFrameBytes, uint8_t* scratch,
                               size_t scratchCap, MjpegInfo& out, const char** errorOut) {
    out = MjpegInfo();
    const char* err = nullptr;

    if (!scratch || scratchCap < 64) {
        if (errorOut) *errorOut = "internal: invalid scan buffer";
        return false;
    }
    const size_t cap = (maxFrameBytes < scratchCap) ? maxFrameBytes : scratchCap;
    out.fileBytes = totalBytes;

    mjpeg::Assembler<Source> asmb;
    asmb.begin(&src, scratch, cap, readChunk, /*loop=*/false);
    for (;;) {
        size_t len = 0;
        bool wrapped = false;
        const mjpeg::Status st = asmb.next(len, wrapped);
        if (st == mjpeg::Status::Ok) {
            const jpegscan::Info& info = asmb.lastInfo();
            if (!info.baseline) {
                err = "not a baseline JPEG (progressive/lossless is unsupported)";
                break;
            }
            if (info.width != expectWidth || info.height != expectHeight) {
                err = "resolution mismatch";
                out.width = info.width;
                out.height = info.height;
                break;
            }
            out.frames++;
            out.width = info.width;
            out.height = info.height;
            if (info.length > out.maxFrameBytes) {
                out.maxFrameBytes = info.length;
            }
            continue;
        }
        if (st == mjpeg::Status::Eof) {
            break;  // 正常終了
        }
        err = (st == mjpeg::Status::Truncated) ? "truncated frame at end of file"
            : (st == mjpeg::Status::TooLarge)  ? "frame exceeds size limit"
                                               : "corrupt JPEG structure";
        break;
    }

    if (!err && out.frames == 0) {
        err = "no JPEG frame found";
    }
    if (errorOut) {
        *errorOut = err;
    }
    return err == nullptr;
}

bool MjpegReader::validate(const char* path, uint16_t expectWidth, uint16_t expectHeight,
                           size_t maxFrameBytes, uint8_t* scratch, size_t scratchCap,
                           MjpegInfo& out, const char** errorOut) {
    out = MjpegInfo();
    if (!path) {
        if (errorOut) *errorOut = "internal: no path";
        return false;
    }
    fs::File f = LittleFS.open(path, "r");
    if (!f || f.isDirectory()) {
        if (f) f.close();
        if (errorOut) *errorOut = "file not found";
        return false;
    }
    FileSource src{&f};
    const bool ok = validateWith(src, f.size(), kReadChunk, expectWidth, expectHeight, maxFrameBytes,
                                 scratch, scratchCap, out, errorOut);
    f.close();
    return ok;
}

bool MjpegReader::validateMemory(const uint8_t* data, size_t size, uint16_t expectWidth,
                                 uint16_t expectHeight, size_t maxFrameBytes, uint8_t* scratch,
                                 size_t scratchCap, MjpegInfo& out, const char** errorOut) {
    out = MjpegInfo();
    if (!data || size == 0) {
        if (errorOut) *errorOut = "no JPEG frame found";
        return false;
    }
    mjpeg::MemorySource src;
    src.data = data;
    src.size = size;
    return validateWith(src, size, kMemReadChunk, expectWidth, expectHeight, maxFrameBytes,
                        scratch, scratchCap, out, errorOut);
}

}  // namespace sastle
