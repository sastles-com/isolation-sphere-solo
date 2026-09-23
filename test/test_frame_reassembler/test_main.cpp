#include <unity.h>
#include <string.h>
#include <vector>
uint32_t g_fakeMillis = 0;
void setUp(void) {}
void tearDown(void) {}
#include "FrameReassembler.h"
using namespace sastle;

static uint8_t g_buf[65507];

// 送信側 (server/app/services/video_streamer.py) と同じ 16B ヘッダでチャンクを作る
static std::vector<uint8_t> chunk(uint32_t frameId, uint16_t index, uint16_t count,
                                  const uint8_t* data, uint16_t size, uint32_t magic = UDP_IMAGE_MAGIC) {
    std::vector<uint8_t> d(sizeof(UDPChunkHeader) + size);
    UDPChunkHeader h;
    h.magic = magic;
    h.frame_id = frameId;
    h.chunk_index = index;
    h.chunk_count = count;
    h.chunk_size = size;
    h.reserved = 0;
    memcpy(d.data(), &h, sizeof(h));
    memcpy(d.data() + sizeof(h), data, size);
    return d;
}

static void fill(uint8_t* p, size_t n, uint8_t seed) {
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)(seed + i);
}

void test_header_is_16_bytes(void) {
    TEST_ASSERT_EQUAL_UINT32(16, sizeof(UDPChunkHeader));
    TEST_ASSERT_EQUAL_HEX32(0x4A504547, UDP_IMAGE_MAGIC);
}

void test_single_chunk_frame(void) {
    FrameReassembler r;
    r.begin(g_buf, sizeof(g_buf));
    uint8_t payload[300];
    fill(payload, sizeof(payload), 7);
    size_t out = 0;
    auto c = chunk(1, 0, 1, payload, sizeof(payload));
    TEST_ASSERT_TRUE(r.addChunk(c.data(), c.size(), out));
    TEST_ASSERT_EQUAL_UINT32(300, out);
    TEST_ASSERT_EQUAL_MEMORY(payload, g_buf, 300);
    TEST_ASSERT_EQUAL_UINT32(0, r.framesDropped());
}

void test_three_chunks_out_of_order(void) {
    FrameReassembler r;
    r.begin(g_buf, sizeof(g_buf));
    uint8_t p0[MAX_CHUNK_DATA], p1[MAX_CHUNK_DATA], p2[500];
    fill(p0, sizeof(p0), 1); fill(p1, sizeof(p1), 2); fill(p2, sizeof(p2), 3);
    size_t out = 0;
    auto c2 = chunk(42, 2, 3, p2, sizeof(p2));
    auto c0 = chunk(42, 0, 3, p0, sizeof(p0));
    auto c1 = chunk(42, 1, 3, p1, sizeof(p1));
    TEST_ASSERT_FALSE(r.addChunk(c2.data(), c2.size(), out));   // 最終チャンクが先に届く
    TEST_ASSERT_FALSE(r.addChunk(c0.data(), c0.size(), out));
    TEST_ASSERT_FALSE(r.addChunk(c0.data(), c0.size(), out));   // 重複は無視
    TEST_ASSERT_TRUE(r.addChunk(c1.data(), c1.size(), out));
    TEST_ASSERT_EQUAL_UINT32(2 * MAX_CHUNK_DATA + 500, out);
    TEST_ASSERT_EQUAL_MEMORY(p0, g_buf, MAX_CHUNK_DATA);
    TEST_ASSERT_EQUAL_MEMORY(p1, g_buf + MAX_CHUNK_DATA, MAX_CHUNK_DATA);
    TEST_ASSERT_EQUAL_MEMORY(p2, g_buf + 2 * MAX_CHUNK_DATA, 500);
}

void test_frame_id_change_drops_incomplete(void) {
    FrameReassembler r;
    r.begin(g_buf, sizeof(g_buf));
    uint8_t p[100];
    fill(p, sizeof(p), 9);
    size_t out = 0;
    auto a0 = chunk(10, 0, 2, p, sizeof(p));
    TEST_ASSERT_FALSE(r.addChunk(a0.data(), a0.size(), out));
    auto b0 = chunk(11, 0, 1, p, sizeof(p));               // 新フレーム → 10 は未完のまま破棄
    TEST_ASSERT_TRUE(r.addChunk(b0.data(), b0.size(), out));
    TEST_ASSERT_EQUAL_UINT32(1, r.framesDropped());
    TEST_ASSERT_EQUAL_UINT32(100, out);
}

