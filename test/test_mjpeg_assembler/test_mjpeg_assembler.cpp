/**
 * @file test_mjpeg_assembler.cpp
 * @brief MjpegAssembler.h (raw MJPEG のフレーム組み立て・先読み管理) の単体テスト
 *
 * 実行: pio test -e native
 *
 * 回帰: 実機で「next() が返したフレームの先頭が、次フレームの先読みバイトで上書きされる」
 * 不具合が出た (確定時に memmove していた)。ここでは next() 復帰後のバッファ内容が
 * 元のフレームと一致することを、様々な read チャンク幅で検証する。
 */
#include <unity.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "MjpegAssembler.h"
#include "../common/jpeg_synth.h"

using jpeg_synth::Bytes;
using sastle::mjpeg::Assembler;
using sastle::mjpeg::Status;

namespace {

/// メモリ上のバイト列を、指定チャンク幅以下で順次返す読み出し源
struct MemSource {
    const Bytes* data;
    size_t pos;
    size_t chunk;
    size_t reads;
    MemSource(const Bytes& d, size_t c) : data(&d), pos(0), chunk(c), reads(0) {}
    size_t read(uint8_t* dst, size_t maxBytes) {
        reads++;
        size_t n = data->size() - pos;
        if (n > maxBytes) n = maxBytes;
        if (n > chunk) n = chunk;
        if (n) memcpy(dst, data->data() + pos, n);
        pos += n;
        return n;
    }
    void rewind() { pos = 0; }
};

/// サイズの異なる 5 フレーム
std::vector<Bytes> frames() {
    std::vector<Bytes> v;
    v.push_back(jpeg_synth::makeFrame(320, 160, jpeg_synth::entropy(30, 1)));
    v.push_back(jpeg_synth::makeFrame(320, 160, jpeg_synth::entropy(500, 2)));
    v.push_back(jpeg_synth::makeFrame(320, 160, jpeg_synth::entropy(5, 3)));
    v.push_back(jpeg_synth::makeFrame(320, 160, jpeg_synth::entropy(1200, 4)));
    v.push_back(jpeg_synth::makeFrame(320, 160, jpeg_synth::entropy(77, 5)));
    return v;
}

Bytes concat(const std::vector<Bytes>& fs, size_t count) {
    Bytes s;
    for (size_t i = 0; i < count; i++) jpeg_synth::append(s, fs[i].data(), fs[i].size());
    return s;
}

const size_t kCap = 4096;

/// 2 ループ分 next() を回し、復帰後のバッファ内容が元フレームと一致することを確認する
void checkIntactForChunk(size_t chunk, size_t readChunk) {
    std::vector<Bytes> fs = frames();
    Bytes stream = concat(fs, fs.size());
    MemSource src(stream, chunk);
    std::vector<uint8_t> buf(kCap, 0xEE);
    Assembler<MemSource> a;
    a.begin(&src, buf.data(), buf.size(), readChunk, true);

    for (size_t i = 0; i < fs.size() * 2; i++) {
        size_t size = 0;
        bool wrapped = false;
        const Status st = a.next(size, wrapped);
        char msg[96];
        snprintf(msg, sizeof msg, "chunk=%u readChunk=%u frame=%u", (unsigned)chunk, (unsigned)readChunk, (unsigned)i);
        TEST_ASSERT_EQUAL_MESSAGE((int)Status::Ok, (int)st, msg);
        const Bytes& f = fs[i % fs.size()];
        TEST_ASSERT_EQUAL_MESSAGE(f.size(), size, msg);
        // 復帰後 (= 呼び出し側がデコードする時点) にフレームが無傷であること
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(f.data(), buf.data(), f.size(), msg);
        TEST_ASSERT_EQUAL_MESSAGE(i == fs.size(), wrapped, msg);
    }
    TEST_ASSERT_EQUAL(1, a.loops());
}

}  // namespace

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------

void test_frames_intact_after_return_small_chunks() {
    checkIntactForChunk(1, 2048);
    checkIntactForChunk(3, 2048);
    checkIntactForChunk(64, 2048);
}

void test_frames_intact_after_return_large_readahead() {
    // 1 回の read で複数フレーム分を先読みする (先読み量が最大になる) = 実機で壊れた条件
    checkIntactForChunk(100000, 100000);
    checkIntactForChunk(2048, 2048);
    checkIntactForChunk(700, 2048);
}

void test_readahead_bytes_preserved_behind_frame() {
    // 返したフレームの後ろに、次フレームの先頭がそのまま残っていること (寄せていない)
    std::vector<Bytes> fs = frames();
    Bytes stream = concat(fs, fs.size());
    MemSource src(stream, 100000);
    std::vector<uint8_t> buf(kCap, 0);
    Assembler<MemSource> a;
    a.begin(&src, buf.data(), buf.size(), 100000, true);
    size_t size = 0; bool wrapped = false;
    TEST_ASSERT_EQUAL((int)Status::Ok, (int)a.next(size, wrapped));
    TEST_ASSERT_EQUAL(fs[0].size(), size);
    TEST_ASSERT_TRUE(a.fill() > size);
    TEST_ASSERT_EQUAL_MEMORY(fs[1].data(), buf.data() + size, fs[1].size() < a.fill() - size ? fs[1].size() : a.fill() - size);
}

