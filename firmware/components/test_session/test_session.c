#include "test_session.h"
#include <math.h>
#include <stdio.h>
#include <inttypes.h>

#include <string.h>
#include "workouts.generated.h"

const char *test_session_workout_catalog(void) { return workout_catalog_json; }
static int64_t segment_end(const test_session_t *s, unsigned segment)
{
    unsigned weight = 0;
    for (unsigned i = 0; i <= segment; ++i) weight += s->workout->segments[i].weight;
    return s->duration_us * weight / s->workout->total_weight;
}
static bool valid_workout(const test_session_t *s, const workout_definition_t *w, float intensity)
{
    if (!w) return false;
    for (unsigned i = 0; i < w->segment_count; ++i) {
        float speed = w->segments[i].speed_mps * intensity;
        float grade = w->segments[i].grade_percent * intensity;
        if (!isfinite(speed) || !isfinite(grade) || speed < 0 || grade < 0 ||
            speed > s->control.config.max_speed_mps || grade > s->control.config.max_grade_percent)
            return false;
    }
    return true;
}
static void segment_targets(test_session_t *s, int segment, int64_t now)
{
    // A scheduled transition must never renew the tablet's command watchdog.
    int64_t deadline_base = s->control.last_command_us;
    if (!control_command(&s->control, now, s->enabled,
            s->workout->segments[segment].speed_mps * s->intensity,
            s->workout->segments[segment].grade_percent * s->intensity)) {
        control_stop(&s->control);
        s->workout_active = false;
    }
    s->control.last_command_us = deadline_base;
    s->segment = segment;
    s->overridden = false;
}
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
    if (s->control.mode != CONTROL_RUNNING || !s->owner) s->workout_active = false;
    if (!s->workout_active) return;
    int64_t elapsed = now - s->workout_started_us;
    if (elapsed >= s->duration_us) {
        control_stop(&s->control);
        s->workout_active = false;
        s->workout_complete = true;
        s->overridden = false;
        return;
    }
    int segment = 0;
    while ((unsigned)(segment + 1) < s->workout->segment_count &&
           elapsed >= segment_end(s, segment)) ++segment;
    if (segment != s->segment) segment_targets(s, segment, now);
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
static bool command(test_session_t *s, int64_t now, uint64_t token,
                          uint32_t sequence, test_action_t action, float speed, float grade,
                    const workout_definition_t *workout)
{
    test_session_tick(s, now);
    if (!s->enabled || !token || token != s->owner || !sequence ||
        sequence <= s->sequence || now < s->control.now_us) return false;
    bool accepted = false;
    switch (action) {
        case TEST_START:
            accepted = control_start(&s->control, now, true);
            if (accepted) { s->workout = 0; s->workout_complete = false; }
            break;
        case TEST_WORKOUT:
            // Transport uses speed/grade slots for duration minutes/intensity percent.
            if (!isfinite(speed) || !isfinite(grade) || speed < 1 || speed > 120 ||
                grade < 50 || grade > 150 || speed != (int)speed) break;
            if (s->control.mode != CONTROL_IDLE || !valid_workout(s, workout, grade / 100)) break;
            accepted = control_start(&s->control, now, true);
            if (accepted) {
                s->workout = workout;
                s->duration_us = (int64_t)speed * 60000000;
                s->intensity = grade / 100;
                s->workout_started_us = now;
                s->workout_active = true;
                s->workout_complete = false;
                segment_targets(s, 0, now);
            }
            break;
        case TEST_STOP: control_stop(&s->control); s->workout_active = false; s->workout_complete = false; accepted = true; break;
        case TEST_TARGETS: accepted = control_command(&s->control, now, true, speed, grade); if (accepted && s->workout_active) s->overridden = true; break;
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

int test_session_workout_json(const test_session_t *s, char *buffer, size_t size)
{
    int64_t elapsed = s->workout_complete ? s->duration_us :
        s->workout_active ? s->control.now_us - s->workout_started_us : 0;
    int64_t remaining = s->workout_active ?
        segment_end(s, s->segment) - elapsed : 0;
    return snprintf(buffer, size,
        "{\"id\":\"%s\",\"segment_count\":%u,\"active\":%s,\"complete\":%s,\"segment\":%d,"
        "\"elapsed_ms\":%" PRId64 ",\"duration_ms\":%" PRId64 ","
        "\"segment_remaining_ms\":%" PRId64 ",\"intensity\":%.2f,\"next_speed_mps\":%.2f,\"next_grade\":%.2f,\"overridden\":%s}",
        s->workout ? s->workout->id : "manual",
        s->workout ? s->workout->segment_count : 0, s->workout_active ? "true" : "false",
        s->workout_complete ? "true" : "false", s->segment,
        elapsed / 1000, s->duration_us / 1000, remaining / 1000,
        (double)s->intensity,
        s->workout_active && (unsigned)(s->segment + 1) < s->workout->segment_count ? (double)(s->workout->segments[s->segment+1].speed_mps*s->intensity) : 0,
        s->workout_active && (unsigned)(s->segment + 1) < s->workout->segment_count ? (double)(s->workout->segments[s->segment+1].grade_percent*s->intensity) : 0,
        s->overridden ? "true" : "false");
}

bool test_session_command(test_session_t *s, int64_t now, uint64_t token,
                          uint32_t sequence, test_action_t action, float speed, float grade)
{
    return command(s, now, token, sequence, action, speed, grade, NULL);
}
bool test_session_start_workout(test_session_t *s, int64_t now, uint64_t token,
                                uint32_t sequence, const char *id, float minutes, float intensity)
{
    const workout_definition_t *found = NULL;
    for (const workout_definition_t *w = workouts; w->id; ++w)
        if (id && !strcmp(w->id, id)) { found = w; break; }
    return command(s, now, token, sequence, TEST_WORKOUT, minutes, intensity, found);
}
