#include "motion_memory.h"
void motion_memory_init(motion_memory_t *m) { *m = (motion_memory_t){0}; }
static bool read_feedback(void *context, motion_feedback_t *feedback)
{
    motion_memory_t *m = context;
    ++m->reads;
    if (m->fail_read) return false;
    *feedback = m->feedback;
    return true;
}
static bool enable(void *context)
{
    motion_memory_t *m = context;
    ++m->enables;
    return !m->fail_enable;
}
static bool targets(void *context, float speed, float grade)
{
    motion_memory_t *m = context;
    ++m->writes;
    m->last_speed_mps = speed;
    m->last_grade_percent = grade;
    return !m->fail_targets;
}
static bool stop(void *context, motion_reason_t reason)
{
    motion_memory_t *m = context;
    ++m->stops;
    m->last_stop_reason = reason;
    return !m->fail_stop;
}
motion_driver_t motion_memory_driver(motion_memory_t *m)
{
    return (motion_driver_t){m, read_feedback, enable, targets, stop};
}
