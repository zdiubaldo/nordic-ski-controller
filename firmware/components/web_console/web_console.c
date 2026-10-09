#include "web_console.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "web_console";
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static control_mode_t mode = CONTROL_IDLE;
static control_fault_t fault = FAULT_NONE;
static int64_t sampled_at_us;
extern const unsigned char page_start[] asm("_binary_index_html_start");
extern const unsigned char page_end[] asm("_binary_index_html_end");

void web_console_publish(const control_t *controller)
{
    portENTER_CRITICAL(&status_lock);
    mode = controller->mode;
    fault = controller->fault;
    sampled_at_us = controller->now_us;
    portEXIT_CRITICAL(&status_lock);
}
static void headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Content-Security-Policy",
        "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
        "connect-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'none'");
}
static esp_err_t index_get(httpd_req_t *req)
{
    headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)page_start, page_end - page_start);
}
static esp_err_t status_get(httpd_req_t *req)
{
    portENTER_CRITICAL(&status_lock);
    control_mode_t current_mode = mode;
    control_fault_t current_fault = fault;
    int64_t sample = sampled_at_us;
    portEXIT_CRITICAL(&status_lock);
    const char *name = current_mode == CONTROL_IDLE ? "idle" :
                       current_mode == CONTROL_RUNNING ? "running" : "fault";
    char body[320];
    int length = snprintf(body, sizeof(body),
        "{\"mode\":\"%s\",\"fault\":%d,\"commissioned\":false,"
        "\"motion_available\":false,\"hardware_verified\":false,"
        "\"uptime_ms\":%" PRId64 ",\"sample_age_ms\":%" PRId64 "}",
        name, (int)current_fault, esp_timer_get_time() / 1000,
        (esp_timer_get_time() - sample) / 1000);
    if (length < 0 || (size_t)length >= sizeof(body)) return ESP_FAIL;
    headers(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, length);
}
esp_err_t web_console_start(void)
{
    const char *ssid = CONFIG_NORDIC_AP_SSID;
    const char *password = CONFIG_NORDIC_AP_PASSWORD;
    size_t ssid_len = strlen(ssid), password_len = strlen(password);
    if (!ssid_len || ssid_len > 32 || password_len < 8 || password_len > 63) {
        ESP_LOGW(TAG, "Wi-Fi disabled: configure SSID and a 8-63 character password in menuconfig");
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < password_len; ++i)
        if ((unsigned char)password[i] < 32 || (unsigned char)password[i] > 126)
            return ESP_ERR_INVALID_ARG;

    // Preserve stored data on NVS errors; do not silently erase configuration.
    ESP_RETURN_ON_ERROR(nvs_flash_init(), TAG, "NVS initialization failed");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Network initialization failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Event loop failed");
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    if (!ap) return ESP_ERR_NO_MEM;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "Wi-Fi initialization failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Wi-Fi storage failed");
    wifi_config_t config = { .ap = {
        .channel = 1, .max_connection = 2, .authmode = WIFI_AUTH_WPA2_PSK,
        .pmf_cfg = { .required = false }
    }};
    memcpy(config.ap.ssid, ssid, ssid_len);
    config.ap.ssid_len = ssid_len;
    memcpy(config.ap.password, password, password_len);
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "AP mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &config), TAG, "AP configuration failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "AP start failed");

    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.max_uri_handlers = 2;
    server_config.lru_purge_enable = true;
    server_config.recv_wait_timeout = 3;
    server_config.send_wait_timeout = 3;
    httpd_handle_t server = NULL;
    esp_err_t result = httpd_start(&server, &server_config);
    if (result == ESP_OK) {
        const httpd_uri_t index = { .uri = "/", .method = HTTP_GET, .handler = index_get };
        const httpd_uri_t status = { .uri = "/api/status", .method = HTTP_GET, .handler = status_get };
        result = httpd_register_uri_handler(server, &index);
        if (result == ESP_OK) result = httpd_register_uri_handler(server, &status);
    }
    if (result != ESP_OK) {
        if (server) httpd_stop(server);
        esp_wifi_stop();
        return result;
    }
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(ap, &info) == ESP_OK)
        ESP_LOGI(TAG, "Status page: http://" IPSTR "/", IP2STR(&info.ip));
    return ESP_OK;
}
