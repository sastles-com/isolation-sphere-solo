#include <unity.h>
#include <math.h>
uint32_t g_fakeMillis = 0;
void setUp(void) {}
void tearDown(void) {}
#include "imu/Bno055Codec.h"
using namespace sastle;
using namespace sastle::bno055;

void test_decode_le(void) {
    TEST_ASSERT_EQUAL_INT16(5784,  decodeLE(0x98, 0x16));
    TEST_ASSERT_EQUAL_INT16(-5784, decodeLE(0x68, 0xE9));
    TEST_ASSERT_EQUAL_INT16(-1,    decodeLE(0xFF, 0xFF));
    TEST_ASSERT_EQUAL_INT(16384, unitToLsb(1.0f));
    TEST_ASSERT_EQUAL_INT(-3408, unitToLsb(-3408.0f / 16384.0f));
}

void test_quat_from_raw(void) {
    // 2026-09-09 dump #4: 103E 39F3 8406 C8F9 = (+0.970,-0.200,+0.102,-0.097)
    const uint8_t b[8] = {0x10, 0x3E, 0x39, 0xF3, 0x84, 0x06, 0xC8, 0xF9};
    Quat q = quatFromRaw(b);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f,  0.970f, q.w);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -0.200f, q.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f,  0.102f, q.y);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -0.097f, q.z);
}

void test_word_implausible_msb_ff(void) {
    // dump #5: 真値 x=0xF2B0 (-3408) が 0xFFB2 (-78) で返った → 部分読み
    TEST_ASSERT_TRUE(wordImplausible(0xB2, 0xFF, -3408));
    // 本物の小さい値: 前回 -1 LSB、今回 -2 LSB → 正常
    TEST_ASSERT_FALSE(wordImplausible(0xFE, 0xFF, -1));
    // 前回 -700 LSB、今回 -78 LSB: 差 622 ≤ 800 → 通る (既知の取りこぼし、最大 ~5°)
    TEST_ASSERT_FALSE(wordImplausible(0xB2, 0xFF, -700));
    // ちょうど 800 は通り、801 で弾く
    TEST_ASSERT_FALSE(wordImplausible(0x00, 0x00, 800));
    TEST_ASSERT_TRUE(wordImplausible(0x00, 0x00, 801));
}

void test_word_implausible_msb_00_and_other(void) {
    TEST_ASSERT_TRUE(wordImplausible(0x10, 0x00, 5000));    // 0x0010=16, 前回 5000
    TEST_ASSERT_FALSE(wordImplausible(0x10, 0x00, 100));
    TEST_ASSERT_FALSE(wordImplausible(0x34, 0x12, -30000)); // MSB が 0xFF/0x00 でなければ常に妥当
}

void test_all_zero_and_tail(void) {
    const uint8_t z[8] = {0}; TEST_ASSERT_TRUE(allZero8(z));
    const uint8_t a[8] = {0, 0, 0, 0, 0, 0, 0, 1}; TEST_ASSERT_FALSE(allZero8(a));
    const uint8_t t[8] = {0x10, 0x3E, 0x39, 0xF3, 0x84, 0x06, 0xFF, 0xFF};
    TEST_ASSERT_TRUE(tailWordIs(t, 0xFF));
    TEST_ASSERT_FALSE(tailWordIs(t, 0x00));
}

void test_hex_encode(void) {
    const uint8_t in[3] = {0x0F, 0xA0, 0x7B};
    char out[7];
    TEST_ASSERT_EQUAL_size_t(6, hexEncode(in, 3, out));
    TEST_ASSERT_EQUAL_STRING("0FA07B", out);
}

void test_partial_tolerance_grows_with_time() {
    using namespace sastle::bno055;
    TEST_ASSERT_EQUAL(800, partialToleranceLsb(0));
    TEST_ASSERT_EQUAL(800, partialToleranceLsb(10));
    TEST_ASSERT_EQUAL(800 + 57 * 10, partialToleranceLsb(20));   // 受理なし 10ms 分だけ 400°/s 相当で伸びる
    TEST_ASSERT_EQUAL(2000, partialToleranceLsb(50));            // 31ms 以上経過で上限に当たる
    TEST_ASSERT_EQUAL(2000, partialToleranceLsb(10000));         // 上限
    // 前回 -3408、今回 -78 (0xFFB2): 差 3330 は上限 2000 を超えるので、経過時間に関わらず化け扱い
    TEST_ASSERT_TRUE(wordImplausible(0xB2, 0xFF, -3408, partialToleranceLsb(10)));
    TEST_ASSERT_TRUE(wordImplausible(0xB2, 0xFF, -3408, partialToleranceLsb(120)));
}

void test_norm_check_catches_msb_corruption() {
    using namespace sastle::bno055;
    // 健全 (実機ダンプ B の受理値、n²=1.0072 = 健全値の最大ずれ) → 通る
    const uint8_t good[8] = {0x59, 0x3B, 0x94, 0xF5, 0x49, 0x10, 0x28, 0x0F};
    TEST_ASSERT_FALSE(quatNormImplausible(good));
    // 化け (x の MSB が 0xFF、n²=0.981 = 化けの最小ずれ) → 弾く
    const uint8_t bad[8] = {0xCF, 0x3B, 0x22, 0xFF, 0x28, 0x0E, 0x7C, 0x0F};
    TEST_ASSERT_TRUE(quatNormImplausible(bad));
    // 全ゼロ埋め (400kHz で観測) → 弾く
    const uint8_t zeros[8] = {0xC4, 0x32, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_TRUE(quatNormImplausible(zeros));
}

void test_straddle_is_delta_based() {
    using namespace sastle::bno055;
    // 融合更新 1 回分の変化 (300 LSB) は同一サンプル扱い
    TEST_ASSERT_FALSE(wordsDisagree(0x00, 0x30, 0x2C, 0x31));   // 0x3000 → 0x312C (+300)
    // 符号反転 (q ↔ -q) は弾く
    TEST_ASSERT_TRUE(wordsDisagree(0x00, 0x30, 0x00, 0xD0));    // +12288 → -12288
    // MSB 欠落 (0x3000 → 0xFF00) は弾く
    TEST_ASSERT_TRUE(wordsDisagree(0x00, 0x30, 0x00, 0xFF));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_partial_tolerance_grows_with_time);
    RUN_TEST(test_straddle_is_delta_based);
    RUN_TEST(test_norm_check_catches_msb_corruption);
    RUN_TEST(test_decode_le);
    RUN_TEST(test_quat_from_raw);
    RUN_TEST(test_word_implausible_msb_ff);
    RUN_TEST(test_word_implausible_msb_00_and_other);
    RUN_TEST(test_all_zero_and_tail);
    RUN_TEST(test_hex_encode);
    return UNITY_END();
}
