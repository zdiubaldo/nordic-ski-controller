#include "ble_console.h"
#include "sdkconfig.h"
#ifdef CONFIG_NORDIC_BLE
#include "ble_frame.h"
#include "controller_runtime.h"
#include "relay_control.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#if !CONFIG_BT_NIMBLE_SM_SC || CONFIG_BT_NIMBLE_SM_LEGACY || !CONFIG_BT_NIMBLE_NVS_PERSIST
#error "Nordic BLE requires Secure Connections only and persistent bonds"
#endif
#if CONFIG_BT_NIMBLE_MAX_CONNECTIONS < 2
#error "Nordic BLE requires at least two NimBLE connections"
#endif
#define PEERS 2
#define RESULTS 8
#define RESULT_SIZE 256
#define STATUS_SIZE 3200
#define INVALID_HANDLE 0xffff
#define WINDOW_US 120000000
#ifdef CONFIG_NORDIC_BLE_NO_PIN_TEST
#define REQUIRE_MITM 0
#define COMMAND_AUTH_FLAGS 0
#else
#define REQUIRE_MITM 1
#define COMMAND_AUTH_FLAGS BLE_GATT_CHR_F_WRITE_AUTHEN
#endif
// UUID: 75e60000-6b92-4e3d-a347-8bc06c51d210; characteristic suffix 0001..0005.
#define UUID(n) BLE_UUID128_INIT(0x10,0xd2,0x51,0x6c,0xc0,0x8b,0x47,0xa3,0x3d,0x4e,0x92,0x6b,n,0,0xe6,0x75)
static const ble_uuid128_t service_uuid=UUID(0), info_uuid=UUID(1), command_uuid=UUID(2), result_uuid=UUID(3), status_uuid=UUID(4), catalog_uuid=UUID(5);
typedef struct { uint16_t id,length,offset; char text[RESULT_SIZE]; } result_message_t;
typedef struct {
    bool used,closing,authenticated,pairing,results_sub,status_sub,catalog_sub,want_status,want_catalog;
    uint16_t handle,serial,status_id,catalog_id,status_length,status_offset,catalog_offset;
    uint64_t token,revision;
    int64_t last_status_us;
    ble_frame_rx_t rx;
    unsigned reserved,head,count;
    result_message_t results[RESULTS];
    char status[STATUS_SIZE];
} peer_t;
typedef struct { unsigned slot; uint16_t serial,id; unsigned special; service_command_t command; } job_t;
static peer_t peers[PEERS];
static SemaphoreHandle_t lock;
static QueueHandle_t jobs,stops;
static uint16_t result_handle,status_handle,catalog_handle,next_serial;
static uint8_t own_address;
static atomic_bool ready;
static bool forget_requested;
static char info_json[256];
static int64_t pairing_until;
static uint32_t pairing_passkey;
static control_service_t snapshot;
static int gap_event(struct ble_gap_event *event,void *arg);
void ble_store_config_init(void);
static peer_t *find_peer(uint16_t handle)
{
    for(unsigned i=0;i<PEERS;i++)if(peers[i].used&&peers[i].handle==handle&&!peers[i].closing)return &peers[i];
    return NULL;
}
static void advertise(void)
{
    if(!ready||ble_gap_adv_active())return;
    xSemaphoreTake(lock,portMAX_DELAY);
    unsigned used=0;for(unsigned i=0;i<PEERS;i++)used+=peers[i].used;
    xSemaphoreGive(lock);
    if(used==PEERS)return;
    struct ble_hs_adv_fields fields={.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP,
        .uuids128=(ble_uuid128_t *)&service_uuid,.num_uuids128=1,.uuids128_is_complete=1};
    if(ble_gap_adv_set_fields(&fields))return;
    const char *name="Nordic Ski";
    struct ble_hs_adv_fields response={.name=(uint8_t *)name,.name_len=10,.name_is_complete=1};
    if(ble_gap_adv_rsp_set_fields(&response))return;
    struct ble_gap_adv_params params={.conn_mode=BLE_GAP_CONN_MODE_UND,.disc_mode=BLE_GAP_DISC_MODE_GEN,
        .itvl_min=160,.itvl_max=240};
    ble_gap_adv_start(own_address,NULL,BLE_HS_FOREVER,&params,gap_event,NULL);
}
static int access_characteristic(uint16_t connection,uint16_t attribute,struct ble_gatt_access_ctxt *ctx,void *arg)
{
    (void)attribute;
    unsigned kind=(unsigned)(uintptr_t)arg;
    if(kind==5){
        const char *info=info_json;
        return os_mbuf_append(ctx->om,info,strlen(info))?BLE_ATT_ERR_INSUFFICIENT_RES:0;
    }
    if(kind!=1||ctx->op!=BLE_GATT_ACCESS_OP_WRITE_CHR)return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    struct ble_gap_conn_desc desc;
    if(ble_gap_conn_find(connection,&desc)||!desc.sec_state.encrypted||(REQUIRE_MITM&&!desc.sec_state.authenticated)||!desc.sec_state.bonded)
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    // Do not wait in the host callback, including when the worker is publishing.
    if(xSemaphoreTake(lock,0)!=pdTRUE)return BLE_ATT_ERR_INSUFFICIENT_RES;
    peer_t *p=find_peer(connection);
    if(!p||!p->authenticated||!p->results_sub){xSemaphoreGive(lock);return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;}
    uint8_t frame[BLE_FRAME_HEADER+BLE_COMMAND_MAX];uint16_t length;
    if(ble_hs_mbuf_to_flat(ctx->om,frame,sizeof(frame),&length)){
        memset(&p->rx,0,sizeof(p->rx));xSemaphoreGive(lock);return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    ble_frame_result_t result=ble_frame_receive(&p->rx,frame,length,esp_timer_get_time());
    if(result!=BLE_FRAME_COMPLETE){xSemaphoreGive(lock);return result<0?BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN:0;}
    job_t job={.slot=(unsigned)(p-peers),.serial=p->serial,.id=p->rx.id,
        .command={.protocol_version=1,.client={SERVICE_BLE,p->serial},.token=p->token}};
    if(!strcmp(p->rx.data,"connect")){job.special=1;job.command.action=SERVICE_REGISTER;}
    else if(!strcmp(p->rx.data,"status"))job.special=2;
    else if(!strcmp(p->rx.data,"workouts"))job.special=3;
    else if(!control_service_parse(p->rx.data,strlen(p->rx.data),&job.command)||job.command.token!=p->token){
        xSemaphoreGive(lock);return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    // Reserve one result slot per accepted request; no command result is silently evicted.
    bool is_stop=!job.special&&job.command.action==SERVICE_STOP;
    if(p->reserved>=(is_stop?RESULTS:RESULTS-1)){xSemaphoreGive(lock);return BLE_ATT_ERR_INSUFFICIENT_RES;}
    QueueHandle_t queue=is_stop?stops:jobs;
    if(xQueueSend(queue,&job,0)!=pdTRUE){xSemaphoreGive(lock);return BLE_ATT_ERR_INSUFFICIENT_RES;}
    p->reserved++;xSemaphoreGive(lock);return 0;
}
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&service_uuid.u,.characteristics=(struct ble_gatt_chr_def[]){
        {.uuid=&info_uuid.u,.access_cb=access_characteristic,.arg=(void *)5,.flags=BLE_GATT_CHR_F_READ},
        {.uuid=&command_uuid.u,.access_cb=access_characteristic,.arg=(void *)1,.flags=BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_ENC|COMMAND_AUTH_FLAGS},
        {.uuid=&result_uuid.u,.access_cb=access_characteristic,.flags=BLE_GATT_CHR_F_NOTIFY,.val_handle=&result_handle},
        {.uuid=&status_uuid.u,.access_cb=access_characteristic,.flags=BLE_GATT_CHR_F_NOTIFY,.val_handle=&status_handle},
        {.uuid=&catalog_uuid.u,.access_cb=access_characteristic,.flags=BLE_GATT_CHR_F_NOTIFY,.val_handle=&catalog_handle},
        {0}}}, {0}};
