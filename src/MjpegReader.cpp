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
    return true;
}

void MjpegReader::close() {
    if (_open) {
        _file.close();
        _open = false;
    }
    _asm.reset();
    _fileBytes = 0;
}

MjpegReader::Status MjpegReader::next(size_t& outSize, bool& wrapped) {
    outSize = 0;
    wrapped = false;
    if (!_open) {
        return Status::NotOpen;
    }
    switch (_asm.next(outSize, wrapped)) {
        case mjpeg::Status::Ok:       return Status::Ok;
        case mjpeg::Status::Empty:    return Status::Empty;
        case mjpeg::Status::TooLarge: return Status::TooLarge;
        case mjpeg::Status::Corrupt:
        case mjpeg::Status::Eof:        // ループ再生では返らない
        case mjpeg::Status::Truncated:  // 同上
        default:                      return Status::Corrupt;
    }
}

bool MjpegReader::validate(const char* path, uint16_t expectWidth, uint16_t expectHeight,
                           size_t maxFrameBytes, uint8_t* scratch, size_t scratchCap,
                           MjpegInfo& out, const char** errorOut) {
    out = MjpegInfo();
    const char* err = nullptr;

    if (!path || !scratch || scratchCap < 64) {
        if (errorOut) *errorOut = "internal: invalid scan buffer";
        return false;
    }
    size_t cap = (maxFrameBytes < scratchCap) ? maxFrameBytes : scratchCap;

    fs::File f = LittleFS.open(path, "r");
    if (!f || f.isDirectory()) {
        if (f) f.close();
        if (errorOut) *errorOut = "file not found";
        return false;
    }
    out.fileBytes = f.size();

    FileSource src{&f};
    mjpeg::Assembler<FileSource> asmb;
    asmb.begin(&src, scratch, cap, kReadChunk, /*loop=*/false);
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

    f.close();

    if (!err && out.frames == 0) {
        err = "no JPEG frame found";
    }
    if (errorOut) {
        *errorOut = err;
    }
    return err == nullptr;
}

}  // namespace sastle
