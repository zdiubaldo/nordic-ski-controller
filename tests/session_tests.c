#include "test_session.h"
#include <math.h>
#ifdef ESP_PLATFORM
#include "unity.h"
#define CHECK(x) TEST_ASSERT_TRUE(x)
#else
#include <assert.h>
#include <stdio.h>
#define CHECK(x) assert(x)
#endif
static void ownership_and_replay(void)
{
    test_session_t s;
    test_session_init(&s, false);
    CHECK(!test_session_claim(&s, 0, 1));
    test_session_init(&s, true);
    CHECK(!test_session_claim(&s, 0, 0));
    CHECK(test_session_claim(&s, 0, 123));
    CHECK(!test_session_claim(&s, 1, 456));
    CHECK(!test_session_command(&s, 1, 456, 1, TEST_START, 0, 0));
    CHECK(test_session_command(&s, 2, 123, 1, TEST_START, 0, 0));
    CHECK(!test_session_command(&s, 3, 123, 1, TEST_STOP, 0, 0));
    CHECK(test_session_command(&s, 4, 123, 2, TEST_TARGETS, 2, 4));
    CHECK(!test_session_command(&s, 5, 123, 3, TEST_TARGETS, NAN, 4));
    CHECK(s.control.requested_speed_mps == 2 && s.last_seen_us == 4);
    CHECK(!test_session_command(&s, 6, 123, 3, TEST_STOP, 0, 0));
}
static void disconnect_and_restart(void)
{
    test_session_t s;
    test_session_init(&s, true);
    CHECK(test_session_claim(&s, 0, 1));
    CHECK(test_session_command(&s, 1, 1, 1, TEST_START, 0, 0));
    CHECK(test_session_command(&s, 2, 1, 2, TEST_TARGETS, 3, 6));
    test_session_tick(&s, 3000001);
    CHECK(s.control.mode == CONTROL_RUNNING);
    test_session_tick(&s, 3000002);
    CHECK(s.control.fault == FAULT_TIMEOUT && s.owner == 0);
    CHECK(s.control.requested_speed_mps == 0);
    CHECK(!test_session_command(&s, 3000003, 1, 3, TEST_HEARTBEAT, 0, 0));
    CHECK(test_session_claim(&s, 3000004, 2));
    CHECK(!test_session_command(&s, 3000005, 2, 1, TEST_START, 0, 0));
    CHECK(test_session_command(&s, 3000006, 2, 2, TEST_RESET, 0, 0));
    CHECK(s.control.mode == CONTROL_IDLE);
    CHECK(test_session_command(&s, 3000007, 2, 3, TEST_START, 0, 0));
    CHECK(s.control.requested_grade_percent == 0);
}
static void heartbeat_and_stop(void)
{
    test_session_t s;
    test_session_init(&s, true);
    CHECK(test_session_claim(&s, 0, 1));
    CHECK(test_session_command(&s, 1, 1, 1, TEST_START, 0, 0));
    CHECK(test_session_command(&s, 2, 1, 2, TEST_TARGETS, 5, 10));
    CHECK(test_session_command(&s, 2000000, 1, 3, TEST_HEARTBEAT, 0, 0));
    test_session_tick(&s, 3000002);
    CHECK(s.control.mode == CONTROL_RUNNING && s.control.requested_speed_mps == 5);
    CHECK(test_session_command(&s, 3000003, 1, 4, TEST_STOP, 0, 0));
    CHECK(s.control.mode == CONTROL_IDLE && s.control.requested_speed_mps == 0);
    test_session_tick(&s, 6000003);
    CHECK(s.owner == 0 && s.control.mode == CONTROL_IDLE);
}
void run_session_tests(void)
{
#ifdef ESP_PLATFORM
    RUN_TEST(ownership_and_replay);
    RUN_TEST(disconnect_and_restart);
    RUN_TEST(heartbeat_and_stop);
#else
    ownership_and_replay(); disconnect_and_restart(); heartbeat_and_stop();
    puts("All 3 session test groups passed.");
#endif
}
#ifndef ESP_PLATFORM
int main(void) { run_session_tests(); return 0; }
#endif
