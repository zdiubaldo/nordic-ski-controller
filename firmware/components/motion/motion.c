#include "motion.h"
#include <math.h>

static bool config_valid(motion_config_t c)
{
    return c.commissioned && isfinite(c.max_speed_mps) && c.max_speed_mps > 0 &&
        isfinite(c.min_grade_percent) && isfinite(c.max_grade_percent) &&
        c.min_grade_percent <= c.max_grade_percent &&
        isfinite(c.acceleration_mps2) && c.acceleration_mps2 > 0 &&
        isfinite(c.deceleration_mps2) && c.deceleration_mps2 > 0 &&
        isfinite(c.grade_rate_percent_per_s) && c.grade_rate_percent_per_s > 0 &&
        c.feedback_timeout_us > 0 && c.command_timeout_us > 0 && c.max_tick_gap_us > 0;
}
static bool in_range(const motion_t *m, float speed, float grade)
{
    return isfinite(speed) && speed >= 0 && speed <= m->config.max_speed_mps &&
        isfinite(grade) && grade >= m->config.min_grade_percent && grade <= m->config.max_grade_percent;
}
static bool stop_with(motion_t *m, motion_reason_t reason, bool fault)
{
    m->running = false;
    m->target_speed_mps = 0;
    // Preserve incline request; stopping is NOT an instruction to lower the lift.
    if (!m->faulted) m->reason = reason;
    m->faulted |= fault;
    m->stop_acknowledged = m->driver.stop && m->driver.stop(m->driver.context, reason);
    if (!m->stop_acknowledged) {
        m->faulted = true;
        m->reason = MOTION_STOP_DRIVER;
    }
    return m->stop_acknowledged;
}
static bool feedback(motion_t *m, int64_t now)
{
    m->feedback = (motion_feedback_t){0};
    if (!m->driver.read || !m->driver.read(m->driver.context, &m->feedback)) return false;
    motion_feedback_t f = m->feedback;
    return f.timestamp_known && f.sampled_us >= 0 && f.sampled_us <= now &&
        now - f.sampled_us < m->config.feedback_timeout_us &&
        f.ready == MOTION_YES && f.interlock_ok == MOTION_YES && f.fault == MOTION_NO &&
        (f.upper_limit == MOTION_YES || f.upper_limit == MOTION_NO) &&
        (f.lower_limit == MOTION_YES || f.lower_limit == MOTION_NO) &&
        !(f.upper_limit == MOTION_YES && f.lower_limit == MOTION_YES) &&
        f.speed_mps.known && f.grade_percent.known &&
        in_range(m, f.speed_mps.value, f.grade_percent.value);
}
static bool limit_blocks(const motion_t *m, float grade)
{
    return (m->feedback.upper_limit == MOTION_YES && grade > m->feedback.grade_percent.value) ||
           (m->feedback.lower_limit == MOTION_YES && grade < m->feedback.grade_percent.value);
}
void motion_init(motion_t *m, motion_driver_t driver)
{
    *m = (motion_t){.driver = driver};
}
bool motion_configure(motion_t *m, motion_config_t config)
{
    if (m->running || m->faulted || !config_valid(config)) return false;
    m->config = config;
    return true;
}
bool motion_enable(motion_t *m, int64_t now)
{
    if (m->running || m->faulted || !config_valid(m->config) || now < m->now_us ||
        !m->driver.enable || !m->driver.targets || !m->driver.stop) return false;
    if (!feedback(m, now)) return false;
    // Enabling a moving roller is forbidden. No inferred zero/position baseline.
    if (m->feedback.speed_mps.value != 0) return false;
    m->now_us = m->last_command_us = now;
    m->target_speed_mps = m->sent_speed_mps = 0;
    m->target_grade_percent = m->sent_grade_percent = m->feedback.grade_percent.value;
    if (!m->driver.enable(m->driver.context)) { stop_with(m, MOTION_STOP_DRIVER, true); return false; }
    m->running = true;
    m->reason = MOTION_OK;
    m->stop_acknowledged = false;
    return true;
}
bool motion_targets(motion_t *m, int64_t now, float speed, float grade)
{
    // Process stale commands, faults, and limits before accepting fresh demand.
    motion_tick(m, now);
    if (!m->running || !in_range(m, speed, grade)) return false;
    if (limit_blocks(m, grade)) { stop_with(m, MOTION_STOP_LIMIT, true); return false; }
    m->target_speed_mps = speed;
    m->target_grade_percent = grade;
    m->last_command_us = now;
    return true;
}
static float approach(float current, float target, float amount)
{
    return current < target ? fminf(target, current + amount) : fmaxf(target, current - amount);
}
void motion_tick(motion_t *m, int64_t now)
{
    if (now < m->now_us) { stop_with(m, MOTION_STOP_CLOCK, true); return; }
    int64_t delta = now - m->now_us;
    m->now_us = now;
    if (!m->running) return;
    if (delta > m->config.max_tick_gap_us) { stop_with(m, MOTION_STOP_CLOCK, true); return; }
    if (now - m->last_command_us >= m->config.command_timeout_us) { stop_with(m, MOTION_STOP_TIMEOUT, true); return; }
    if (!feedback(m, now)) {
        stop_with(m, m->feedback.interlock_ok == MOTION_NO ? MOTION_STOP_INTERLOCK : MOTION_STOP_FEEDBACK, true);
        return;
    }
    if (limit_blocks(m, m->target_grade_percent)) { stop_with(m, MOTION_STOP_LIMIT, true); return; }
    if (!delta) return;
    float seconds = (float)delta / 1000000;
    float rate = m->target_speed_mps > m->sent_speed_mps ? m->config.acceleration_mps2 : m->config.deceleration_mps2;
    float speed = approach(m->sent_speed_mps, m->target_speed_mps, rate * seconds);
    float grade = approach(m->sent_grade_percent, m->target_grade_percent, m->config.grade_rate_percent_per_s * seconds);
    if (!m->driver.targets(m->driver.context, speed, grade)) { stop_with(m, MOTION_STOP_DRIVER, true); return; }
    m->sent_speed_mps = speed;
    m->sent_grade_percent = grade;
}
bool motion_stop(motion_t *m) { return stop_with(m, MOTION_STOP_OPERATOR, false); }
bool motion_reset(motion_t *m, int64_t now)
{
    if (!m->faulted || now < m->now_us || !m->stop_acknowledged ||
        !feedback(m, now) || m->feedback.speed_mps.value != 0) return false;
    m->now_us = now;
    m->faulted = false;
    m->reason = MOTION_OK;
    return true;
}
