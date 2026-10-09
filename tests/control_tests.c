#include "control.h"
#include <math.h>
#ifdef ESP_PLATFORM
#include "unity.h"
#define CHECK(expr) TEST_ASSERT_TRUE(expr)
#else
#include <assert.h>
#include <stdio.h>
#define CHECK(expr) assert(expr)
#endif

static control_t ready(void)
{
    control_t c;
    // Test fixtures only; these are not approved machine limits.
    control_init(&c, (control_config_t){true, 5, 10, 1000});
    return c;
}
static void unconfigured_start(void)
{
    control_t c;
    control_init(&c, (control_config_t){0});
    CHECK(!control_start(&c, 0, true));
    CHECK(c.mode == CONTROL_IDLE && c.requested_speed_mps == 0);
    c = ready(); c.config.max_speed_mps = NAN;
    CHECK(!control_start(&c, 0, true));
    c = ready(); c.config.command_timeout_us = 0;
    CHECK(!control_start(&c, 0, true));
}
static void command_validation(void)
{
    control_t c = ready();
    CHECK(!control_command(&c, 0, true, 1, 1));
    CHECK(control_start(&c, 0, true));
    CHECK(control_command(&c, 100, true, 2, 3));
    CHECK(!control_command(&c, 200, true, NAN, 4));
    CHECK(!control_command(&c, 300, true, 2, INFINITY));
    CHECK(!control_command(&c, 400, true, -1, 3));
    CHECK(!control_command(&c, 500, true, 6, 3));
    CHECK(!control_command(&c, 600, true, 2, 11));
    CHECK(c.requested_speed_mps == 2 && c.requested_grade_percent == 3);
    CHECK(c.last_command_us == 100);
    control_tick(&c, 1100, true);
    CHECK(c.fault == FAULT_TIMEOUT);
}
static void timeout_latches(void)
{
    control_t c = ready();
    CHECK(control_start(&c, 0, true));
    control_tick(&c, 999, true);
    CHECK(c.mode == CONTROL_RUNNING);
    CHECK(!control_command(&c, 1000, true, 1, 1));
    CHECK(c.fault == FAULT_TIMEOUT && c.requested_speed_mps == 0);
    control_stop(&c);
    CHECK(!control_start(&c, 1001, true));
    CHECK(!control_reset(&c, 1002, false));
    CHECK(control_reset(&c, 1003, true));
    CHECK(c.mode == CONTROL_IDLE);
    CHECK(control_start(&c, 1004, true));
    CHECK(c.requested_speed_mps == 0 && c.requested_grade_percent == 0);
}
static void interlock_and_clock(void)
{
    control_t c = ready();
    CHECK(!control_start(&c, 0, false));
    CHECK(control_start(&c, 1, true));
    CHECK(control_command(&c, 2, true, 2, 3));
    control_tick(&c, 3, false);
    CHECK(c.fault == FAULT_INTERLOCK && c.requested_grade_percent == 0);
    c = ready();
    CHECK(control_start(&c, 100, true));
    control_tick(&c, 99, true);
    CHECK(c.fault == FAULT_CLOCK);
    CHECK(!control_reset(&c, 99, true));
}
static void stop_and_restart(void)
{
    control_t c = ready();
    CHECK(control_start(&c, 0, true));
    CHECK(control_command(&c, 1, true, 5, 10));
    control_stop(&c);
    CHECK(c.mode == CONTROL_IDLE && c.requested_speed_mps == 0);
    CHECK(control_start(&c, 2, true));
    CHECK(c.requested_grade_percent == 0);
}
#ifdef ESP_PLATFORM
void app_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(unconfigured_start);
    RUN_TEST(command_validation);
    RUN_TEST(timeout_latches);
    RUN_TEST(interlock_and_clock);
    RUN_TEST(stop_and_restart);
    UNITY_END();
}
#else
int main(void)
{
    unconfigured_start(); command_validation(); timeout_latches();
    interlock_and_clock(); stop_and_restart();
    puts("All 5 control test groups passed.");
    return 0;
}
#endif
