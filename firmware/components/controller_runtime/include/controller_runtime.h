#pragma once
#include "control_service.h"
#include "esp_err.h"
// One owning task serves ALL adapters. Never call service/driver directly from HTTP/BLE.
esp_err_t controller_runtime_start(bool software_test);
service_result_t controller_runtime_submit_result(const service_command_t *command);
bool controller_runtime_submit(const service_command_t *command);
void controller_runtime_snapshot(control_service_t *snapshot);
