#include "web_console.h"
#include "test_session.h"
#include "esp_random.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
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
static test_session_t session;
static bool session_initialized;
extern const unsigned char page_start[] asm("_binary_index_html_start");
extern const unsigned char page_end[] asm("_binary_index_html_end");
extern const unsigned char logo_start[] asm("_binary_logo_svg_start");
extern const unsigned char logo_end[] asm("_binary_logo_svg_end");
extern const unsigned char hero_start[] asm("_binary_hero_jpg_start");
extern const unsigned char hero_end[] asm("_binary_hero_jpg_end");

void web_console_publish(const control_t *controller)
{
    portENTER_CRITICAL(&status_lock);
    if (session_initialized) test_session_tick(&session, esp_timer_get_time());
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
        "img-src 'self'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'none'");
}
static esp_err_t index_get(httpd_req_t *req)
{
    headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)page_start, page_end - page_start);
}
static esp_err_t logo_get(httpd_req_t *req)
{
    headers(req);
    httpd_resp_set_type(req, "image/svg+xml");
    return httpd_resp_send(req, (const char *)logo_start, logo_end - logo_start);
}
static esp_err_t hero_get(httpd_req_t *req)
{
    headers(req);
    httpd_resp_set_type(req, "image/jpeg");
    return httpd_resp_send(req, (const char *)hero_start, hero_end - hero_start);
}
static esp_err_t status_get(httpd_req_t *req)
{
    portENTER_CRITICAL(&status_lock);
    bool test_enabled = session.enabled;
    control_mode_t current_mode = test_enabled ? session.control.mode : mode;
    control_fault_t current_fault = test_enabled ? session.control.fault : fault;
    float speed = session.control.requested_speed_mps;
    float grade = session.control.requested_grade_percent;
    bool owner = session.owner != 0;
    int64_t sample = sampled_at_us;
    portEXIT_CRITICAL(&status_lock);
    const char *name = current_mode == CONTROL_IDLE ? "idle" :
                       current_mode == CONTROL_RUNNING ? "running" : "fault";
    char body[512];
    int length = snprintf(body, sizeof(body),
        "{\"mode\":\"%s\",\"fault\":%d,\"commissioned\":false,"
        "\"software_test\":%s,\"session_active\":%s,\"requested_speed_mps\":%.2f,"
        "\"requested_grade_percent\":%.2f,"
        "\"motion_available\":false,\"hardware_verified\":false,"
        "\"uptime_ms\":%" PRId64 ",\"sample_age_ms\":%" PRId64 "}",
        name, (int)current_fault, test_enabled ? "true" : "false",
        owner ? "true" : "false", (double)speed, (double)grade, esp_timer_get_time() / 1000,
        (esp_timer_get_time() - sample) / 1000);
    if (length < 0 || (size_t)length >= sizeof(body)) return ESP_FAIL;
    headers(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, length);
}

