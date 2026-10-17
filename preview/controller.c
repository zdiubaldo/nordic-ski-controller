#define _POSIX_C_SOURCE 200809L
#include "control_service.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

static int64_t now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
int main(int argc,char **argv)
{
    control_service_t service;
    motion_memory_t memory;
    control_service_memory_init(&service, &memory, true);
    service.button_mode=argc>1 && !strcmp(argv[1],"--buttons");
    service.boot_id=(uint32_t)now_us();
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (;;) {
        control_service_memory_sample(&memory,now_us());
        control_service_tick(&service,now_us());
        fd_set ready;
        FD_ZERO(&ready); FD_SET(STDIN_FILENO, &ready);
        struct timeval timeout = {.tv_usec = 20000};
        int result = select(STDIN_FILENO + 1, &ready, NULL, NULL, &timeout);
        if (result < 0) return 1;
        if (!result) continue;
        char line[256];
        if (!fgets(line, sizeof(line), stdin)) return 0;
        int64_t now = now_us();
        if (!strcmp(line, "workouts\n")) { puts(test_session_workout_catalog()); continue; }
        control_service_memory_sample(&memory,now);
        bool accepted = false;
        service_command_t command={.protocol_version=1,.client={
            argc>1 && !strcmp(argv[1],"--ble") ? SERVICE_BLE : SERVICE_HTTP,1}};
        uint64_t token;
        if (!strcmp(line, "status\n")) accepted = true;
        else if (sscanf(line, "register %16" SCNx64, &token) == 1)
        { command.action=SERVICE_REGISTER;command.token=token;accepted=control_service_command(&service,now,&command); }
        else if (sscanf(line, "claim %16" SCNx64, &token) == 1)
        { command.action=SERVICE_CLAIM;command.token=token;accepted=control_service_command(&service,now,&command); }
        else if (!strncmp(line,"command ",8)) {
            line[strcspn(line,"\n")]=0;
            if(control_service_parse(line+8,strlen(line+8),&command))
                accepted=control_service_command(&service,now,&command);
        }
        control_service_memory_sample(&memory,now_us());
        control_service_tick(&service,now_us());
        char body[3200];
        int length=control_service_json(&service,now_us(),true,accepted,body,sizeof(body));
        if(length<0 || (size_t)length>=sizeof(body))return 1;
        puts(body);
    }
}
