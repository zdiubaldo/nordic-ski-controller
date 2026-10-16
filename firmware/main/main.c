#include "controller_runtime.h"
#include "web_console.h"
#include "usb_console.h"
#include "ble_console.h"
#include "esp_log.h"
#include "sdkconfig.h"
void app_main(void)
{
#ifdef CONFIG_NORDIC_SOFTWARE_TEST
    bool enabled=true;
#else
    bool enabled=false;
#endif
    ESP_ERROR_CHECK(controller_runtime_start(enabled));
    ESP_LOGI("nordic","Shared service started; see dashboard for relay-output capability");
    esp_err_t usb=usb_console_start();
    if(usb==ESP_OK)ESP_LOGI("nordic","USB diagnostic client ready");
    else ESP_LOGW("nordic","USB diagnostic client unavailable: %s",esp_err_to_name(usb));
    esp_err_t ble=ble_console_start();
    if(ble==ESP_OK)ESP_LOGI("nordic","BLE client starting");
    else if(ble!=ESP_ERR_NOT_SUPPORTED)ESP_LOGW("nordic","BLE unavailable: %s",esp_err_to_name(ble));
    esp_err_t result=web_console_start();
    if(result!=ESP_OK)ESP_LOGW("nordic","Web page unavailable: %s",esp_err_to_name(result));
}
