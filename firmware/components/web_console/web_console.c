#include "web_console.h"
#include "relay_control.h"
#include "controller_runtime.h"
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
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "web_console";
extern const unsigned char page_start[] asm("_binary_index_html_start");
extern const unsigned char page_end[] asm("_binary_index_html_end");
extern const unsigned char logo_start[] asm("_binary_logo_svg_start");
extern const unsigned char logo_end[] asm("_binary_logo_svg_end");
extern const unsigned char hero_start[] asm("_binary_hero_jpg_start");
extern const unsigned char hero_end[] asm("_binary_hero_jpg_end");

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
static esp_err_t workouts_get(httpd_req_t *req)
{
    headers(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, test_session_workout_catalog());
}
static esp_err_t send_status(httpd_req_t *req,const service_result_t *result)
{
    // This server runs synchronous handlers on one HTTP task. Keep its large
    // scratch buffers off the 6 KiB task stack, leaving room for JSON formatting
    // and lwIP. Revisit this ownership if handlers become asynchronous or another
    // server instance is introduced. USB uses its own independent buffers.
    static control_service_t snapshot;
    static char body[3200];
    controller_runtime_snapshot(&snapshot);
    if(result)snapshot.last_result=*result;
    int length=control_service_json(&snapshot,esp_timer_get_time(),false,!result || result->accepted,body,sizeof(body));
    if (length < 0 || (size_t)length >= sizeof(body)) return ESP_FAIL;
    relay_control_status_t relay=relay_control_status();
    int tail=snprintf(body+length-1,sizeof(body)-(size_t)length+1,
        ",\"relays\":{\"enabled\":%s,\"ready\":%s,\"fault\":%s,\"known\":%s,\"mask\":%u,\"button_ack_event\":%u,"
        "\"start\":%u,\"stop\":%u,\"speed_up\":%u,\"speed_down\":%u,\"incline_up\":%u,\"incline_down\":%u,\"pulse_ms\":%u}}",
        relay.enabled?"true":"false",relay.ready?"true":"false",relay.fault?"true":"false",
        relay.known?"true":"false",relay.mask,(unsigned)relay.button_ack_event,relay.mapping.start,relay.mapping.stop,relay.mapping.speed_up,
        relay.mapping.speed_down,relay.mapping.incline_up,relay.mapping.incline_down,relay.mapping.pulse_ms);
    if(tail<0 || (size_t)tail>=sizeof(body)-(size_t)length+1)return ESP_FAIL;
    length+=tail-1;
    headers(req);
    httpd_resp_set_type(req, "application/json");
    esp_err_t sent=httpd_resp_send(req, body, length);
    // ESP-IDF reports this high-water mark in bytes. Log after the send so the
    // first sample includes both JSON formatting and the network response path.
    static bool stack_reported;
    if(!stack_reported){
        ESP_LOGI(TAG,"HTTP response minimum stack headroom: %u bytes",
            (unsigned)uxTaskGetStackHighWaterMark(NULL));
        stack_reported=true;
    }
    return sent;
}

static esp_err_t status_get(httpd_req_t *req){return send_status(req,NULL);}

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
static esp_err_t connect_post(httpd_req_t *req)
{
    if (!command_header(req) || req->content_len != 0)
        return rejected(req, "400 Bad Request", "Invalid test request");
    uint64_t token;
    esp_fill_random(&token, sizeof(token));
    if (!token) return rejected(req, "503 Service Unavailable", "Retry claim");
    service_command_t command={.protocol_version=1,.action=strstr(req->uri,"claim")?SERVICE_CLAIM:SERVICE_REGISTER,.client={SERVICE_HTTP,1},.token=token};
    service_result_t result=controller_runtime_submit_result(&command);
    if(!result.accepted){httpd_resp_set_status(req,"409 Conflict");return send_status(req,&result);}
    char body[160];
    snprintf(body,sizeof(body),"{\"protocol_version\":1,\"token\":\"%016" PRIx64 "\",\"client_id\":%u,\"control_epoch\":%u}",token,(unsigned)result.client_id,(unsigned)result.epoch);
    headers(req);httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,body);
}

static esp_err_t command_post(httpd_req_t *req)
{
    if (!command_header(req) || req->content_len <= 0 || req->content_len >= 160)
        return rejected(req, "400 Bad Request", "Invalid command length or header");
    char body[160];
    int received = 0;
    while (received < req->content_len) {
        int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (count <= 0) return ESP_FAIL;
        received += count;
    }
    body[received] = 0;
    service_command_t command={.client={SERVICE_HTTP,1}};
    if(!control_service_parse(body,(size_t)received,&command))
        return rejected(req,"400 Bad Request","Malformed v1 command");
    service_result_t result=controller_runtime_submit_result(&command);
    if(!result.accepted)httpd_resp_set_status(req,"409 Conflict");
    return send_status(req,&result);
}

esp_err_t web_console_start(void)
{
#ifdef CONFIG_NORDIC_SOFTWARE_TEST
    ESP_LOGW(TAG, "CONTROL TEST ENABLED: injected motion feedback; 3 second command timeout");
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
    server_config.stack_size = 6144;
    server_config.max_uri_handlers = 8;
    server_config.lru_purge_enable = true;
    server_config.recv_wait_timeout = 3;
    server_config.send_wait_timeout = 3;
    httpd_handle_t server = NULL;
    esp_err_t result = httpd_start(&server, &server_config);
    if (result == ESP_OK) {
        const httpd_uri_t index = { .uri = "/", .method = HTTP_GET, .handler = index_get };
        const httpd_uri_t workouts = { .uri = "/api/workouts", .method = HTTP_GET, .handler = workouts_get };
        const httpd_uri_t status = { .uri = "/api/status", .method = HTTP_GET, .handler = status_get };
        const httpd_uri_t claim = { .uri = "/api/test/claim", .method = HTTP_POST, .handler = connect_post };
        const httpd_uri_t connect = { .uri = "/api/session", .method = HTTP_POST, .handler = connect_post };
        const httpd_uri_t command = { .uri = "/api/test/command", .method = HTTP_POST, .handler = command_post };
        const httpd_uri_t logo = { .uri = "/assets/logo.svg", .method = HTTP_GET, .handler = logo_get };
        const httpd_uri_t hero = { .uri = "/assets/hero.jpg", .method = HTTP_GET, .handler = hero_get };
        result = httpd_register_uri_handler(server, &index);
        if (result == ESP_OK) result = httpd_register_uri_handler(server, &workouts);
        if (result == ESP_OK) result = httpd_register_uri_handler(server, &status);
        if (result == ESP_OK) result = httpd_register_uri_handler(server, &claim);
        if (result == ESP_OK) result = httpd_register_uri_handler(server, &connect);
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
