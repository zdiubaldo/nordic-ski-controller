#include "relay.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "unity.h"
#define CHECK(expr) TEST_ASSERT_TRUE(expr)
#else
#include <assert.h>
#include <stdio.h>
#define CHECK(expr) assert(expr)
#endif

typedef struct {
    uint8_t registers[4];
    unsigned calls, fail_at, mismatch_at;
    uint8_t written_registers[16], written_values[16];
    unsigned writes;
} bus_t;
static bool write_reg(void *ctx, uint8_t reg, uint8_t value)
{
    bus_t *b = ctx;
    b->calls++;
    if (b->calls == b->fail_at) return false;
    CHECK(b->writes < 16);
    b->written_registers[b->writes] = reg;
    b->written_values[b->writes++] = value;
    b->registers[reg] = value;
    return true;
}
static bool read_reg(void *ctx, uint8_t reg, uint8_t *value)
{
    bus_t *b = ctx;
    b->calls++;
    if (b->calls == b->fail_at) return false;
    *value = b->registers[reg];
    if (b->calls == b->mismatch_at) *value ^= 1;
    return true;
}
static void prepare(relay_t *r, bus_t *b, uint8_t inactive)
{
    memset(b, 0, sizeof(*b));
    b->registers[1] = b->registers[3] = 255;
    relay_prepare(r, b, write_reg, read_reg, inactive);
}
static void initialization_order(void)
{
    relay_t r; bus_t b;
    prepare(&r, &b, 0);
    CHECK(!relay_set(&r, 1) && b.calls == 0);
    CHECK(relay_initialize(&r));
    CHECK(b.writes == 2 && b.written_registers[0] == 1);
    CHECK(b.written_values[0] == 0 && b.written_registers[1] == 3);
    CHECK(b.written_values[1] == 0 && r.commanded_mask == 0);
    // Reinitialization also clears a previously energized output.
    CHECK(relay_set(&r, 128));
    CHECK(relay_initialize(&r) && r.commanded_mask == 0);
}
static void polarity_and_channels(void)
{
    for (unsigned polarity = 0; polarity <= 255; polarity += 255) {
        relay_t r; bus_t b;
        prepare(&r, &b, (uint8_t)polarity);
        CHECK(relay_initialize(&r));
        for (unsigned channel = 0; channel < 8; ++channel) {
            uint8_t mask = (uint8_t)(1u << channel);
            CHECK(relay_set(&r, mask));
            CHECK(b.registers[1] == (uint8_t)(mask ^ polarity));
            CHECK(r.commanded_mask == mask);
        }
        CHECK(relay_set(&r, 0) && b.registers[1] == polarity);
    }
}
static void initialization_failures(void)
{
    for (unsigned fail = 1; fail <= 4; ++fail) {
        relay_t r; bus_t b;
        prepare(&r, &b, 0); b.fail_at = fail;
        CHECK(!relay_initialize(&r) && !r.ready);
        unsigned calls = b.calls;
        CHECK(!relay_set(&r, 255) && b.calls == calls);
    }
    for (unsigned mismatch = 2; mismatch <= 4; mismatch += 2) {
        relay_t r; bus_t b;
        prepare(&r, &b, 0); b.mismatch_at = mismatch;
        CHECK(!relay_initialize(&r) && !r.ready);
    }
    relay_t r;
    relay_prepare(&r, 0, 0, 0, 0);
    CHECK(!relay_initialize(&r));
}
static void runtime_failures(void)
{
    for (unsigned fail = 1; fail <= 3; ++fail) {
        relay_t r; bus_t b;
        prepare(&r, &b, 0); CHECK(relay_initialize(&r));
        b.fail_at = b.calls + fail;
        CHECK(!relay_set(&r, 1) && !r.ready && r.commanded_mask == 0);
        unsigned calls = b.calls;
        CHECK(!relay_set(&r, 2) && b.calls == calls);
        b.fail_at = 0;
        CHECK(relay_initialize(&r) && r.commanded_mask == 0);
    }
    relay_t r; bus_t b;
    prepare(&r, &b, 0); CHECK(relay_initialize(&r));
    b.mismatch_at = b.calls + 3;
    CHECK(!relay_set(&r, 1) && !r.ready);
    prepare(&r, &b, 0); CHECK(relay_initialize(&r));
    b.registers[3] = 255; // Expander reset independently of MCU.
    unsigned writes = b.writes;
    CHECK(!relay_set(&r, 1) && !r.ready && b.writes == writes);
}
void run_relay_tests(void)
{
#ifdef ESP_PLATFORM
    RUN_TEST(initialization_order);
    RUN_TEST(polarity_and_channels);
    RUN_TEST(initialization_failures);
    RUN_TEST(runtime_failures);
#else
    initialization_order(); polarity_and_channels();
    initialization_failures(); runtime_failures();
    puts("All 4 relay test groups passed.");
#endif
}
#ifndef ESP_PLATFORM
int main(void) { run_relay_tests(); return 0; }
#endif
