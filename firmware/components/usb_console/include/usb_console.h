#pragma once
#include "esp_err.h"
// Bench diagnostic adapter. Physical USB access is its authorization boundary.
esp_err_t usb_console_start(void);
