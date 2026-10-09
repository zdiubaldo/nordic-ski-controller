#pragma once
#include <stdbool.h>
#include <stdint.h>

// Callbacks access one register on an already selected TCA9554 device.
// One task owns the driver. Callbacks must have bounded execution time.
typedef bool (*relay_write_fn)(void *, uint8_t, uint8_t);
typedef bool (*relay_read_fn)(void *, uint8_t, uint8_t *);
typedef struct {
    void *context;
    relay_write_fn write;
    relay_read_fn read;
    uint8_t inactive_levels;
    uint8_t commanded_mask;
    bool ready;
} relay_t;

// Caller must verify board polarity before use. An inactive bit is the
// electrical level for a de-energized relay, not the input inversion register.
// Prepare never touches hardware; initialize explicitly writes all-off first.
void relay_prepare(relay_t *r, void *context, relay_write_fn write,
                   relay_read_fn read, uint8_t inactive_levels);
bool relay_initialize(relay_t *r);
bool relay_set(relay_t *r, uint8_t energized_mask);
