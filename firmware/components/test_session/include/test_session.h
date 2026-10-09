#pragma once
#include "control.h"

typedef enum { TEST_START, TEST_STOP, TEST_TARGETS, TEST_HEARTBEAT, TEST_RESET } test_action_t;
typedef struct {
    control_t control;
    bool enabled;
    uint64_t owner;
    uint32_t sequence;
    int64_t last_seen_us;
} test_session_t;
// Software-only fixture. Callers serialize all access; no hardware adapter exists.
void test_session_init(test_session_t *s, bool enabled);
void test_session_tick(test_session_t *s, int64_t now);
bool test_session_claim(test_session_t *s, int64_t now, uint64_t token);
bool test_session_command(test_session_t *s, int64_t now, uint64_t token,
                          uint32_t sequence, test_action_t action, float speed, float grade);
