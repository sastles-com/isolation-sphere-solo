#include <unity.h>
uint32_t g_fakeMillis = 0;
void setUp(void) {}
void tearDown(void) {}
#include "SourceArbiter.h"
using namespace sastle;
using Mode = SourceArbiter::Mode;
using Active = SourceArbiter::Active;

static SourceArbiter make(Mode m = Mode::Auto) {
    SourceArbiter a;
    a.configure(m, 2000, 100000);   // idle 2s, period 100ms
    return a;
}

void test_parse_and_names(void) {
    Mode m;
    TEST_ASSERT_TRUE(SourceArbiter::parseMode("auto", m));    TEST_ASSERT_EQUAL((int)Mode::Auto, (int)m);
    TEST_ASSERT_TRUE(SourceArbiter::parseMode("local", m));   TEST_ASSERT_EQUAL((int)Mode::Local, (int)m);
    TEST_ASSERT_TRUE(SourceArbiter::parseMode("network", m)); TEST_ASSERT_EQUAL((int)Mode::Network, (int)m);
    TEST_ASSERT_FALSE(SourceArbiter::parseMode("udp", m));
    TEST_ASSERT_FALSE(SourceArbiter::parseMode(nullptr, m));
    TEST_ASSERT_EQUAL_STRING("network", SourceArbiter::modeName(Mode::Network));
    TEST_ASSERT_EQUAL_STRING("none", SourceArbiter::activeName(Active::None));
}

void test_net_live_window_and_idle_edge(void) {
    SourceArbiter a = make();
    TEST_ASSERT_FALSE(a.netLive(1000));
    TEST_ASSERT_FALSE(a.takeNetIdleEdge(1000));          // 一度も live になっていない
    a.onNetFrame(1000);
    TEST_ASSERT_TRUE(a.netLive(1000));
    TEST_ASSERT_TRUE(a.netLive(2999));
    TEST_ASSERT_FALSE(a.netLive(3000));                  // idle_timeout ちょうどで idle
    TEST_ASSERT_FALSE(a.takeNetIdleEdge(2999));          // まだ live
    TEST_ASSERT_TRUE(a.takeNetIdleEdge(3000));           // 1 回だけ
    TEST_ASSERT_FALSE(a.takeNetIdleEdge(3100));
    a.onNetFrame(5000);                                  // 再び live → 再びエッジが出る
    TEST_ASSERT_TRUE(a.takeNetIdleEdge(7000));
}

void test_net_live_wraps_millis(void) {
    SourceArbiter a = make();
    a.onNetFrame(0xFFFFFF00u);
    TEST_ASSERT_TRUE(a.netLive(0x00000010u));            // millis 折り返し後 ~272ms
    TEST_ASSERT_FALSE(a.netLive(0x00000900u));
}

void test_local_mode_ignores_network(void) {
    SourceArbiter a = make(Mode::Local);
    a.onNetFrame(1000);
    TEST_ASSERT_FALSE(a.netLive(1000));
    TEST_ASSERT_TRUE(a.localAllowed(1000));
    TEST_ASSERT_EQUAL((int)Active::Local, (int)a.active(1000, true));
    TEST_ASSERT_EQUAL((int)Active::None, (int)a.active(1000, false));
}

void test_network_mode_blocks_local(void) {
    SourceArbiter a = make(Mode::Network);
    TEST_ASSERT_FALSE(a.localAllowed(1000));
    TEST_ASSERT_EQUAL((int)Active::None, (int)a.active(1000, true));
    a.onNetFrame(1000);
    TEST_ASSERT_EQUAL((int)Active::Network, (int)a.active(1500, true));
}

void test_auto_prefers_network_then_local(void) {
    SourceArbiter a = make();
    TEST_ASSERT_TRUE(a.localAllowed(0));
    TEST_ASSERT_EQUAL((int)Active::Local, (int)a.active(0, true));
    a.onNetFrame(100);
    TEST_ASSERT_FALSE(a.localAllowed(100));
    TEST_ASSERT_EQUAL((int)Active::Network, (int)a.active(100, true));
    TEST_ASSERT_TRUE(a.localAllowed(2100));              // 途切れたら local
}

void test_local_deadline_math(void) {
    SourceArbiter a = make();
    uint32_t missed = 99;
    a.rearm(1000000);                                    // next = 1.1s
    TEST_ASSERT_EQUAL_UINT32(100000, a.waitUs(1000000));
    TEST_ASSERT_EQUAL_UINT32(0, a.waitUs(1200000));
    TEST_ASSERT_FALSE(a.localDue(1099999, missed));
    TEST_ASSERT_EQUAL_UINT32(0, missed);
    // 定刻: miss 0、次は 1.2s
    TEST_ASSERT_TRUE(a.localDue(1100000, missed));
    TEST_ASSERT_EQUAL_UINT32(0, missed);
    TEST_ASSERT_EQUAL_UINT32(100000, a.waitUs(1100000));
    // 半周期遅れ: miss 0 (1 周期未満)、次は 1.3s に揃う
    TEST_ASSERT_TRUE(a.localDue(1250000, missed));
    TEST_ASSERT_EQUAL_UINT32(0, missed);
    TEST_ASSERT_EQUAL_UINT32(50000, a.waitUs(1250000));
    // 2.5 周期遅れ: miss 2、遅延を溜め込まない (次は now から 1 周期以内)
    TEST_ASSERT_TRUE(a.localDue(1550000, missed));
    TEST_ASSERT_EQUAL_UINT32(2, missed);
    TEST_ASSERT_TRUE(a.waitUs(1550000) <= 100000);
    TEST_ASSERT_EQUAL_UINT32(50000, a.waitUs(1550000));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_and_names);
    RUN_TEST(test_net_live_window_and_idle_edge);
    RUN_TEST(test_net_live_wraps_millis);
    RUN_TEST(test_local_mode_ignores_network);
    RUN_TEST(test_network_mode_blocks_local);
    RUN_TEST(test_auto_prefers_network_then_local);
    RUN_TEST(test_local_deadline_math);
    return UNITY_END();
}