void test_truncated_tail_is_discarded_and_loops() {
    std::vector<Bytes> fs = frames();
    Bytes stream = concat(fs, 2);
    jpeg_synth::append(stream, fs[2].data(), fs[2].size() / 2);  // 途中で切れた 3 フレーム目
    MemSource src(stream, 50);
    std::vector<uint8_t> buf(kCap, 0);
    Assembler<MemSource> a;
    a.begin(&src, buf.data(), buf.size(), 2048, true);
    size_t size = 0; bool wrapped = false;
    TEST_ASSERT_EQUAL((int)Status::Ok, (int)a.next(size, wrapped)); TEST_ASSERT_FALSE(wrapped);
    TEST_ASSERT_EQUAL((int)Status::Ok, (int)a.next(size, wrapped)); TEST_ASSERT_FALSE(wrapped);
    TEST_ASSERT_EQUAL((int)Status::Ok, (int)a.next(size, wrapped));
    TEST_ASSERT_TRUE(wrapped);                                   // 切れた末尾を捨てて先頭へ
    TEST_ASSERT_EQUAL(fs[0].size(), size);
    TEST_ASSERT_EQUAL_MEMORY(fs[0].data(), buf.data(), size);
    TEST_ASSERT_EQUAL(1, a.loops());
}

void test_nonloop_reports_eof_and_truncated() {
    std::vector<Bytes> fs = frames();
    Bytes full = concat(fs, fs.size());
    {
        MemSource src(full, 128);
        std::vector<uint8_t> buf(kCap, 0);
        Assembler<MemSource> a;
        a.begin(&src, buf.data(), buf.size(), 2048, false);
        size_t size = 0; bool wrapped = false;
        for (size_t i = 0; i < fs.size(); i++) {
            TEST_ASSERT_EQUAL((int)Status::Ok, (int)a.next(size, wrapped));
            TEST_ASSERT_EQUAL_MEMORY(fs[i].data(), buf.data(), fs[i].size());
        }
        TEST_ASSERT_EQUAL((int)Status::Eof, (int)a.next(size, wrapped));
        TEST_ASSERT_EQUAL((int)Status::Eof, (int)a.next(size, wrapped));  // 再呼び出しも Eof
    }
    {
        Bytes cut = concat(fs, 1);
        jpeg_synth::append(cut, fs[1].data(), fs[1].size() - 3);
        MemSource src(cut, 128);
        std::vector<uint8_t> buf(kCap, 0);
        Assembler<MemSource> a;
        a.begin(&src, buf.data(), buf.size(), 2048, false);
        size_t size = 0; bool wrapped = false;
        TEST_ASSERT_EQUAL((int)Status::Ok, (int)a.next(size, wrapped));
        TEST_ASSERT_EQUAL((int)Status::Truncated, (int)a.next(size, wrapped));
    }
}

void test_too_large_frame() {
    std::vector<Bytes> fs = frames();
    Bytes stream = concat(fs, fs.size());
    MemSource src(stream, 2048);
    std::vector<uint8_t> buf(800, 0);      // fs[3] (~1300 bytes) は入らない
    Assembler<MemSource> a;
    a.begin(&src, buf.data(), buf.size(), 2048, true);
    size_t size = 0; bool wrapped = false;
    TEST_ASSERT_EQUAL((int)Status::Ok, (int)a.next(size, wrapped));
    TEST_ASSERT_EQUAL((int)Status::Ok, (int)a.next(size, wrapped));
    TEST_ASSERT_EQUAL((int)Status::Ok, (int)a.next(size, wrapped));
    TEST_ASSERT_EQUAL((int)Status::TooLarge, (int)a.next(size, wrapped));
}

void test_empty_and_corrupt_streams() {
    Bytes empty;
    {
        MemSource src(empty, 2048);
        std::vector<uint8_t> buf(kCap, 0);
        Assembler<MemSource> a;
        a.begin(&src, buf.data(), buf.size(), 2048, true);
        size_t size = 0; bool wrapped = false;
        TEST_ASSERT_EQUAL((int)Status::Empty, (int)a.next(size, wrapped));
    }
    {
        Bytes garbage(300, 0x12);
        MemSource src(garbage, 2048);
        std::vector<uint8_t> buf(kCap, 0);
        Assembler<MemSource> a;
        a.begin(&src, buf.data(), buf.size(), 2048, true);
        size_t size = 0; bool wrapped = false;
        TEST_ASSERT_EQUAL((int)Status::Corrupt, (int)a.next(size, wrapped));
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_frames_intact_after_return_small_chunks);
    RUN_TEST(test_frames_intact_after_return_large_readahead);
    RUN_TEST(test_readahead_bytes_preserved_behind_frame);
    RUN_TEST(test_truncated_tail_is_discarded_and_loops);
    RUN_TEST(test_nonloop_reports_eof_and_truncated);
    RUN_TEST(test_too_large_frame);
    RUN_TEST(test_empty_and_corrupt_streams);
    return UNITY_END();
}
