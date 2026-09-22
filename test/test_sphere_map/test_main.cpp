#include <unity.h>
#include <math.h>
uint32_t g_fakeMillis = 0;
void setUp(void) {}
void tearDown(void) {}
#include "SphereMap.h"
using namespace sastle;

void test_fast_atan2_matches_libm(void) {
    // 全象限の格子で atan2f/π と比較 (多項式近似の誤差 ~0.013° = 7e-5)
    for (int iy = -8; iy <= 8; iy++) for (int ix = -8; ix <= 8; ix++) {
        if (iy == 0 && ix == 0) continue;   // atan2(0,0) は未定義なので除外
        float y = iy * 0.25f, x = ix * 0.25f;
        float ref = atan2f(y, x) / 3.14159265f;
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, ref, _atan2(y, x));
    }
}

void test_fast_atan2_y0_xneg_is_180deg(void) {
    // 2026-09 修正: y==0 && x<0 は 180° (=1.0)。以前は象限補正のどちらにも入らず
    // 多項式の a(0)=-0.0083° が返って、その経線上の u が 1.0 でなく ~0.5 になっていた。
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, _atan2(0.0f, -1.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, _atan2(0.0f, -0.25f));
}

void test_to_uv_poles_and_equator(void) {
    float u, v;
    sphere::toUV(0, 0, 1, u, v);   TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, v);   // 北極 → 上端
    sphere::toUV(0, 0, -1, u, v);   TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, v);   // 南極 → 下端 (2026-09 修正)
    sphere::toUV(1, 0, 0, u, v);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.75f, u);   // 経度 +90° → (90+180)/360
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.5f, v);    // 赤道
    sphere::toUV(0, 1, 0, u, v);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.5f, u);    // 経度 0° → 中央
    sphere::toUV(0, 0, 0, u, v);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, u);    // 零ベクトルは中央
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, v);
}

void test_rotate_by_quaternion(void) {
    // Z 軸まわり +90°: (1,0,0) → (0,1,0)
    float x = 1, y = 0, z = 0;
    const float h = 0.70710678f;
    sphere::rotateByQuaternion(x, y, z, h, 0, 0, h);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, x);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, y);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, z);
    // 恒等回転は不変
    x = 0.3f; y = -0.4f; z = 0.866f;
    sphere::rotateByQuaternion(x, y, z, 1, 0, 0, 0);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.3f, x);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.4f, y);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fast_atan2_matches_libm);
    RUN_TEST(test_fast_atan2_y0_xneg_is_180deg);
    RUN_TEST(test_to_uv_poles_and_equator);
    RUN_TEST(test_rotate_by_quaternion);
    return UNITY_END();
}
