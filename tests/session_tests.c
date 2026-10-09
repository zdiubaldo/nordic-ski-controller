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
static void workout_timing_and_override(void)
{
    test_session_t s;
    test_session_init(&s, true);
    CHECK(test_session_claim(&s, 0, 1));
    CHECK(!test_session_start_workout(&s, 0, 1, 1, "hills", NAN, 100));
    CHECK(!test_session_start_workout(&s, 0, 1, 2, "hills", 1, 151));
    CHECK(test_session_start_workout(&s, 0, 1, 3, "hills", 1, 100));
    CHECK(s.workout_active && s.control.requested_speed_mps == 1);
    CHECK(!test_session_start_workout(&s, 1, 1, 4, "steady", 1, 100));
    uint32_t seq = 5;
    for (int second = 1; second <= 60; ++second) {
        int64_t now = (int64_t)second * 1000000;
        CHECK(test_session_command(&s, now, 1, seq++, TEST_HEARTBEAT, 0, 0));
        if (second == 2) {
            CHECK(test_session_command(&s, now, 1, seq++, TEST_TARGETS, 4, 8));
            CHECK(s.overridden);
        }
        if (second == 5) CHECK(s.control.requested_speed_mps == 4);
        if (second == 6) {
            CHECK(s.segment == 1 && !s.overridden);
            CHECK(s.control.requested_speed_mps == 1.5f);
        }
    }
    CHECK(s.workout_complete && !s.workout_active && s.control.mode == CONTROL_IDLE);
    CHECK(s.control.requested_speed_mps == 0);
    CHECK(test_session_command(&s, 61000000, 1, seq++, TEST_START, 0, 0));
    CHECK(!s.workout_active && !s.workout_complete && s.workout == 0);
}
static void workout_does_not_renew_lease(void)
{
    test_session_t s;
    test_session_init(&s, true);
    CHECK(test_session_claim(&s, 0, 1));
    CHECK(test_session_start_workout(&s, 0, 1, 1, "intervals", 1, 150));
    CHECK(test_session_command(&s, 2000000, 1, 2, TEST_HEARTBEAT, 0, 0));
    CHECK(test_session_command(&s, 4000000, 1, 3, TEST_HEARTBEAT, 0, 0));
    test_session_tick(&s, 6000000); // Segment boundary between heartbeats.
    CHECK(s.segment == 1 && s.control.last_command_us == 4000000);
    test_session_tick(&s, 7000000);
    CHECK(s.control.fault == FAULT_TIMEOUT && !s.workout_active && !s.owner);
    CHECK(s.control.requested_speed_mps == 0 && !s.workout_complete);
    test_session_init(&s, true);
    CHECK(test_session_claim(&s, 0, 1));
    CHECK(test_session_start_workout(&s, 0, 1, 1, "steady", 1, 50));
    CHECK(test_session_command(&s, 1, 1, 2, TEST_STOP, 0, 0));
    test_session_tick(&s, 6000000);
    CHECK(!s.workout_active && s.control.mode == CONTROL_IDLE);
}
static void generic_weighted_segments(void)
{
    const workout_segment_t segments[] = {{1, 1, 0}, {2, 2, 3}, {1, 1, 0}};
    const workout_definition_t custom = {"custom", 3, 4, segments};
    test_session_t s;
    test_session_init(&s, true);
    CHECK(test_session_claim(&s, 0, 1));
    CHECK(!test_session_start_workout(&s, 0, 1, 1, "unknown", 1, 100));
    CHECK(s.control.mode == CONTROL_IDLE);
    CHECK(!test_session_command(&s, 0, 1, 1, TEST_START, 0, 0)); // Rejected sequence consumed.
    CHECK(test_session_start_workout(&s, 0, 1, 2, "steady", 1, 100));
    s.workout = &custom; // Exercise runner independently of the shipped catalog.
    for (int second = 1; second <= 60; ++second) {
        CHECK(test_session_command(&s, (int64_t)second*1000000, 1, second+2, TEST_HEARTBEAT, 0, 0));
        if (second < 15) CHECK(s.segment == 0);
        else if (second < 45) CHECK(s.segment == 1 && s.control.requested_speed_mps == 2);
        else CHECK(s.segment == 2);
    }
    CHECK(s.workout_complete && s.control.mode == CONTROL_IDLE);
    test_session_init(&s, true);
    CHECK(test_session_claim(&s, 0, 1));
    s.control.config.max_speed_mps = 1;
    CHECK(!test_session_start_workout(&s, 0, 1, 1, "intervals", 1, 100));
    CHECK(s.control.mode == CONTROL_IDLE); // Entire profile validated before any motion request.
}
void run_session_tests(void)
{
#ifdef ESP_PLATFORM
    RUN_TEST(generic_weighted_segments);
    RUN_TEST(workout_timing_and_override);
    RUN_TEST(workout_does_not_renew_lease);
    RUN_TEST(ownership_and_replay);
    RUN_TEST(disconnect_and_restart);
    RUN_TEST(heartbeat_and_stop);
#else
    generic_weighted_segments();
    workout_timing_and_override(); workout_does_not_renew_lease();
    ownership_and_replay(); disconnect_and_restart(); heartbeat_and_stop();
    puts("All 6 session test groups passed.");
#endif
}
#ifndef ESP_PLATFORM
int main(void) { run_session_tests(); return 0; }
#endif
