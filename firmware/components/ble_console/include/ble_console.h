#pragma once
#include "esp_err.h"
#include <stdint.h>
esp_err_t ble_console_start(void);
// Physical USB administrative operations. Fail unless runtime is idle.
esp_err_t ble_console_pair(uint32_t *passkey);
esp_err_t ble_console_forget(void);
