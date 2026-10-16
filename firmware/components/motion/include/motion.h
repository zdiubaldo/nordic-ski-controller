#pragma once
#include <stdbool.h>
#include <stdint.h>

// Unknown is deliberately the zero/default state. No inferred measurements.
typedef enum { MOTION_UNKNOWN, MOTION_NO, MOTION_YES } motion_signal_t;
typedef struct { bool known; float value; } motion_measurement_t;
typedef struct {
    bool timestamp_known;
    int64_t sampled_us;
    motion_signal_t ready, interlock_ok, fault, upper_limit, lower_limit;
    motion_measurement_t speed_mps, grade_percent;
} motion_feedback_t;
typedef enum {
    MOTION_OK, MOTION_STOP_OPERATOR, MOTION_STOP_TIMEOUT, MOTION_STOP_FEEDBACK,
    MOTION_STOP_INTERLOCK, MOTION_STOP_LIMIT, MOTION_STOP_DRIVER, MOTION_STOP_CLOCK
} motion_reason_t;
// Stop is a semantic request. A future physical driver must implement approved
// roller stopping AND lift holding; it is not equivalent to all relays off.
typedef struct {
    void *context;
    bool (*read)(void *context, motion_feedback_t *feedback);
    // Enable at zero roller demand and hold current lift position; never replay targets.
    bool (*enable)(void *context);
    bool (*targets)(void *context, float speed_mps, float grade_percent);
    bool (*stop)(void *context, motion_reason_t reason);
} motion_driver_t;
typedef struct {
    bool commissioned;
    float max_speed_mps, min_grade_percent, max_grade_percent;
    float acceleration_mps2, deceleration_mps2, grade_rate_percent_per_s;
    int64_t feedback_timeout_us, command_timeout_us, max_tick_gap_us;
} motion_config_t;
typedef struct {
    motion_driver_t driver;
    motion_config_t config;
    motion_feedback_t feedback;
    bool running, faulted, stop_acknowledged; // Acknowledged request, not proof of physical stop.
    motion_reason_t reason;
    int64_t now_us, last_command_us;
    float target_speed_mps, target_grade_percent;
    float sent_speed_mps, sent_grade_percent;
} motion_t;
// Single owner: call only from one task; queue transport requests to that task.
// No I/O during init; no default machine configuration or automatic restart.
void motion_init(motion_t *m, motion_driver_t driver);
bool motion_configure(motion_t *m, motion_config_t config);
bool motion_enable(motion_t *m, int64_t now_us);
bool motion_targets(motion_t *m, int64_t now_us, float speed_mps, float grade_percent);
void motion_tick(motion_t *m, int64_t now_us);
bool motion_stop(motion_t *m);
bool motion_reset(motion_t *m, int64_t now_us);
