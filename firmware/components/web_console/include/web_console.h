#pragma once
#include "control.h"
#include "esp_err.h"

// Publish a copy from the owning control task; HTTP never touches control state.
void web_console_publish(const control_t *controller);
esp_err_t web_console_start(void);
