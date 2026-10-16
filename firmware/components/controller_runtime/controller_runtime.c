#include "controller_runtime.h"
#include "relay_control.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
static control_service_t service, published;
static motion_memory_t memory;
typedef struct {service_command_t command;service_result_t result;SemaphoreHandle_t done;} job_t;
static QueueHandle_t commands, stops;
static SemaphoreHandle_t snapshot_lock;
static bool started;
static void publish(void)
{
    xSemaphoreTake(snapshot_lock,portMAX_DELAY);published=service;xSemaphoreGive(snapshot_lock);
}
static void run(void *unused)
{
    (void)unused;job_t *pending=NULL;
    for(;;){
        job_t *job=NULL;
        if(xQueueReceive(stops,&job,0)!=pdTRUE){
            if(!pending)xQueueReceive(commands,&pending,pdMS_TO_TICKS(20));
            // Stop wins even if it arrived while the normal queue was being read.
            if(xQueueReceive(stops,&job,0)!=pdTRUE){job=pending;pending=NULL;}
        }
        int64_t now=esp_timer_get_time();
        relay_control_status_t relays=relay_control_status();
        memory.fail_read=relays.enabled && (!relays.ready || relays.fault || now-relays.sampled_us>500000);
        if(relays.enabled && relays.known && relays.mask && service.button_busy_until<now+20000)
            service.button_busy_until=now+20000;
        control_service_memory_sample(&memory,now);
        if(job)job->result=control_service_execute(&service,now,&job->command);
        else control_service_tick(&service,now);
        bool stop=job && job->result.accepted && (job->command.action==SERVICE_STOP || job->command.action==SERVICE_CANCEL_WORKOUT);
        relay_control_update(service.session.control.mode==CONTROL_RUNNING,stop,
            service.session.control.requested_speed_mps,service.session.control.requested_grade_percent,
            service.button,service.button_event,now);
        publish();
        if(job)xSemaphoreGive(job->done);
    }
}
esp_err_t controller_runtime_start(bool software_test)
{
    if(started)return ESP_ERR_INVALID_STATE;
    commands=xQueueCreate(8,sizeof(job_t *));stops=xQueueCreate(4,sizeof(job_t *));
    snapshot_lock=xSemaphoreCreateMutex();
    if(!commands||!stops||!snapshot_lock)goto failed;
    relay_control_start();
    control_service_memory_init(&service,&memory,software_test);
    relay_control_status_t relays=relay_control_status();
    service.relay_outputs=relays.enabled;service.button_mode=relays.enabled;
    service.pulse_ms=relays.enabled?relays.mapping.pulse_ms:500;
    service.boot_id=esp_random();
    control_service_memory_sample(&memory,esp_timer_get_time());
    control_service_tick(&service,esp_timer_get_time());publish();
    if(xTaskCreate(run,"controller",6144,NULL,5,NULL)!=pdPASS)goto failed;
    started=true;return ESP_OK;
failed:
    if(commands)vQueueDelete(commands);
    if(stops)vQueueDelete(stops);
    if(snapshot_lock)vSemaphoreDelete(snapshot_lock);
    commands=NULL;stops=NULL;snapshot_lock=NULL;return ESP_ERR_NO_MEM;
}
service_result_t controller_runtime_submit_result(const service_command_t *command)
{
    service_result_t rejected={.code=SERVICE_BUSY};
    if(!started||!command)return rejected;
    job_t job={.command=*command,.done=xSemaphoreCreateBinary()};
    if(!job.done)return rejected;
    job_t *pointer=&job;
    QueueHandle_t queue=command->action==SERVICE_STOP?stops:commands;
    if(xQueueSend(queue,&pointer,0)!=pdTRUE){vSemaphoreDelete(job.done);return rejected;}
    // No timeout: a job's stack and reply semaphore remain alive until execution finishes.
    xSemaphoreTake(job.done,portMAX_DELAY);vSemaphoreDelete(job.done);return job.result;
}
bool controller_runtime_submit(const service_command_t *command)
{ return controller_runtime_submit_result(command).accepted; }
void controller_runtime_snapshot(control_service_t *snapshot)
{
    xSemaphoreTake(snapshot_lock,portMAX_DELAY);*snapshot=published;xSemaphoreGive(snapshot_lock);
}
