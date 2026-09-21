/**
 * @file test_jpeg_scan.cpp
 * @brief JpegScan.h (raw MJPEG フレーム境界パーサー) の単体テスト
 *
 * 実行: pio test -e native_solo
 *
 * 合成した最小 JPEG バイト列でマーカー追跡の境界条件を検証する。
 * 実デコードはしない (TJpg_Decoder は対象外)。
 */

#include <unity.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "JpegScan.h"

using sastle::jpegscan::Info;
using sastle::jpegscan::Result;
using sastle::jpegscan::parse;

namespace {

typedef std::vector<uint8_t> Bytes;

void append(Bytes& b, const uint8_t* p, size_t n) { b.insert(b.end(), p, p + n); }

void marker(Bytes& b, uint8_t m) {
    b.push_back(0xFF);
    b.push_back(m);
}

/// [FF m][len hi][len lo][payload...]  (len はペイロード+2)
void segment(Bytes& b, uint8_t m, const Bytes& payload) {
    marker(b, m);
    const size_t len = payload.size() + 2;
    b.push_back((uint8_t)(len >> 8));
    b.push_back((uint8_t)(len & 0xFF));
    append(b, payload.data(), payload.size());
}

Bytes sofPayload(uint16_t w, uint16_t h) {
    // precision, height, width, ncomp=1, (id, sampling, qtable)
    Bytes p;
    p.push_back(8);
    p.push_back((uint8_t)(h >> 8)); p.push_back((uint8_t)(h & 0xFF));
    p.push_back((uint8_t)(w >> 8)); p.push_back((uint8_t)(w & 0xFF));
    p.push_back(1);
    p.push_back(1); p.push_back(0x11); p.push_back(0);
    return p;
}

Bytes sosPayload() {
    Bytes p;
    p.push_back(1);            // ncomp
    p.push_back(1); p.push_back(0x00);  // comp id, tables
    p.push_back(0); p.push_back(63); p.push_back(0);  // Ss, Se, Ah/Al
    return p;
}

/// 標準的な1フレーム: SOI, APP0, DQT, SOF0, DHT, SOS, entropy, EOI
Bytes makeFrame(uint16_t w, uint16_t h, const Bytes& entropy, uint8_t sofMarker = 0xC0,
                const Bytes* app1 = nullptr) {
    Bytes f;
    marker(f, 0xD8);
    segment(f, 0xE0, Bytes{'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0});
    if (app1) {
        segment(f, 0xE1, *app1);
    }
    segment(f, 0xDB, Bytes(65, 0x01));           // DQT (dummy)
    segment(f, sofMarker, sofPayload(w, h));
    segment(f, 0xC4, Bytes(17, 0x00));           // DHT (dummy)
    segment(f, 0xDA, sosPayload());
    append(f, entropy.data(), entropy.size());
    marker(f, 0xD9);
    return f;
}

/// バイトスタッフィング (FF00) と RST マーカーを含むエントロピーデータ
Bytes trickyEntropy() {
    Bytes e;
    // 実エントロピーデータには 0xFF 単独は現れない (必ず FF00 でスタッフされる) ので
    // 疑似乱数列も 0x00..0x7F に収める
    for (int i = 0; i < 40; i++) e.push_back((uint8_t)((i * 7 + 3) & 0x7F));
    e.push_back(0xFF); e.push_back(0x00);        // stuffed 0xFF
    for (int i = 0; i < 10; i++) e.push_back(0x55);
    e.push_back(0xFF); e.push_back(0xD3);        // RST3
    for (int i = 0; i < 10; i++) e.push_back(0xAA);
    e.push_back(0xFF); e.push_back(0x00);
    e.push_back(0xFF); e.push_back(0x00);        // 連続スタッフ
    for (int i = 0; i < 5; i++) e.push_back(0x12);
    return e;
}

}  // namespace

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------

void test_complete_frame_ok() {
    Bytes f = makeFrame(320, 160, trickyEntropy());
    Info info;
    TEST_ASSERT_EQUAL(Result::Ok, parse(f.data(), f.size(), info));
    TEST_ASSERT_EQUAL_UINT32(f.size(), info.length);
    TEST_ASSERT_EQUAL_UINT16(320, info.width);
    TEST_ASSERT_EQUAL_UINT16(160, info.height);
    TEST_ASSERT_TRUE(info.baseline);
}

void test_every_prefix_needs_more() {
    // 完全フレームのあらゆる途中切れは NeedMore (Ok にも Invalid にもならない)
    Bytes f = makeFrame(320, 160, trickyEntropy());
    for (size_t n = 0; n < f.size(); n++) {
        Info info;
        Result r = parse(f.data(), n, info);
        if (r != Result::NeedMore) {
            char msg[64];
            snprintf(msg, sizeof(msg), "prefix len=%u gave %d", (unsigned)n, (int)r);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

void test_concatenated_frames_split_correctly() {
    Bytes a = makeFrame(320, 160, trickyEntropy());
    Bytes b = makeFrame(320, 160, Bytes(30, 0x33));
    Bytes both = a;
    append(both, b.data(), b.size());

    Info info;
    TEST_ASSERT_EQUAL(Result::Ok, parse(both.data(), both.size(), info));
    TEST_ASSERT_EQUAL_UINT32(a.size(), info.length);

    Info info2;
    TEST_ASSERT_EQUAL(Result::Ok, parse(both.data() + a.size(), both.size() - a.size(), info2));
    TEST_ASSERT_EQUAL_UINT32(b.size(), info2.length);
}

void test_eoi_inside_app_segment_is_not_frame_end() {
    // EXIF サムネイルのように APP1 内に FFD8..FFD9 を含むケース。線形探索なら誤検出する。
    Bytes app1;
    const uint8_t exifHdr[] = {'E', 'x', 'i', 'f', 0, 0};
    append(app1, exifHdr, sizeof(exifHdr));
    Bytes thumb = makeFrame(16, 8, Bytes(6, 0x77));
    append(app1, thumb.data(), thumb.size());

    Bytes f = makeFrame(320, 160, trickyEntropy(), 0xC0, &app1);
    Info info;
    TEST_ASSERT_EQUAL(Result::Ok, parse(f.data(), f.size(), info));
    TEST_ASSERT_EQUAL_UINT32(f.size(), info.length);
    TEST_ASSERT_EQUAL_UINT16(320, info.width);  // 外側の SOF0 を採用する
}

void test_not_soi_is_invalid() {
    const uint8_t junk[] = {0x00, 0x11, 0x22, 0x33};
    Info info;
    TEST_ASSERT_EQUAL(Result::Invalid, parse(junk, sizeof(junk), info));

    const uint8_t halfSoi[] = {0xFF, 0xD9, 0xFF, 0xD8};
    TEST_ASSERT_EQUAL(Result::Invalid, parse(halfSoi, sizeof(halfSoi), info));
}

void test_progressive_reports_not_baseline() {
    Bytes f = makeFrame(320, 160, Bytes(20, 0x10), 0xC2);
    Info info;
    TEST_ASSERT_EQUAL(Result::Ok, parse(f.data(), f.size(), info));
    TEST_ASSERT_FALSE(info.baseline);
    TEST_ASSERT_EQUAL_UINT16(320, info.width);
}

void test_garbage_between_segments_is_invalid() {
    Bytes f;
    marker(f, 0xD8);
    segment(f, 0xE0, Bytes(4, 0));
    f.push_back(0x42);  // マーカー位置に 0xFF 以外
    segment(f, 0xDB, Bytes(4, 0));
    Info info;
    TEST_ASSERT_EQUAL(Result::Invalid, parse(f.data(), f.size(), info));
}

void test_eoi_without_sos_is_invalid() {
    Bytes f;
    marker(f, 0xD8);
    segment(f, 0xC0, sofPayload(320, 160));
    marker(f, 0xD9);
    Info info;
    TEST_ASSERT_EQUAL(Result::Invalid, parse(f.data(), f.size(), info));
}

void test_nested_soi_is_invalid() {
    // 前フレームの EOI を欠いたまま次フレームの SOI が来た
    Bytes f;
    marker(f, 0xD8);
    segment(f, 0xE0, Bytes(4, 0));
    marker(f, 0xD8);
    Info info;
    TEST_ASSERT_EQUAL(Result::Invalid, parse(f.data(), f.size(), info));
}

void test_fill_bytes_before_marker_ok() {
    Bytes f;
    marker(f, 0xD8);
    f.push_back(0xFF); f.push_back(0xFF);  // フィルバイト
    segment(f, 0xC0, sofPayload(320, 160));
    segment(f, 0xDA, sosPayload());
    append(f, Bytes(8, 0x01).data(), 8);
    f.push_back(0xFF);                     // エントロピー末尾のフィル
    marker(f, 0xD9);
    Info info;
    TEST_ASSERT_EQUAL(Result::Ok, parse(f.data(), f.size(), info));
    TEST_ASSERT_EQUAL_UINT32(f.size(), info.length);
}

void test_zero_length_segment_is_invalid() {
    Bytes f;
    marker(f, 0xD8);
    marker(f, 0xE0);
    f.push_back(0x00); f.push_back(0x01);  // len=1 (<2)
    Info info;
    TEST_ASSERT_EQUAL(Result::Invalid, parse(f.data(), f.size(), info));
}

void test_standalone_markers_between_segments_ok() {
    // TEM(01) と RSTn はセグメント長を持たない
    Bytes f;
    marker(f, 0xD8);
    marker(f, 0x01);
    marker(f, 0xD0);
    segment(f, 0xC0, sofPayload(320, 160));
    segment(f, 0xDA, sosPayload());
    append(f, Bytes(8, 0x01).data(), 8);
    marker(f, 0xD9);
    Info info;
    TEST_ASSERT_EQUAL(Result::Ok, parse(f.data(), f.size(), info));
    TEST_ASSERT_EQUAL_UINT32(f.size(), info.length);
}

void test_null_buffer_is_invalid() {
    Info info;
    TEST_ASSERT_EQUAL(Result::Invalid, parse(nullptr, 100, info));
}

// ---------------------------------------------------------------------------

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_complete_frame_ok);
    RUN_TEST(test_every_prefix_needs_more);
    RUN_TEST(test_concatenated_frames_split_correctly);
    RUN_TEST(test_eoi_inside_app_segment_is_not_frame_end);
    RUN_TEST(test_not_soi_is_invalid);
    RUN_TEST(test_progressive_reports_not_baseline);
    RUN_TEST(test_garbage_between_segments_is_invalid);
    RUN_TEST(test_eoi_without_sos_is_invalid);
    RUN_TEST(test_nested_soi_is_invalid);
    RUN_TEST(test_fill_bytes_before_marker_ok);
    RUN_TEST(test_zero_length_segment_is_invalid);
    RUN_TEST(test_standalone_markers_between_segments_ok);
    RUN_TEST(test_null_buffer_is_invalid);
    return UNITY_END();
}
