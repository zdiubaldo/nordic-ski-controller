#pragma once
#include "motion.h"
// Command recorder and manually injected feedback, not a physics simulation.
// No GPIO, relays, serial ports, or assumption that a requested target was reached.
typedef struct {
    motion_feedback_t feedback;
    bool fail_read, fail_enable, fail_targets, fail_stop;
    unsigned reads, enables, writes, stops;
    float last_speed_mps, last_grade_percent;
    motion_reason_t last_stop_reason;
} motion_memory_t;
void motion_memory_init(motion_memory_t *memory);
motion_driver_t motion_memory_driver(motion_memory_t *memory);
