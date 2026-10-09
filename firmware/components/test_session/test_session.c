#include "test_session.h"

#define LEASE_US 3000000
void test_session_init(test_session_t *s, bool enabled)
{
    *s = (test_session_t){ .enabled = enabled };
    // Artificial test limits, never physical commissioning parameters.
    control_init(&s->control, (control_config_t){enabled, 5, 10, LEASE_US});
}
void test_session_tick(test_session_t *s, int64_t now)
{
    control_tick(&s->control, now, s->enabled);
    if (s->owner && (now < s->last_seen_us || now - s->last_seen_us >= LEASE_US)) {
        s->owner = 0;
        s->sequence = 0;
    }
}
bool test_session_claim(test_session_t *s, int64_t now, uint64_t token)
{
    test_session_tick(s, now);
    if (!s->enabled || !token || s->owner || now < s->control.now_us) return false;
    s->owner = token;
    s->sequence = 0;
    s->last_seen_us = now;
    return true;
}
bool test_session_command(test_session_t *s, int64_t now, uint64_t token,
                          uint32_t sequence, test_action_t action, float speed, float grade)
{
    test_session_tick(s, now);
    if (!s->enabled || !token || token != s->owner || !sequence ||
        sequence <= s->sequence || now < s->control.now_us) return false;
    bool accepted = false;
    switch (action) {
        case TEST_START: accepted = control_start(&s->control, now, true); break;
        case TEST_STOP: control_stop(&s->control); accepted = true; break;
        case TEST_TARGETS: accepted = control_command(&s->control, now, true, speed, grade); break;
        case TEST_RESET: accepted = control_reset(&s->control, now, true); break;
        case TEST_HEARTBEAT:
            accepted = s->control.mode != CONTROL_RUNNING ||
                control_command(&s->control, now, true,
                    s->control.requested_speed_mps, s->control.requested_grade_percent);
            break;
        default: break;
    }
    // Consume an authenticated sequence even on invalid action/setpoints, so
    // rejected requests cannot become valid by being replayed in a later state.
    s->sequence = sequence;
    if (accepted) s->last_seen_us = now;
    return accepted;
}
