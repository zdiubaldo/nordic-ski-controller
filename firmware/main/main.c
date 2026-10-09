#include "control.h"
#include "web_console.h"
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
    web_console_publish(&controller);
    esp_err_t network = web_console_start();
    if (network != ESP_OK)
        ESP_LOGW("nordic", "Status page unavailable: %s", esp_err_to_name(network));
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        // No physical interlock feedback exists yet; never assume it is healthy.
        control_tick(&controller, esp_timer_get_time(), false);
        web_console_publish(&controller);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(20));
    }
}
