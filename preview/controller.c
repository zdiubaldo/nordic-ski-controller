#define _POSIX_C_SOURCE 200809L
#include "test_session.h"
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
int main(void)
{
    test_session_t session;
    test_session_init(&session, true);
    const int64_t boot = now_us();
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (;;) {
        test_session_tick(&session, now_us());
        fd_set ready;
        FD_ZERO(&ready); FD_SET(STDIN_FILENO, &ready);
        struct timeval timeout = {.tv_usec = 20000};
        int result = select(STDIN_FILENO + 1, &ready, NULL, NULL, &timeout);
        if (result < 0) return 1;
        if (!result) continue;
        char line[256];
        if (!fgets(line, sizeof(line), stdin)) return 0;
        int64_t now = now_us();
        bool accepted = false;
        uint64_t token;
        uint32_t sequence;
        float speed, grade;
        char action[16];
        if (!strcmp(line, "status\n")) accepted = true;
        else if (sscanf(line, "claim %16" SCNx64, &token) == 1)
            accepted = test_session_claim(&session, now, token);
        else if (sscanf(line, "command %16" SCNx64 " %" SCNu32 " %15s %f %f",
                        &token, &sequence, action, &speed, &grade) == 5) {
            const char *names[] = {"start", "stop", "targets", "heartbeat", "reset"};
            for (int i = 0; i < 5; ++i)
                if (!strcmp(action, names[i]))
                    accepted = test_session_command(&session, now, token, sequence,
                                                     (test_action_t)i, speed, grade);
        }
        test_session_tick(&session, now_us());
        const char *mode = session.control.mode == CONTROL_IDLE ? "idle" :
                           session.control.mode == CONTROL_RUNNING ? "running" : "fault";
        printf("{\"ok\":%s,\"local_preview\":true,\"software_test\":true,"
               "\"mode\":\"%s\",\"fault\":%d,\"session_active\":%s,"
               "\"requested_speed_mps\":%.3f,\"requested_grade_percent\":%.3f,"
               "\"physical_outputs\":false,\"uptime_ms\":%" PRId64 ",\"sample_age_ms\":0}\n",
               accepted ? "true" : "false", mode, (int)session.control.fault,
               session.owner ? "true" : "false", (double)session.control.requested_speed_mps,
               (double)session.control.requested_grade_percent, (now - boot) / 1000);
    }
}
