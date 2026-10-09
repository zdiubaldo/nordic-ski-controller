#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { CONTROL_IDLE, CONTROL_RUNNING, CONTROL_FAULT } control_mode_t;
typedef enum { FAULT_NONE, FAULT_TIMEOUT, FAULT_INTERLOCK, FAULT_CLOCK } control_fault_t;
typedef struct {
    bool commissioned;
    float max_speed_mps;
    float max_grade_percent;
    int64_t command_timeout_us;
} control_config_t;
typedef struct {
    control_config_t config;
    control_mode_t mode;
    control_fault_t fault;
    bool interlock_ok;
    int64_t now_us;
    int64_t last_command_us;
    float requested_speed_mps;
    float requested_grade_percent;
} control_t;

// Single owning task only. Future transports must queue validated commands to it.
void control_init(control_t *c, control_config_t config);
void control_tick(control_t *c, int64_t now_us, bool interlock_ok);
bool control_start(control_t *c, int64_t now_us, bool interlock_ok);
bool control_command(control_t *c, int64_t now_us, bool interlock_ok,
                     float speed_mps, float grade_percent);
void control_stop(control_t *c);
bool control_reset(control_t *c, int64_t now_us, bool interlock_ok);
