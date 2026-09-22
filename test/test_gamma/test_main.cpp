#include <unity.h>
#include <math.h>
uint32_t g_fakeMillis = 0;
void setUp(void) {}
void tearDown(void) {}
#include "Gamma.h"
using namespace sastle;

void test_gamma_table(void) {
    // 現行実装の値で固定 (γ=2.2、1% 以上は最低 1)
    const uint8_t pct[] = {0, 1, 5, 10, 25, 44, 50, 75, 90, 100};
    const uint8_t led[] = {0, 1, 1, 2, 12, 42, 55, 135, 202, 255};
    for (unsigned i = 0; i < sizeof(pct); i++) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(led[i], brightnessPercentToLed(pct[i]), "gamma table");
    }
}

void test_gamma_monotonic_and_clamped(void) {
    uint8_t prev = 0;
    for (int p = 0; p <= 100; p++) {
        uint8_t v = brightnessPercentToLed((uint8_t)p);
        TEST_ASSERT_TRUE(v >= prev);
        prev = v;
    }
    TEST_ASSERT_EQUAL_UINT8(255, brightnessPercentToLed(200));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_gamma_table);
    RUN_TEST(test_gamma_monotonic_and_clamped);
    return UNITY_END();
}
