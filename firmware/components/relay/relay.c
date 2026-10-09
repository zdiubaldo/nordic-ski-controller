#include "relay.h"

// TI TCA9554 register map, datasheet SCPS233E section 8.6.
enum { OUTPUT = 1, CONFIGURATION = 3 };

void relay_prepare(relay_t *r, void *context, relay_write_fn write,
                   relay_read_fn read, uint8_t inactive_levels)
{
    *r = (relay_t){ .context = context, .write = write, .read = read,
                    .inactive_levels = inactive_levels };
}
static bool matches(relay_t *r, uint8_t reg, uint8_t expected)
{
    uint8_t actual = 0;
    return r->read(r->context, reg, &actual) && actual == expected;
}
bool relay_initialize(relay_t *r)
{
    r->ready = false;
    if (!r->write || !r->read) return false;
    // Preload inactive latch BEFORE enabling outputs, including MCU-only reset
    // where the expander could still be powered and configured as outputs.
    if (!r->write(r->context, OUTPUT, r->inactive_levels) ||
        !matches(r, OUTPUT, r->inactive_levels) ||
        !r->write(r->context, CONFIGURATION, 0) ||
        !matches(r, CONFIGURATION, 0)) return false;
    r->commanded_mask = 0;
    r->ready = true;
    return true;
}
bool relay_set(relay_t *r, uint8_t energized_mask)
{
    if (!r->ready) return false;
    // Detect lost expander configuration before issuing a new demand.
    if (!matches(r, CONFIGURATION, 0)) {
        r->ready = false;
        return false;
    }
    uint8_t levels = energized_mask ^ r->inactive_levels;
    if (!r->write(r->context, OUTPUT, levels) || !matches(r, OUTPUT, levels)) {
        r->ready = false;
        return false;
    }
    r->commanded_mask = energized_mask;
    return true;
}