void test_rejects_bad_header(void) {
    FrameReassembler r;
    r.begin(g_buf, sizeof(g_buf));
    uint8_t p[50];
    fill(p, sizeof(p), 1);
    size_t out = 0;
    auto badMagic = chunk(1, 0, 1, p, sizeof(p), 0xDEADBEEF);
    TEST_ASSERT_FALSE(r.addChunk(badMagic.data(), badMagic.size(), out));
    auto tooMany = chunk(1, 0, MAX_CHUNKS + 1, p, sizeof(p));
    TEST_ASSERT_FALSE(r.addChunk(tooMany.data(), tooMany.size(), out));
    auto idxOut = chunk(1, 3, 3, p, sizeof(p));
    TEST_ASSERT_FALSE(r.addChunk(idxOut.data(), idxOut.size(), out));
    auto truncated = chunk(1, 0, 1, p, sizeof(p));
    TEST_ASSERT_FALSE(r.addChunk(truncated.data(), truncated.size() - 10, out));  // chunk_size > 実長
    uint8_t tiny[8] = {0};
    TEST_ASSERT_FALSE(r.addChunk(tiny, sizeof(tiny), out));
    // 何も受理していないので、正しいフレームはそのまま完成する
    auto ok = chunk(1, 0, 1, p, sizeof(p));
    TEST_ASSERT_TRUE(r.addChunk(ok.data(), ok.size(), out));
}

void test_frame_id_all_ones_twice(void) {
    // 回帰: 旧実装は 0xFFFFFFFF を「未受信」センチネルに使っていて、送信側が同じ値を使うと
    // 2 枚目が前フレームの再デコードになっていた (停止時の黒フレームが効かなかった)
    FrameReassembler r;
    r.begin(g_buf, sizeof(g_buf));
    uint8_t a[64], b[64];
    fill(a, sizeof(a), 0x10); fill(b, sizeof(b), 0x80);
    size_t out = 0;
    auto ca = chunk(0xFFFFFFFFu, 0, 1, a, sizeof(a));
    auto cb = chunk(0xFFFFFFFFu, 0, 1, b, sizeof(b));
    TEST_ASSERT_TRUE(r.addChunk(ca.data(), ca.size(), out));
    TEST_ASSERT_EQUAL_MEMORY(a, g_buf, 64);
    TEST_ASSERT_TRUE(r.addChunk(cb.data(), cb.size(), out));
    TEST_ASSERT_EQUAL_MEMORY(b, g_buf, 64);
    TEST_ASSERT_EQUAL_UINT32(0, r.framesDropped());
}

void test_max_frame_fits_buffer(void) {
    FrameReassembler r;
    r.begin(g_buf, sizeof(g_buf));
    uint8_t p[MAX_CHUNK_DATA];
    fill(p, sizeof(p), 5);
    size_t out = 0;
    bool done = false;
    // 46 チャンク × 1400 = 64400 B < 65507
    for (uint16_t i = 0; i < MAX_CHUNKS; i++) {
        auto c = chunk(7, i, MAX_CHUNKS, p, sizeof(p));
        done = r.addChunk(c.data(), c.size(), out);
        TEST_ASSERT_EQUAL(i == MAX_CHUNKS - 1, done);
    }
    TEST_ASSERT_EQUAL_UINT32((uint32_t)MAX_CHUNKS * MAX_CHUNK_DATA, out);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_header_is_16_bytes);
    RUN_TEST(test_single_chunk_frame);
    RUN_TEST(test_three_chunks_out_of_order);
    RUN_TEST(test_frame_id_change_drops_incomplete);
    RUN_TEST(test_rejects_bad_header);
    RUN_TEST(test_frame_id_all_ones_twice);
    RUN_TEST(test_max_frame_fits_buffer);
    return UNITY_END();
}