static int gap_event(struct ble_gap_event *event,void *arg)
{
    (void)arg;
    switch(event->type){
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI("nordic_ble","connect status=%d",event->connect.status);
        if(!event->connect.status){
            uint16_t h=event->connect.conn_handle;
            xSemaphoreTake(lock,portMAX_DELAY);
            peer_t *p=NULL;for(unsigned i=0;i<PEERS;i++)if(!peers[i].used){p=&peers[i];break;}
            if(p){
                memset(p,0,sizeof(*p));p->used=true;p->handle=h;
                if(!++next_serial)++next_serial;
                p->serial=next_serial;p->want_status=true;
                do{esp_fill_random(&p->token,sizeof(p->token));}while(!p->token);
            }
            xSemaphoreGive(lock);
            if(!p)ble_gap_terminate(h,BLE_ERR_REM_USER_CONN_TERM);
            else {int rc=ble_gap_security_initiate(h);ESP_LOGI("nordic_ble","security initiate handle=%u result=%d",h,rc);}
        }
        advertise();break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI("nordic_ble","disconnect reason=%d",event->disconnect.reason);
        xSemaphoreTake(lock,portMAX_DELAY);
        {peer_t *p=find_peer(event->disconnect.conn.conn_handle);if(p){p->closing=true;p->authenticated=false;}}
        xSemaphoreGive(lock);break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        xSemaphoreTake(lock,portMAX_DELAY);
        {peer_t *p=find_peer(event->subscribe.conn_handle);if(p){
            if(event->subscribe.attr_handle==result_handle)p->results_sub=event->subscribe.cur_notify;
            if(event->subscribe.attr_handle==status_handle){p->status_sub=event->subscribe.cur_notify;p->want_status=true;}
            if(event->subscribe.attr_handle==catalog_handle)p->catalog_sub=event->subscribe.cur_notify;
        }}
        xSemaphoreGive(lock);break;
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        uint16_t h=event->passkey.conn_handle;
        xSemaphoreTake(lock,portMAX_DELAY);
        bool allowed=esp_timer_get_time()<pairing_until&&event->passkey.params.action==BLE_SM_IOACT_DISP;
        uint32_t code=pairing_passkey;
        peer_t *p=find_peer(h);if(p)p->pairing=allowed;else allowed=false;
        xSemaphoreGive(lock);
        ESP_LOGI("nordic_ble","passkey action=%u allowed=%d",event->passkey.params.action,allowed);
        if(!allowed){ble_gap_terminate(h,BLE_ERR_AUTH_FAIL);break;}
        struct ble_sm_io io={.action=BLE_SM_IOACT_DISP,.passkey=code};
        if(ble_sm_inject_io(h,&io))ble_gap_terminate(h,BLE_ERR_AUTH_FAIL);
        break;
    }
    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc desc;uint16_t h=event->enc_change.conn_handle;
        bool found=ble_gap_conn_find(h,&desc)==0;
        ESP_LOGI("nordic_ble","encryption status=%d found=%d encrypted=%d authenticated=%d bonded=%d",event->enc_change.status,found,found?desc.sec_state.encrypted:0,found?desc.sec_state.authenticated:0,found?desc.sec_state.bonded:0);
        bool secure=!event->enc_change.status&&found&&desc.sec_state.encrypted&&(!REQUIRE_MITM||desc.sec_state.authenticated)&&desc.sec_state.bonded;
        xSemaphoreTake(lock,portMAX_DELAY);
        peer_t *p=find_peer(h);
        bool expired=p&&p->pairing&&esp_timer_get_time()>=pairing_until;
        if(p){p->authenticated=secure&&!expired;p->pairing=false;}
        xSemaphoreGive(lock);
        if(!secure||expired){
            if(expired&&found)ble_store_util_delete_peer(&desc.peer_id_addr);
            ble_gap_terminate(h,BLE_ERR_AUTH_FAIL);
        }
        break;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        ESP_LOGW("nordic_ble","repeat pairing rejected: peer already has a stored bond");
        // Never replace an existing bond silently; USB forget is required.
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    case BLE_GAP_EVENT_ADV_COMPLETE:advertise();break;
    default:break;
    }
    return 0;
}
static void sync_host(void)
{
    if(ble_hs_util_ensure_addr(0)||ble_hs_id_infer_auto(0,&own_address))return;
    ready=true;advertise();
}
static void reset_host(int reason)
{
    (void)reason;ready=false;
    xSemaphoreTake(lock,portMAX_DELAY);
    for(unsigned i=0;i<PEERS;i++)if(peers[i].used){peers[i].closing=true;peers[i].authenticated=false;}
    xSemaphoreGive(lock);
}
static void host_task(void *unused)
{
    (void)unused;nimble_port_run();nimble_port_freertos_deinit();
}
static void finish_job(const job_t *job,const char *text)
{
    xSemaphoreTake(lock,portMAX_DELAY);
    peer_t *p=&peers[job->slot];
    if(p->used&&!p->closing&&p->serial==job->serial){
        result_message_t *r=&p->results[(p->head+p->count)%RESULTS];
        r->id=job->id;r->offset=0;
        r->length=(uint16_t)snprintf(r->text,sizeof(r->text),"%s",text);
        p->count++;
    }
    xSemaphoreGive(lock);
}
static void execute_job(job_t *job)
{
    xSemaphoreTake(lock,portMAX_DELAY);
    peer_t *p=&peers[job->slot];
    bool valid=p->used&&!p->closing&&p->authenticated&&p->serial==job->serial;
    bool catalog_sub=p->catalog_sub;
    xSemaphoreGive(lock);
    if(!valid)return;
    char result[RESULT_SIZE];
    if(job->special==2||job->special==3){
        bool ok=job->special==2||catalog_sub;
        if(job->special==3&&strlen(test_session_workout_catalog())>UINT16_MAX)ok=false;
        xSemaphoreTake(lock,portMAX_DELAY);
        if(p->used&&p->serial==job->serial){
            if(job->special==2)p->want_status=true;
            else if(ok){p->want_catalog=true;p->catalog_id=job->id;p->catalog_offset=0;}
        }
        xSemaphoreGive(lock);
        snprintf(result,sizeof(result),"{\"ok\":%s,\"reason\":\"%s\"}",ok?"true":"false",ok?"ok":"catalog_unavailable_or_not_subscribed");
    }else{
        service_result_t r=controller_runtime_submit_result(&job->command);
        if(job->special==1&&r.accepted)
            snprintf(result,sizeof(result),"{\"ok\":true,\"protocol_version\":1,\"token\":\"%016" PRIx64 "\",\"client_id\":%u,\"control_epoch\":%u}",job->command.token,(unsigned)r.client_id,(unsigned)r.epoch);
        else snprintf(result,sizeof(result),"{\"ok\":%s,\"reason\":\"%s\",\"client_id\":%u,\"sequence\":%u,\"control_epoch\":%u,\"revision\":\"%" PRIu64 "\"}",
            r.accepted?"true":"false",control_service_reason(r.code),(unsigned)r.client_id,(unsigned)r.sequence,(unsigned)r.epoch,r.revision);
    }
    finish_job(job,result);
}
// Transmit from a NimBLE host event: handle lifetime cannot change mid-send.
static struct ble_npl_event transmit_event;
static void transmit(struct ble_npl_event *event)
{
    (void)event;
    uint8_t frame[244];
    for(unsigned i=0;i<PEERS;i++){
        xSemaphoreTake(lock,portMAX_DELAY);
        peer_t *p=&peers[i];
        if(!p->used||p->closing||!p->authenticated){xSemaphoreGive(lock);continue;}
        unsigned mtu=ble_att_mtu(p->handle);
        size_t capacity=mtu>3?mtu-3:20;if(capacity>sizeof(frame))capacity=sizeof(frame);
        // Results, status and catalog each get one fragment per pass; a long catalog
        // cannot starve command results or status.
        for(unsigned channel=0;channel<3;channel++){
            size_t n=0;uint16_t attribute=0;
            result_message_t *r=&p->results[p->head];
            if(channel==0&&p->count&&p->results_sub){
                attribute=result_handle;n=ble_frame_encode(frame,capacity,2,r->id,r->text,r->length,r->offset);
            }else if(channel==1&&p->status_length&&p->status_sub){
                attribute=status_handle;n=ble_frame_encode(frame,capacity,3,p->status_id,p->status,p->status_length,p->status_offset);
            }else if(channel==2&&p->want_catalog&&p->catalog_sub){
                attribute=catalog_handle;const char *catalog=test_session_workout_catalog();
                n=ble_frame_encode(frame,capacity,4,p->catalog_id,catalog,strlen(catalog),p->catalog_offset);
            }
            if(!n)continue;
            struct os_mbuf *buffer=ble_hs_mbuf_from_flat(frame,n);
            if(!buffer)break;
            int rc=ble_gatts_notify_custom(p->handle,attribute,buffer);
            if(rc)break;
            uint16_t advance=(uint16_t)(n-BLE_FRAME_HEADER);
            if(channel==0){r->offset+=advance;if(r->offset==r->length){p->head=(p->head+1)%RESULTS;p->count--;p->reserved--;}}
            else if(channel==1){p->status_offset+=advance;if(p->status_offset==p->status_length){p->status_length=0;p->status_offset=0;}}
            else{p->catalog_offset+=advance;if(p->catalog_offset==strlen(test_session_workout_catalog())){p->want_catalog=false;p->catalog_offset=0;}}
        }
        xSemaphoreGive(lock);
    }
}
static void disconnect_service(peer_t *p)
{
    controller_runtime_snapshot(&snapshot);
    for(unsigned n=0;n<SERVICE_MAX_CLIENTS;n++){
        service_connection_t *c=&snapshot.clients[n];
        if(c->token==p->token&&c->client.transport==SERVICE_BLE&&c->client.peer==p->serial){
            service_command_t command={.protocol_version=1,.client=c->client,.token=c->token,.sequence=c->sequence+1,.action=SERVICE_DISCONNECT};
            controller_runtime_submit_result(&command);break;
        }
    }
}
static void worker(void *unused)
{
    (void)unused;
    for(;;){
        // Closing slots are retained until the service unregister completes; a
        // recycled connection handle cannot execute an old queued command.
        for(unsigned i=0;i<PEERS;i++){
            xSemaphoreTake(lock,portMAX_DELAY);bool closing=peers[i].used&&peers[i].closing;xSemaphoreGive(lock);
            if(closing){disconnect_service(&peers[i]);xSemaphoreTake(lock,portMAX_DELAY);memset(&peers[i],0,sizeof(peers[i]));xSemaphoreGive(lock);}
        }
        job_t job;
        if(xQueueReceive(stops,&job,0)==pdTRUE)execute_job(&job);
        else if(xQueueReceive(jobs,&job,0)==pdTRUE){
            job_t stop;while(xQueueReceive(stops,&stop,0)==pdTRUE)execute_job(&stop);
            execute_job(&job);
        }
        controller_runtime_snapshot(&snapshot);
        int64_t now=esp_timer_get_time();
        relay_control_status_t relays=relay_control_status();
        xSemaphoreTake(lock,portMAX_DELAY);
        for(unsigned i=0;i<PEERS;i++){
            peer_t *p=&peers[i];
            if(p->used&&!p->closing&&p->authenticated&&p->status_sub&&!p->status_length&&now-p->last_status_us>=200000&&
                (p->want_status||p->revision!=snapshot.revision)){
                int n=control_service_json(&snapshot,now,false,true,p->status,sizeof(p->status));
                if(n>0){
                    int tail=snprintf(p->status+n-1,sizeof(p->status)-(size_t)n+1,
                        ",\"relays\":{\"enabled\":%s,\"ready\":%s,\"known\":%s,\"fault\":%s,\"mask\":%u}}",
                        relays.enabled?"true":"false",relays.ready?"true":"false",relays.known?"true":"false",relays.fault?"true":"false",relays.mask);
                    if(tail<0||(size_t)tail>=sizeof(p->status)-(size_t)n+1)n=-1;else n=n-1+tail;
                }
                if(n>0){p->status_length=(uint16_t)n;p->status_offset=0;if(!++p->status_id)++p->status_id;
                    p->last_status_us=now;p->revision=snapshot.revision;p->want_status=false;}
            }
        }
        xSemaphoreGive(lock);
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&transmit_event);
        advertise();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
esp_err_t ble_console_pair(uint32_t *passkey)
{
    if(!ready||!passkey)return ESP_ERR_INVALID_STATE;
    control_service_t state;controller_runtime_snapshot(&state);
    if(state.session.control.mode!=CONTROL_IDLE)return ESP_ERR_INVALID_STATE;
    uint32_t code=esp_random()%1000000;
    xSemaphoreTake(lock,portMAX_DELAY);pairing_passkey=code;pairing_until=esp_timer_get_time()+WINDOW_US;xSemaphoreGive(lock);
    *passkey=code;return ESP_OK;
}
static struct ble_npl_event forget_event;
static void forget_bonds(struct ble_npl_event *event)
{
    (void)event;
    xSemaphoreTake(lock,portMAX_DELAY);pairing_until=0;
    uint16_t handles[PEERS];for(unsigned i=0;i<PEERS;i++){handles[i]=peers[i].used?peers[i].handle:INVALID_HANDLE;peers[i].authenticated=false;if(peers[i].used)peers[i].closing=true;}
    xSemaphoreGive(lock);
    for(unsigned i=0;i<PEERS;i++)if(handles[i]!=INVALID_HANDLE)ble_gap_terminate(handles[i],BLE_ERR_REM_USER_CONN_TERM);
    ble_store_clear();
    xSemaphoreTake(lock,portMAX_DELAY);forget_requested=false;xSemaphoreGive(lock);
}
esp_err_t ble_console_forget(void)
{
    if(!ready)return ESP_ERR_INVALID_STATE;
    control_service_t state;controller_runtime_snapshot(&state);
    if(state.session.control.mode!=CONTROL_IDLE)return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lock,portMAX_DELAY);
    if(forget_requested){xSemaphoreGive(lock);return ESP_ERR_INVALID_STATE;}
    forget_requested=true;xSemaphoreGive(lock);
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&forget_event);return ESP_OK;
}
esp_err_t ble_console_start(void)
{
    esp_err_t error=nvs_flash_init();if(error!=ESP_OK)return error;
    lock=xSemaphoreCreateMutex();jobs=xQueueCreate(8,sizeof(job_t));stops=xQueueCreate(4,sizeof(job_t));
    if(!lock||!jobs||!stops)return ESP_ERR_NO_MEM;
    error=nimble_port_init();if(error!=ESP_OK)return error;
    uint8_t mac[6];error=esp_read_mac(mac,ESP_MAC_BT);if(error!=ESP_OK)return error;
    controller_runtime_snapshot(&snapshot);
    snprintf(info_json,sizeof(info_json),"{\"protocol_version\":1,\"framing\":1,\"command_max\":191,\"connections\":2,\"device_id\":\"%02x%02x%02x%02x%02x%02x\",\"boot_id\":%u,\"numeric_targets\":true,\"workouts\":true,\"calibrated_targets\":false}",
        mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],(unsigned)snapshot.boot_id);
    ble_hs_cfg.sync_cb=sync_host;ble_hs_cfg.reset_cb=reset_host;
    ble_hs_cfg.sm_io_cap=REQUIRE_MITM?BLE_HS_IO_DISPLAY_ONLY:BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding=1;ble_hs_cfg.sm_mitm=REQUIRE_MITM;ble_hs_cfg.sm_sc=1;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();ble_svc_gatt_init();ble_svc_gap_device_name_set("Nordic Ski");
    if(ble_gatts_count_cfg(services)||ble_gatts_add_svcs(services))return ESP_FAIL;
    ble_store_config_init();
    ble_npl_event_init(&transmit_event,transmit,NULL);ble_npl_event_init(&forget_event,forget_bonds,NULL);
    if(xTaskCreate(worker,"ble_client",6144,NULL,3,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
    nimble_port_freertos_init(host_task);return ESP_OK;
}
#else
esp_err_t ble_console_start(void){return ESP_ERR_NOT_SUPPORTED;}
esp_err_t ble_console_pair(uint32_t *passkey){(void)passkey;return ESP_ERR_NOT_SUPPORTED;}
esp_err_t ble_console_forget(void){return ESP_ERR_NOT_SUPPORTED;}
#endif
