#pragma once
#include "control.h"
#include <stddef.h>

typedef enum { TEST_START, TEST_STOP, TEST_TARGETS, TEST_HEARTBEAT, TEST_RESET, TEST_WORKOUT } test_action_t;
typedef struct { unsigned weight; float speed_mps, grade_percent; } workout_segment_t;
typedef struct {
    const char *id;
    unsigned segment_count, total_weight;
    const workout_segment_t *segments;
} workout_definition_t;
typedef struct {
    control_t control;
    bool enabled;
    uint64_t owner;
    uint32_t sequence;
    int64_t last_seen_us;
    const workout_definition_t *workout; // NULL for manual
    int segment;
    int64_t workout_started_us, duration_us;
    float intensity;
    bool workout_active, workout_complete, overridden;
} test_session_t;
// Software-only fixture. Callers serialize all access; no hardware adapter exists.
void test_session_init(test_session_t *s, bool enabled);
void test_session_tick(test_session_t *s, int64_t now);
bool test_session_claim(test_session_t *s, int64_t now, uint64_t token);
bool test_session_command(test_session_t *s, int64_t now, uint64_t token,
                          uint32_t sequence, test_action_t action, float speed, float grade);

// JSON object for transport status; snapshot session before calling.
int test_session_workout_json(const test_session_t *s, char *buffer, size_t size);

const char *test_session_workout_catalog(void);
bool test_session_start_workout(test_session_t *s, int64_t now, uint64_t token,
                                uint32_t sequence, const char *id,
                                float minutes, float intensity);