// Custom header requires a same-origin browser request: no CORS/preflight route
// is provided. WPA2 access remains the authentication boundary for bench tests.
static bool command_header(httpd_req_t *req)
{
    char value[8];
    return httpd_req_get_hdr_value_str(req, "X-Nordic-Test", value, sizeof(value)) == ESP_OK &&
           strcmp(value, "1") == 0;
}
static esp_err_t rejected(httpd_req_t *req, const char *status, const char *message)
{
    headers(req);
    httpd_resp_set_status(req, status);
    return httpd_resp_sendstr(req, message);
}
static esp_err_t claim_post(httpd_req_t *req)
{
    if (!command_header(req) || req->content_len != 0)
        return rejected(req, "400 Bad Request", "Invalid test request");
    uint64_t token;
    esp_fill_random(&token, sizeof(token));
    if (!token) return rejected(req, "503 Service Unavailable", "Retry claim");
    portENTER_CRITICAL(&status_lock);
    bool accepted = test_session_claim(&session, esp_timer_get_time(), token);
    portEXIT_CRITICAL(&status_lock);
    if (!accepted) return rejected(req, "409 Conflict", "Test disabled or another page owns control");
    char body[40];
    snprintf(body, sizeof(body), "{\"token\":\"%016" PRIx64 "\"}", token);
    headers(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}
static esp_err_t command_post(httpd_req_t *req)
{
    if (!command_header(req) || req->content_len <= 0 || req->content_len >= 128)
        return rejected(req, "400 Bad Request", "Invalid command length or header");
    char body[128];
    int received = 0;
    while (received < req->content_len) {
        int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (count <= 0) return ESP_FAIL;
        received += count;
    }
    body[received] = 0;
    char token_text[17], sequence_text[11];
    char action[16];
    float speed, grade;
    int end = 0;
    if (sscanf(body, "%16[0-9a-f] %10[0-9] %15s %f %f %n",
               token_text, sequence_text, action, &speed, &grade, &end) != 5 ||
        end != received || strlen(token_text) != 16 || !isfinite(speed) || !isfinite(grade))
        return rejected(req, "400 Bad Request", "Malformed test command");
    uint64_t token = strtoull(token_text, NULL, 16);
    unsigned long long sequence_value = strtoull(sequence_text, NULL, 10);
    if (!sequence_value || sequence_value > UINT32_MAX)
        return rejected(req, "400 Bad Request", "Invalid sequence");
    uint32_t sequence = (uint32_t)sequence_value;
    test_action_t operation;
    if (!strcmp(action, "start")) operation = TEST_START;
    else if (!strcmp(action, "stop")) operation = TEST_STOP;
    else if (!strcmp(action, "targets")) operation = TEST_TARGETS;
    else if (!strcmp(action, "heartbeat")) operation = TEST_HEARTBEAT;
    else if (!strcmp(action, "reset")) operation = TEST_RESET;
    else return rejected(req, "400 Bad Request", "Unknown action");
    portENTER_CRITICAL(&status_lock);
    bool accepted = test_session_command(&session, esp_timer_get_time(), token,
                                          sequence, operation, speed, grade);
    portEXIT_CRITICAL(&status_lock);
    if (!accepted) return rejected(req, "409 Conflict", "Command rejected: check session, state, and test limits");
    return status_get(req);
}

esp_err_t web_console_start(void)
{
    portENTER_CRITICAL(&status_lock);
#ifdef CONFIG_NORDIC_SOFTWARE_TEST
    test_session_init(&session, true);
#else
    test_session_init(&session, false);
#endif
    session_initialized = true;
    portEXIT_CRITICAL(&status_lock);
#ifdef CONFIG_NORDIC_SOFTWARE_TEST
    ESP_LOGW(TAG, "SOFTWARE TEST ENABLED: no physical outputs; 3 second command timeout");
#endif
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
    server_config.max_uri_handlers = 6;
    server_config.lru_purge_enable = true;
    server_config.recv_wait_timeout = 3;
    server_config.send_wait_timeout = 3;
    httpd_handle_t server = NULL;
    esp_err_t result = httpd_start(&server, &server_config);
    if (result == ESP_OK) {
        const httpd_uri_t index = { .uri = "/", .method = HTTP_GET, .handler = index_get };
        const httpd_uri_t status = { .uri = "/api/status", .method = HTTP_GET, .handler = status_get };
        const httpd_uri_t claim = { .uri = "/api/test/claim", .method = HTTP_POST, .handler = claim_post };
        const httpd_uri_t command = { .uri = "/api/test/command", .method = HTTP_POST, .handler = command_post };
        const httpd_uri_t logo = { .uri = "/assets/logo.svg", .method = HTTP_GET, .handler = logo_get };
        const httpd_uri_t hero = { .uri = "/assets/hero.jpg", .method = HTTP_GET, .handler = hero_get };
        result = httpd_register_uri_handler(server, &index);
        if (result == ESP_OK) result = httpd_register_uri_handler(server, &status);
        if (result == ESP_OK) result = httpd_register_uri_handler(server, &claim);
        if (result == ESP_OK) result = httpd_register_uri_handler(server, &command);
        if (result == ESP_OK) result = httpd_register_uri_handler(server, &logo);
        if (result == ESP_OK) result = httpd_register_uri_handler(server, &hero);
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
