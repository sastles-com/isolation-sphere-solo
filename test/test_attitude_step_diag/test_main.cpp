#include <unity.h>
#include <math.h>
uint32_t g_fakeMillis = 0;
void setUp(void) {}
void tearDown(void) {}
#include "imu/AttitudeStepDiag.h"
using namespace sastle;

void test_identity_then_rotation(void) {
    AttitudeStepDiag d; float ortho = 0, norm = 0, step = 0;
    d.observe(1, 0, 0, 0, ortho, norm, step);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, ortho);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, norm);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, step);
    // 30° about X: 正規な回転なので ortho/norm は 0、step は 30°
    const float h = 15.0f * 0.0174532925f;
    d.observe(cosf(h), sinf(h), 0, 0, ortho, norm, step);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, ortho);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.0f, norm);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 30.0f, step);
    // 符号反転 (-q) は同じ回転: step は増えない
    d.observe(-cosf(h), -sinf(h), 0, 0, ortho, norm, step);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 30.0f, step);
}

void test_broken_quat_is_detected(void) {
    AttitudeStepDiag d; float ortho = 0, norm = 0, step = 0;
    d.observe(0.9f, 0.0f, 0.0f, 0.0f, ortho, norm, step);   // 非正規化
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.19f, norm);
    // 非単位 quat は回転行列が直交しない → ortho > 0
    float o2 = 0, n2 = 0, s2 = 0;
    AttitudeStepDiag d2;
    d2.observe(0.8f, 0.5f, 0.3f, 0.0f, o2, n2, s2);
    TEST_ASSERT_TRUE(o2 > 0.01f);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_identity_then_rotation);
    RUN_TEST(test_broken_quat_is_detected);
    return UNITY_END();
}
