#include "usb_console.h"
#include "sdkconfig.h"
#if defined(CONFIG_NORDIC_SOFTWARE_TEST) && defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
#include "controller_runtime.h"
#include "ble_console.h"
#include "relay_control.h"
#include "driver/usb_serial_jtag.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

// One USB task owns these buffers; they do not consume the HTTP task's stack.
static control_service_t snapshot;
static char json[3200];
static void reply(unsigned long id,const char *body)
{
    // A single stdio call keeps log messages out of the middle of a reply.
    // Keep the existing nonblocking console output behavior when USB is absent.
    printf("\n@nordic %lu %s\n",id,body);
    fflush(stdout);
}
static void status(unsigned long id,const service_result_t *result)
{
    controller_runtime_snapshot(&snapshot);
    if(result)snapshot.last_result=*result;
    int n=control_service_json(&snapshot,esp_timer_get_time(),false,!result||result->accepted,json,sizeof(json));
    if(n<0){reply(id,"{\"ok\":false,\"error\":\"status_overflow\"}");return;}
    relay_control_status_t r=relay_control_status();
    int tail=snprintf(json+n-1,sizeof(json)-(size_t)n+1,
        ",\"relays\":{\"enabled\":%s,\"ready\":%s,\"fault\":%s,\"known\":%s,\"mask\":%u,"
        "\"button_ack_event\":%u,\"start\":%u,\"stop\":%u,\"speed_up\":%u,\"speed_down\":%u,"
        "\"incline_up\":%u,\"incline_down\":%u,\"pulse_ms\":%u}}",
        r.enabled?"true":"false",r.ready?"true":"false",r.fault?"true":"false",r.known?"true":"false",
        r.mask,(unsigned)r.button_ack_event,r.mapping.start,r.mapping.stop,r.mapping.speed_up,r.mapping.speed_down,
        r.mapping.incline_up,r.mapping.incline_down,r.mapping.pulse_ms);
    if(tail<0 || (size_t)tail>=sizeof(json)-(size_t)n+1){reply(id,"{\"ok\":false,\"error\":\"status_overflow\"}");return;}
    reply(id,json);
}
static void handle(char *line)
{
    char *body;
    if(line[0]<'0'||line[0]>'9'){reply(0,"{\"ok\":false,\"error\":\"invalid_envelope\"}");return;}
    unsigned long id=strtoul(line,&body,10);
    if(!id || id>1000000000UL || *body!=' '){reply(0,"{\"ok\":false,\"error\":\"invalid_envelope\"}");return;}
    body++;
    if(!strcmp(body,"ble-pair")){
        uint32_t code=0;esp_err_t error=ble_console_pair(&code);
        if(error==ESP_OK)snprintf(json,sizeof(json),"{\"ok\":true,\"pairing_seconds\":120,\"passkey\":\"%06" PRIu32 "\"}",code);
        else snprintf(json,sizeof(json),"{\"ok\":false,\"error\":\"%s\"}",esp_err_to_name(error));
        reply(id,json);return;
    }
    if(!strcmp(body,"ble-forget")){
        esp_err_t error=ble_console_forget();
        snprintf(json,sizeof(json),"{\"ok\":%s,\"result\":\"%s\"}",error==ESP_OK?"true":"false",error==ESP_OK?"forget_queued":esp_err_to_name(error));
        reply(id,json);return;
    }
    if(!strcmp(body,"status")){status(id,NULL);return;}
    if(!strcmp(body,"workouts")){
        printf("\n@nordic %lu {\"ok\":true,\"workouts\":%s}\n",id,test_session_workout_catalog());
        fflush(stdout);return;
    }
    service_command_t cmd={.protocol_version=1,.client={SERVICE_USB,1}};
    if(!strcmp(body,"connect")){
        do{esp_fill_random(&cmd.token,sizeof(cmd.token));}while(!cmd.token);
        cmd.action=SERVICE_REGISTER;
        service_result_t r=controller_runtime_submit_result(&cmd);
        if(!r.accepted){status(id,&r);return;}
        snprintf(json,sizeof(json),"{\"ok\":true,\"protocol_version\":1,\"token\":\"%016" PRIx64 "\",\"client_id\":%u,\"control_epoch\":%u}",
            cmd.token,(unsigned)r.client_id,(unsigned)r.epoch);
        reply(id,json);return;
    }
    if(!control_service_parse(body,strlen(body),&cmd)){
        reply(id,"{\"ok\":false,\"error\":\"malformed_command\"}");return;
    }
    service_result_t r=controller_runtime_submit_result(&cmd);
    status(id,&r);
}
static void run(void *unused)
{
    (void)unused;
    char line[192];size_t used=0;bool discard=false;
    for(;;){
        unsigned char ch;
        if(usb_serial_jtag_read_bytes(&ch,1,pdMS_TO_TICKS(50))!=1)continue;
        if(ch=='\r')continue;
        if(ch=='\n'){
            if(discard)reply(0,"{\"ok\":false,\"error\":\"invalid_line\"}");
            else if(used){line[used]=0;handle(line);}
            used=0;discard=false;
        }else if(ch<32 || ch>126 || used==sizeof(line)-1)discard=true;
        else if(!discard)line[used++]=(char)ch;
    }
}
#endif
esp_err_t usb_console_start(void)
{
#if defined(CONFIG_NORDIC_SOFTWARE_TEST) && defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    usb_serial_jtag_driver_config_t config={.tx_buffer_size=256,.rx_buffer_size=512};
    esp_err_t error=usb_serial_jtag_driver_install(&config);
    if(error!=ESP_OK)return error;
    if(xTaskCreate(run,"usb_client",6144,NULL,3,NULL)!=pdPASS){
        usb_serial_jtag_driver_uninstall();return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
