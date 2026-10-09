#include "control.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    control_t controller;
    control_init(&controller, (control_config_t){0});
    ESP_LOGI("nordic", "Boot complete: UNCONFIGURED; motion commands disabled");
    ESP_LOGI("nordic", "No board I/O driver loaded; physical relay state is unverified");
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        // No physical interlock feedback exists yet; never assume it is healthy.
        control_tick(&controller, esp_timer_get_time(), false);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(20));
    }
}
