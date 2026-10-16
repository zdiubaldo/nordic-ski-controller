#include "relay_control.h"
#include "sdkconfig.h"
#ifdef CONFIG_NORDIC_RELAY_CONTROL
#include "relay_idf.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
static SemaphoreHandle_t lock;
static relay_t driver;
static relay_control_status_t status;
static bool must_stop, explicit_stop;
static struct {bool running;float speed,grade;unsigned button;uint32_t button_event;int64_t now;} demand;
static void run(void *unused)
{
    (void)unused;
    relay_mapping_state_t mapping={.map=status.mapping};
    bool fault=false;uint32_t seen_button=0,pending_ack=0;
    for(;;){
        int64_t now=esp_timer_get_time();
        xSemaphoreTake(lock,portMAX_DELAY);
        bool active=demand.running && !must_stop;
        bool stop_requested=explicit_stop;
        must_stop=false;explicit_stop=false;
        float speed=demand.speed,grade=demand.grade;
        unsigned button=demand.button;uint32_t event=demand.button_event;
        int64_t age=now-demand.now;
        xSemaphoreGive(lock);
        // Independent freshness watchdog if the control task stalls.
        if(active && (age<0 || age>500000))fault=true;
        relay_mapping_update(&mapping,active&&!fault,stop_requested,speed,grade,now);
        if(event!=seen_button){
            seen_button=event;
            if(active && !fault && button && relay_mapping_press(&mapping,button,now))pending_ack=event;
        }
        uint8_t mask=relay_mapping_mask(&mapping,now);
        if(!fault && !relay_set(&driver,mask))fault=true;
        if(fault){
            // Retry only inactive output, never resume an old demand.
            relay_initialize(&driver);
        }
        xSemaphoreTake(lock,portMAX_DELAY);
        if(!fault && pending_ack && mask && now>=mapping.start_end && now>=mapping.stop_end){
            status.button_ack_event=pending_ack;pending_ack=0;
        }
        if(!active || fault)pending_ack=0;
        status.ready=!fault&&driver.ready;status.fault=fault;
        status.known=driver.ready;status.mask=driver.commanded_mask;status.sampled_us=esp_timer_get_time();
        xSemaphoreGive(lock);
        vTaskDelay(pdMS_TO_TICKS(fault?200:20));
    }
}
void relay_control_start(void)
{
    status.enabled=true;status.fault=true;
    status.mapping=(relay_mapping_t){CONFIG_NORDIC_RELAY_START,CONFIG_NORDIC_RELAY_STOP,CONFIG_NORDIC_RELAY_SPEED_UP,
        CONFIG_NORDIC_RELAY_SPEED_DOWN,CONFIG_NORDIC_RELAY_INCLINE_UP,
        CONFIG_NORDIC_RELAY_INCLINE_DOWN,CONFIG_NORDIC_RELAY_PULSE_MS};
    lock=xSemaphoreCreateMutex();
    if(!lock)return;
    i2c_master_bus_config_t config={.i2c_port=I2C_NUM_0,.sda_io_num=42,.scl_io_num=41,
        .clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};
    i2c_master_bus_handle_t bus;
    if(i2c_new_master_bus(&config,&bus)!=ESP_OK)return;
    i2c_device_config_t dc={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x20,.scl_speed_hz=100000};
    i2c_master_dev_handle_t device;
    if(i2c_master_bus_add_device(bus,&dc,&device)!=ESP_OK)return;
    relay_prepare_idf(&driver,device,0); // Waveshare demo: active high CH1..8.
    if(!relay_initialize(&driver))return;
    // Even invalid mappings must first clear retained outputs after an MCU reset.
    if(!relay_mapping_valid(status.mapping))return;
    status.ready=true;status.known=true;status.fault=false;status.sampled_us=esp_timer_get_time();
    if(xTaskCreate(run,"relay_control",3072,NULL,4,NULL)!=pdPASS){status.ready=false;status.fault=true;return;}
    ESP_LOGW("relay_control","Unloaded relay controls ready: start=%u stop=%u speed=%u/%u incline=%u/%u pulse=%ums",
        status.mapping.start,status.mapping.stop,status.mapping.speed_up,status.mapping.speed_down,
        status.mapping.incline_up,status.mapping.incline_down,status.mapping.pulse_ms);
}
void relay_control_update(bool running,bool stop_requested,float speed,float grade,unsigned button,uint32_t button_event,int64_t now)
{
    if(!lock)return;
    xSemaphoreTake(lock,portMAX_DELAY);
    if(!running || stop_requested)must_stop=true;
    explicit_stop|=stop_requested;
    demand.running=running;demand.speed=speed;demand.grade=grade;demand.button=button;demand.button_event=button_event;demand.now=now;
    xSemaphoreGive(lock);
}
relay_control_status_t relay_control_status(void)
{
    if(!lock)return status;
    xSemaphoreTake(lock,portMAX_DELAY);relay_control_status_t copy=status;xSemaphoreGive(lock);
    return copy;
}
#else
void relay_control_start(void){}
void relay_control_update(bool running,bool stop_requested,float speed,float grade,unsigned button,uint32_t button_event,int64_t now){(void)running;(void)stop_requested;(void)speed;(void)grade;(void)button;(void)button_event;(void)now;}
relay_control_status_t relay_control_status(void){return (relay_control_status_t){0};}
#endif
