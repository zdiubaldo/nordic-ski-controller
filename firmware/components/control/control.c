#include "control.h"
#include <math.h>

static void clear_demand(control_t *c)
{
    c->requested_speed_mps = 0;
    c->requested_grade_percent = 0;
}
static void trip(control_t *c, control_fault_t fault)
{
    clear_demand(c);
    c->mode = CONTROL_FAULT;
    if (c->fault == FAULT_NONE) c->fault = fault;
}
static bool configured(const control_t *c)
{
    return c->config.commissioned && c->config.command_timeout_us > 0 &&
        isfinite(c->config.max_speed_mps) && c->config.max_speed_mps > 0 &&
        isfinite(c->config.max_grade_percent) && c->config.max_grade_percent >= 0;
}
void control_init(control_t *c, control_config_t config)
{
    *c = (control_t){ .config = config, .mode = CONTROL_IDLE };
}
void control_tick(control_t *c, int64_t now_us, bool interlock_ok)
{
    c->interlock_ok = interlock_ok;
    if (now_us < c->now_us) {
        trip(c, FAULT_CLOCK);
        return;
    }
    c->now_us = now_us;
    if (c->mode != CONTROL_RUNNING) return;
    if (!interlock_ok) trip(c, FAULT_INTERLOCK);
    else if (now_us - c->last_command_us >= c->config.command_timeout_us)
        trip(c, FAULT_TIMEOUT);
}
bool control_start(control_t *c, int64_t now_us, bool interlock_ok)
{
    control_tick(c, now_us, interlock_ok);
    if (c->mode != CONTROL_IDLE || !configured(c) || !c->interlock_ok) return false;
    clear_demand(c);
    c->last_command_us = now_us;
    c->mode = CONTROL_RUNNING;
    return true;
}
bool control_command(control_t *c, int64_t now_us, bool interlock_ok,
                     float speed_mps, float grade_percent)
{
    control_tick(c, now_us, interlock_ok);
    if (c->mode != CONTROL_RUNNING || !isfinite(speed_mps) || !isfinite(grade_percent) ||
        speed_mps < 0 || speed_mps > c->config.max_speed_mps ||
        grade_percent < 0 || grade_percent > c->config.max_grade_percent) return false;
    c->requested_speed_mps = speed_mps;
    c->requested_grade_percent = grade_percent;
    c->last_command_us = now_us;
    return true;
}
void control_stop(control_t *c)
{
    clear_demand(c);
    if (c->mode != CONTROL_FAULT) c->mode = CONTROL_IDLE;
}
bool control_reset(control_t *c, int64_t now_us, bool interlock_ok)
{
    control_tick(c, now_us, interlock_ok);
    if (!interlock_ok || now_us < c->now_us || c->mode != CONTROL_FAULT) return false;
    clear_demand(c);
    c->fault = FAULT_NONE;
    c->mode = CONTROL_IDLE;
    return true;
}
