#include <unity.h>
#include <math.h>
uint32_t g_fakeMillis = 0;
void setUp(void) {}
void tearDown(void) {}
#include "imu/QuatSmoother.h"
using namespace sastle;

static Quat rotX(float rad) { return Quat{cosf(rad / 2), sinf(rad / 2), 0, 0}; }
static float absDot(const Quat& a, const Quat& b) { return fabsf(quatDot(a, b)); }
static float norm(const Quat& q) { return sqrtf(quatDot(q, q)); }

void test_window_one_is_passthrough(void) {
    QuatSmoother s;                           // 既定 window=1
    for (int i = 0; i < 50; i++) {
        Quat q = rotX(i * 0.05f);
        Quat o = s.push(q);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, q.w, o.w);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, q.x, o.x);
    }
}

void test_constant_rate_lag_is_window_center(void) {
    QuatSmoother s; s.setWindow(5);
    const float step = 0.02f; Quat o;
    for (int i = 0; i < 200; i++) o = s.push(rotX(i * step));
    Quat expect = rotX((199 - 2.0f) * step);  // 窓の中心 = 2 サンプル前
    TEST_ASSERT_TRUE(absDot(o, expect) > 0.9999995f);
}

void test_sign_flips_do_not_break_average(void) {
    QuatSmoother s; s.setWindow(5);
    const float step = 0.02f; Quat o;
    for (int i = 0; i < 200; i++) {
        Quat q = rotX(i * step);
        if (i % 3 == 0) q = quatNegated(q);    // BNO055 の符号反転を模擬
        o = s.push(q);
    }
    Quat expect = rotX((199 - 2.0f) * step);
    TEST_ASSERT_TRUE(absDot(o, expect) > 0.9999995f);   // 符号合わせが無いと 0 付近に落ちる
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, norm(o));
}

void test_window_shrink_drops_old_samples(void) {
    QuatSmoother s; s.setWindow(50);
    Quat o;
    for (int i = 0; i < 50; i++) o = s.push(Quat{1, 0, 0, 0});
    s.setWindow(3);
    Quat tgt = rotX(1.0f);
    for (int i = 0; i < 3; i++) o = s.push(tgt);
    TEST_ASSERT_TRUE(absDot(o, tgt) > 0.9999995f);   // 恒等姿勢が残っていたらズレる
}

void test_window_clamp_and_reset(void) {
    QuatSmoother s;
    s.setWindow(0);   TEST_ASSERT_EQUAL_UINT8(1, s.window());
    s.setWindow(200); TEST_ASSERT_EQUAL_UINT8(QuatSmoother::kMax, s.window());
    s.push(rotX(0.5f)); s.push(rotX(0.5f));
    TEST_ASSERT_EQUAL_UINT8(2, s.count());
    s.reset();
    TEST_ASSERT_EQUAL_UINT8(0, s.count());
    // reset 後は前回値との符号合わせをしない (負の w もそのまま通る)
    Quat neg = quatNegated(rotX(0.5f));
    Quat o = s.push(neg);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, neg.w, o.w);
}

void test_wraparound_10000(void) {
    QuatSmoother s; s.setWindow(50); Quat o;
    for (int i = 0; i < 10000; i++) o = s.push(Quat{1, 0, 0, 0});
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, o.w);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, o.x);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_window_one_is_passthrough);
    RUN_TEST(test_constant_rate_lag_is_window_center);
    RUN_TEST(test_sign_flips_do_not_break_average);
    RUN_TEST(test_window_shrink_drops_old_samples);
    RUN_TEST(test_window_clamp_and_reset);
    RUN_TEST(test_wraparound_10000);
    return UNITY_END();
}
