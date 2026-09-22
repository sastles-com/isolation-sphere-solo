#include <unity.h>
#include <string.h>
uint32_t g_fakeMillis = 0;
void setUp(void) {}
void tearDown(void) {}
#include "imu/ImuDiag.h"
using namespace sastle;

void test_raw_slot_keeps_first_until_taken(void) {
    RawSlot<8> s;
    const uint8_t a[8] = {1,2,3,4,5,6,7,8}, b[8] = {9,9,9,9,9,9,9,9};
    uint8_t out[8];
    TEST_ASSERT_FALSE(s.take(out));
    s.offer(a); s.offer(b);                 // 2 件目は捨てられる (未回収の 1 件目を保持)
    TEST_ASSERT_TRUE(s.take(out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a, out, 8);
    TEST_ASSERT_FALSE(s.take(out));
    s.offer(b);
    TEST_ASSERT_TRUE(s.take(out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b, out, 8);
}

void test_discard_vec_event_reset_slots(void) {
    DiscardSlot d; uint8_t out[8]; float n2 = 0;
    const uint8_t raw[8] = {0x58,0xE9,0xB5,0xFF,0x13,0x04,0xDF,0xD6};
    d.offer(raw, 0.543f);
    TEST_ASSERT_TRUE(d.take(out, n2));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.543f, n2);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(raw, out, 8);

    VecPartialSlot v; uint8_t reg = 0, o6[6];
    const uint8_t r6[6] = {1,2,3,4,0xFF,0xFF};
    v.offer(0x1A, r6);
    TEST_ASSERT_TRUE(v.take(reg, o6));
    TEST_ASSERT_EQUAL_UINT8(0x1A, reg);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(r6, o6, 6);

    EventSlot e;
    TEST_ASSERT_FALSE(e.take()); e.raise(); TEST_ASSERT_TRUE(e.take()); TEST_ASSERT_FALSE(e.take());

    ResetSlot rs; bool ok = false, au = false;
    rs.offer(true, false); rs.offer(false, true);   // 最新を優先
    TEST_ASSERT_TRUE(rs.take(ok, au));
    TEST_ASSERT_FALSE(ok); TEST_ASSERT_TRUE(au);
}

void test_dump_ring_record_format_and_lines(void) {
    RawDumpRing ring; uint8_t buf[RawDumpRing::kRec * 10];
    TEST_ASSERT_TRUE(ring.arm(buf, 10));
    TEST_ASSERT_FALSE(ring.arm(buf, 10));            // armed 中は拒否
    const uint8_t raw[8] = {0x10,0x3E,0x39,0xF3,0x84,0x06,0xC8,0xF9};
    ring.record(raw, RawDumpRing::kFlagReadOk | RawDumpRing::kFlagAccepted, 1, 1000);
    ring.record(raw, RawDumpRing::kFlagReadOk, 0, 1010);
    TEST_ASSERT_EQUAL_UINT16(2, ring.count());
    // レコード 0: dt=0 (先頭)、レコード 1: dt=10
    TEST_ASSERT_EQUAL_UINT8(3, buf[8]);  TEST_ASSERT_EQUAL_UINT8(1, buf[9]);
    TEST_ASSERT_EQUAL_UINT8(0, buf[10]); TEST_ASSERT_EQUAL_UINT8(0, buf[11]);
    TEST_ASSERT_EQUAL_UINT8(1, buf[12 + 8]); TEST_ASSERT_EQUAL_UINT8(10, buf[12 + 10]);
    TEST_ASSERT_FALSE(ring.ready());                 // まだ 10 に達していない
    for (int i = 0; i < 8; i++) ring.record(raw, 1, 0, 1020 + i * 10);
    TEST_ASSERT_TRUE(ring.ready());
    TEST_ASSERT_FALSE(ring.armed());
    ring.record(raw, 1, 0, 2000);                    // 満杯後は無視
    TEST_ASSERT_EQUAL_UINT16(10, ring.count());

    char line[RawDumpRing::kSamplesPerLine * RawDumpRing::kRec * 2 + 1]; uint16_t idx0 = 99;
    TEST_ASSERT_TRUE(ring.takeLine(line, sizeof(line), idx0));
    TEST_ASSERT_EQUAL_UINT16(0, idx0);
    TEST_ASSERT_EQUAL_size_t(8 * 12 * 2, strlen(line));
    TEST_ASSERT_EQUAL_STRING_LEN("103E39F38406C8F9030100", line, 22);   // raw + flags + straddles + dt
    TEST_ASSERT_TRUE(ring.takeLine(line, sizeof(line), idx0));
    TEST_ASSERT_EQUAL_UINT16(8, idx0);
    TEST_ASSERT_EQUAL_size_t(2 * 12 * 2, strlen(line));
    TEST_ASSERT_FALSE(ring.takeLine(line, sizeof(line), idx0));
    TEST_ASSERT_TRUE(ring.exhausted());
    ring.disarm();
    TEST_ASSERT_FALSE(ring.ready());
    TEST_ASSERT_NULL(ring.buffer());
    // 小さすぎる出力バッファは拒否 (書き込まない)
    RawDumpRing r2; r2.arm(buf, 1); r2.record(raw, 1, 0, 5);
    char tiny[4];
    TEST_ASSERT_FALSE(r2.takeLine(tiny, sizeof(tiny), idx0));
}

void test_counters_are_plain_uint32(void) {
    ImuCounters c;
    c.readTotal++; c.partialReads += 2;
    TEST_ASSERT_EQUAL_UINT32(1, c.readTotal);
    TEST_ASSERT_EQUAL_UINT32(2, c.partialReads);
    TEST_ASSERT_EQUAL_UINT32(0, c.straddles);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_raw_slot_keeps_first_until_taken);
    RUN_TEST(test_discard_vec_event_reset_slots);
    RUN_TEST(test_dump_ring_record_format_and_lines);
    RUN_TEST(test_counters_are_plain_uint32);
    return UNITY_END();
}
