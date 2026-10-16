#include "motion_memory.h"
#include <math.h>
#ifdef ESP_PLATFORM
#include "unity.h"
#define CHECK(x) TEST_ASSERT_TRUE(x)
#else
#include <assert.h>
#include <stdio.h>
#define CHECK(x) assert(x)
#endif
static motion_config_t config(void)
{
    // Artificial test values, not approved machine limits.
    return (motion_config_t){true, 5, 0, 10, 1, 2, 1, 2000000, 3000000, 1000000};
}
static void ready(motion_memory_t *d, int64_t now)
{
    d->feedback = (motion_feedback_t){.timestamp_known=true,.sampled_us=now,
        .ready=MOTION_YES,.interlock_ok=MOTION_YES,.fault=MOTION_NO,
        .upper_limit=MOTION_NO,.lower_limit=MOTION_NO,
        .speed_mps={true,0},.grade_percent={true,3}};
}
static void setup(motion_t *m, motion_memory_t *d)
{
    motion_memory_init(d);motion_init(m,motion_memory_driver(d));
    CHECK(motion_configure(m,config()));ready(d,0);CHECK(motion_enable(m,0));
}
static void configuration_and_unknown(void)
{
    motion_t m;motion_memory_t d;motion_memory_init(&d);motion_init(&m,motion_memory_driver(&d));
    CHECK(!motion_enable(&m,0) && !d.enables && !d.writes);
    CHECK(!m.feedback.speed_mps.known && m.feedback.ready==MOTION_UNKNOWN);
    motion_config_t bad=config();bad.acceleration_mps2=NAN;
    CHECK(!motion_configure(&m,bad));CHECK(motion_configure(&m,config()));
    CHECK(!motion_enable(&m,0));ready(&d,0);d.feedback.grade_percent.known=false;
    CHECK(!motion_enable(&m,0));ready(&d,0);d.feedback.speed_mps.value=1;
    CHECK(!motion_enable(&m,0));ready(&d,0);CHECK(motion_enable(&m,0));
    CHECK(!motion_configure(&m,config()));
}
static void ramp_and_hold(void)
{
    motion_t m;motion_memory_t d;setup(&m,&d);
    CHECK(motion_targets(&m,0,4,6));ready(&d,500000);motion_tick(&m,500000);
    CHECK(d.last_speed_mps==.5f && d.last_grade_percent==3.5f);
    CHECK(d.feedback.speed_mps.value==0 && d.feedback.grade_percent.value==3);
    CHECK(motion_targets(&m,500000,0,3));ready(&d,750000);motion_tick(&m,750000);
    CHECK(d.last_speed_mps==0 && d.last_grade_percent==3.25f);
    unsigned writes=d.writes;CHECK(motion_stop(&m));
    CHECK(!m.running && !m.faulted && d.stops==1 && d.writes==writes);
    CHECK(d.last_grade_percent==3.25f && d.last_stop_reason==MOTION_STOP_OPERATOR);
    CHECK(motion_enable(&m,750000));CHECK(m.target_speed_mps==0 && m.target_grade_percent==3);
}
static void timeout_and_reset(void)
{
    motion_t m;motion_memory_t d;setup(&m,&d);CHECK(motion_targets(&m,0,4,6));
    for(int i=1;i<=3;i++){ready(&d,(int64_t)i*1000000);motion_tick(&m,(int64_t)i*1000000);}
    CHECK(m.faulted && !m.running && m.reason==MOTION_STOP_TIMEOUT);
    CHECK(!motion_enable(&m,3000000));d.feedback.speed_mps.value=1;
    CHECK(!motion_reset(&m,3000000));d.feedback.speed_mps.value=0;
    CHECK(motion_reset(&m,3000000));CHECK(!m.running);CHECK(motion_enable(&m,3000000));
    CHECK(m.target_speed_mps==0 && m.target_grade_percent==3);
}
static void stale_feedback_and_limits(void)
{
    motion_t m;motion_memory_t d;setup(&m,&d);m.config.feedback_timeout_us=100;
    motion_tick(&m,100);CHECK(m.faulted && m.reason==MOTION_STOP_FEEDBACK);
    setup(&m,&d);d.feedback.upper_limit=MOTION_YES;
    CHECK(!motion_targets(&m,0,1,4));CHECK(m.reason==MOTION_STOP_LIMIT);
    setup(&m,&d);d.feedback.upper_limit=MOTION_YES;
    CHECK(motion_targets(&m,0,1,2)); // Travel away from the upper limit is allowed.
    d.feedback.lower_limit=MOTION_YES;motion_tick(&m,1);CHECK(m.faulted);
    setup(&m,&d);d.feedback.interlock_ok=MOTION_NO;motion_tick(&m,1);
    CHECK(m.reason==MOTION_STOP_INTERLOCK && !d.writes);
}
static void driver_and_clock_faults(void)
{
    motion_t m;motion_memory_t d;setup(&m,&d);d.fail_targets=true;motion_tick(&m,1);
    CHECK(m.faulted && d.stops==1 && m.reason==MOTION_STOP_DRIVER);
    d.fail_stop=true;CHECK(!motion_stop(&m));CHECK(!motion_reset(&m,1));
    d.fail_stop=false;CHECK(motion_stop(&m));ready(&d,1);CHECK(motion_reset(&m,1));
    setup(&m,&d);motion_tick(&m,1000001);CHECK(m.reason==MOTION_STOP_CLOCK);
    setup(&m,&d);motion_tick(&m,1);motion_tick(&m,0);CHECK(m.reason==MOTION_STOP_CLOCK);
    setup(&m,&d);d.fail_read=true;motion_tick(&m,1);CHECK(m.reason==MOTION_STOP_FEEDBACK);
    CHECK(!m.feedback.speed_mps.known);
}
static void invalid_targets_do_not_extend_deadline(void)
{
    motion_t m;motion_memory_t d;setup(&m,&d);
    CHECK(!motion_targets(&m,1,NAN,3));CHECK(!motion_targets(&m,2,6,3));
    CHECK(m.last_command_us==0 && m.target_speed_mps==0);
    CHECK(!motion_targets(&m,3,1,11));CHECK(m.target_grade_percent==3);
}
void run_motion_tests(void)
{
#ifdef ESP_PLATFORM
    RUN_TEST(configuration_and_unknown);RUN_TEST(ramp_and_hold);RUN_TEST(timeout_and_reset);
    RUN_TEST(stale_feedback_and_limits);RUN_TEST(driver_and_clock_faults);
    RUN_TEST(invalid_targets_do_not_extend_deadline);
#else
    configuration_and_unknown();ramp_and_hold();timeout_and_reset();stale_feedback_and_limits();
    driver_and_clock_faults();invalid_targets_do_not_extend_deadline();
    puts("All 6 generic motion test groups passed.");
#endif
}
#ifndef ESP_PLATFORM
int main(void){run_motion_tests();return 0;}
#endif
