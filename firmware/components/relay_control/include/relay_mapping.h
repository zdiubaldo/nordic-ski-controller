#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { unsigned start, stop, speed_up, speed_down, incline_up, incline_down, pulse_ms; } relay_mapping_t;
typedef struct {
    relay_mapping_t map;
    bool running;
    float speed, grade;
    int speed_direction, grade_direction;
    int64_t start_end, stop_end;
    int64_t speed_start, speed_end, grade_start, grade_end;
} relay_mapping_state_t;
bool relay_mapping_valid(relay_mapping_t map);
void relay_mapping_update(relay_mapping_state_t *s, bool running, bool stop_requested, float speed, float grade, int64_t now);
uint8_t relay_mapping_mask(const relay_mapping_state_t *s, int64_t now);

// Discrete button press; false for invalid/stopped/busy requests.
bool relay_mapping_press(relay_mapping_state_t *s, unsigned button, int64_t now);
