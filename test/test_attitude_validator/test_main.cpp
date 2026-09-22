#include <unity.h>
#include <math.h>
uint32_t g_fakeMillis = 0;
void setUp(void) {}
void tearDown(void) {}
#include "imu/AttitudeValidator.h"
using namespace sastle;

static Quat rotX(float rad) { return Quat{cosf(rad / 2), sinf(rad / 2), 0, 0}; }
static const Quat kIdentity{1, 0, 0, 0};
static const float kDeg = 0.0174532925f;

void test_norm_gate(void) {
    AttitudeValidator v;
    // ノルム² 0.81 (<0.97) → 棄却
    AttitudeVerdict r = v.evaluate(quatScaled(kIdentity, 0.9f), true, kIdentity, 0, true, 100);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.81f, r.n2);
    // ノルム² 0.9604 は窓 0.97〜1.03 の外 → 棄却 (実機の化けは 0.979〜0.994 に出る)
    r = v.evaluate(quatScaled(kIdentity, 0.98f), true, kIdentity, 0, true, 105);
    TEST_ASSERT_FALSE(r.ok);
    // ノルム² 0.9801 → 受理して正規化
    r = v.evaluate(quatScaled(kIdentity, 0.99f), true, kIdentity, 0, true, 110);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_FALSE(r.forced);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, r.q.w);
    // 読み失敗は常に棄却 (連続棄却にも数えない)
    r = v.evaluate(kIdentity, false, kIdentity, 0, true, 120);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_UINT8(0, v.consecutiveDiscards());
}

void test_continuity_rejects_jump_at_rest(void) {
    AttitudeValidator v;
    v.evaluate(kIdentity, true, kIdentity, 0, true, 1000);          // 基準を受理
    // 静止 (gyro 0) で 30° の飛び → maxAngle=15° → 棄却
    AttitudeVerdict r = v.evaluate(rotX(30 * kDeg), true, kIdentity, 0, true, 1010);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_UINT8(1, v.consecutiveDiscards());
    // 10° なら通る
    r = v.evaluate(rotX(10 * kDeg), true, kIdentity, 0, true, 1020);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_UINT8(0, v.consecutiveDiscards());
}

void test_continuity_allows_fast_rotation(void) {
    AttitudeValidator v;
    v.evaluate(kIdentity, true, kIdentity, 0, true, 1000);
    // gyro 300°/s, 前回受理から 150ms → maxAngle = 0.26 + 5.236*0.15*1.5 = 1.44rad (82°) → 30° 受理
    AttitudeVerdict r = v.evaluate(rotX(30 * kDeg), true, kIdentity, 300.0f, true, 1150);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_FALSE(r.forced);
    // aux OFF は 360°/s を仮定するので、静止で 30° でも dt=10ms なら 0.26+6.28*0.01*1.5=0.354rad(20°) → 棄却
    AttitudeValidator v2;
    v2.evaluate(kIdentity, true, kIdentity, 0, false, 1000);
    r = v2.evaluate(rotX(30 * kDeg), true, kIdentity, 0, false, 1010);
    TEST_ASSERT_FALSE(r.ok);
}

void test_dt_clamp(void) {
    AttitudeValidator v;
    v.evaluate(kIdentity, true, kIdentity, 0, true, 1000);
    // 5 秒空いても dt は 0.5 でクランプ: gyro 100°/s → 0.26 + 1.745*0.5*1.5 = 1.57rad (90°)
    AttitudeVerdict r = v.evaluate(rotX(80 * kDeg), true, kIdentity, 100.0f, true, 6000);
    TEST_ASSERT_TRUE(r.ok);
    r = v.evaluate(rotX(100 * kDeg), true, kIdentity, 100.0f, true, 11000);
    TEST_ASSERT_FALSE(r.ok);
}

void test_forced_accept_after_10_discards(void) {
    AttitudeValidator v;
    v.evaluate(kIdentity, true, kIdentity, 0, true, 1000);
    Quat far = rotX(30 * kDeg);
    for (int i = 0; i < 10; i++) {
        AttitudeVerdict r = v.evaluate(far, true, kIdentity, 0, true, 1010 + i * 10);
        TEST_ASSERT_FALSE(r.ok);
    }
    TEST_ASSERT_EQUAL_UINT8(10, v.consecutiveDiscards());
    AttitudeVerdict r = v.evaluate(far, true, kIdentity, 0, true, 1200);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.forced);
    TEST_ASSERT_EQUAL_UINT8(0, v.consecutiveDiscards());
}

void test_force_next_accept(void) {
    AttitudeValidator v;
    v.evaluate(kIdentity, true, kIdentity, 0, true, 1000);
    v.forceNextAccept();
    AttitudeVerdict r = v.evaluate(rotX(170 * kDeg), true, kIdentity, 0, true, 1010);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.forced);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_norm_gate);
    RUN_TEST(test_continuity_rejects_jump_at_rest);
    RUN_TEST(test_continuity_allows_fast_rotation);
    RUN_TEST(test_dt_clamp);
    RUN_TEST(test_forced_accept_after_10_discards);
    RUN_TEST(test_force_next_accept);
    return UNITY_END();
}
