#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "relay_mapping.h"
typedef struct { bool enabled, ready, fault, known; uint8_t mask; uint32_t button_ack_event; int64_t sampled_us; relay_mapping_t mapping; } relay_control_status_t;
void relay_control_start(void);
void relay_control_update(bool running, bool stop_requested, float speed, float grade, unsigned button, uint32_t button_event, int64_t now);
relay_control_status_t relay_control_status(void);
