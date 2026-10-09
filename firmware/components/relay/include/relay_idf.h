#pragma once
#include "relay.h"
#include "driver/i2c_master.h"

// Device/bus lifetime belongs to caller. No GPIO/address/polarity assumptions.
// This only binds the callbacks; it performs no I2C operations.
void relay_prepare_idf(relay_t *r, i2c_master_dev_handle_t device,
                       uint8_t inactive_levels);
