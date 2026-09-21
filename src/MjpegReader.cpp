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

    _buf = frameBuf;
    _cap = frameCap;
    _fill = 0;
    _fileBytes = _file.size();
    _loops = 0;
    _open = true;
    return true;
}

void MjpegReader::close() {
    if (_open) {
        _file.close();
        _open = false;
    }
    _buf = nullptr;
    _cap = 0;
    _fill = 0;
    _fileBytes = 0;
}

size_t MjpegReader::fillMore() {
    if (!_open || _fill >= _cap) {
        return 0;
    }
    size_t want = _cap - _fill;
    if (want > kReadChunk) {
        want = kReadChunk;
    }
    const size_t got = _file.read(_buf + _fill, want);
    _fill += got;
    return got;
}

MjpegReader::Status MjpegReader::next(size_t& outSize, bool& wrapped) {
    outSize = 0;
    wrapped = false;
    if (!_open) {
        return Status::NotOpen;
    }

    int wraps = 0;
    for (;;) {
        jpegscan::Info info;
        const jpegscan::Result r = jpegscan::parse(_buf, _fill, info);

        if (r == jpegscan::Result::Ok) {
            outSize = info.length;
            // 先読みしてしまった次フレーム分をバッファ先頭へ寄せる
            if (_fill > info.length) {
                memmove(_buf, _buf + info.length, _fill - info.length);
            }
            _fill -= info.length;
            return Status::Ok;
        }
        if (r == jpegscan::Result::Invalid) {
            return Status::Corrupt;
        }

        // NeedMore: 追記読みして再判定
        if (_fill >= _cap) {
            return Status::TooLarge;
        }
        if (fillMore() > 0) {
            continue;
        }

        // EOF に到達
        if (wraps >= 1) {
            // 巻き戻した直後にまたEOF = 有効フレームを含まない
            return (_fill == 0) ? Status::Empty : Status::Corrupt;
        }
        if (_fill > 0) {
            // 末尾に完結しないフレームが残っている = 途中で切れたファイル。
            // 破棄して先頭へ戻り、再生自体は継続させる。
            _fill = 0;
        }
        wraps++;
        wrapped = true;
        _loops++;
        _file.seek(0);
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

    size_t fill = 0;
    for (;;) {
        jpegscan::Info info;
        const jpegscan::Result r = jpegscan::parse(scratch, fill, info);

        if (r == jpegscan::Result::Ok) {
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
            if (fill > info.length) {
                memmove(scratch, scratch + info.length, fill - info.length);
            }
            fill -= info.length;
            continue;
        }
        if (r == jpegscan::Result::Invalid) {
            err = "corrupt JPEG structure";
            break;
        }

        // NeedMore
        if (fill >= cap) {
            err = "frame exceeds size limit";
            break;
        }
        size_t want = cap - fill;
        if (want > kReadChunk) {
            want = kReadChunk;
        }
        const size_t got = f.read(scratch + fill, want);
        if (got == 0) {
            if (fill != 0) {
                err = "truncated frame at end of file";
            }
            break;  // fill==0 なら正常終了
        }
        fill += got;
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
