#pragma once
#include "test_session.h"
#include "motion_memory.h"
#define SERVICE_PROTOCOL_VERSION 1
#define SERVICE_MAX_CLIENTS 8
// Transport adapters authenticate clients and supply identities; never trust payload identity.
typedef enum { SERVICE_HTTP=1, SERVICE_BLE=2, SERVICE_WIRED=3, SERVICE_USB=4 } service_transport_t;
typedef struct { service_transport_t transport; uint64_t peer; } service_client_t;
typedef enum { SERVICE_SINGLE_OWNER=0, SERVICE_SHARED_RESERVED=1 } service_policy_t;
typedef enum { SERVICE_CLAIM, SERVICE_START, SERVICE_STOP, SERVICE_TARGETS,
    SERVICE_HEARTBEAT, SERVICE_RESET, SERVICE_WORKOUT, SERVICE_DISCONNECT,
    SERVICE_REGISTER, SERVICE_RELEASE, SERVICE_SPEED_UP, SERVICE_SPEED_DOWN,
    SERVICE_INCLINE_UP, SERVICE_INCLINE_DOWN, SERVICE_CANCEL_WORKOUT } service_action_t;
typedef enum { SERVICE_OK, SERVICE_BAD_VERSION, SERVICE_DISABLED, SERVICE_UNAUTHORIZED,
    SERVICE_NOT_OWNER, SERVICE_IN_USE, SERVICE_REPLAY, SERVICE_STALE_EPOCH,
    SERVICE_INVALID, SERVICE_WRONG_STATE, SERVICE_UNSUPPORTED, SERVICE_BUSY,
    SERVICE_DRIVER_FAULT, SERVICE_CAPACITY } service_code_t;
typedef struct {
    unsigned protocol_version;
    service_client_t client;
    uint64_t token;
    uint32_t sequence, expected_epoch;
    service_action_t action;
    float speed_mps, grade_percent, duration_minutes, intensity_percent;
    char workout_id[25];
} service_command_t;
typedef struct {
    bool accepted;
    service_code_t code;
    uint32_t client_id, sequence, epoch;
    uint64_t revision;
} service_result_t;
typedef struct {
    service_client_t client;
    uint64_t token;
    uint32_t id, sequence;
    int64_t last_seen_us;
} service_connection_t;
typedef struct {
    test_session_t session;
    motion_t motion;
    service_client_t owner;
    service_policy_t policy;
    service_connection_t clients[SERVICE_MAX_CLIENTS];
    uint32_t next_client_id, epoch, boot_id;
    uint64_t revision;
    service_result_t last_result;
    bool memory_fixture, relay_outputs, button_mode;
    // Button event is a command, never a fabricated speed/angle measurement.
    unsigned button; // 1 speed up, 2 speed down, 3 incline up, 4 incline down
    uint32_t button_event;
    int64_t button_busy_until;
    unsigned pulse_ms;
} control_service_t;
void control_service_init(control_service_t *s, bool enabled, motion_driver_t driver, motion_config_t config);
// Only SINGLE_OWNER is implemented. Shared mode requires an approved conflict policy.
bool control_service_set_policy(control_service_t *s, service_policy_t policy);
void control_service_tick(control_service_t *s, int64_t now);
service_result_t control_service_execute(control_service_t *s, int64_t now, const service_command_t *command);
bool control_service_command(control_service_t *s, int64_t now, const service_command_t *command);
const char *control_service_reason(service_code_t code);
void control_service_memory_init(control_service_t *s, motion_memory_t *memory, bool enabled);
void control_service_memory_sample(motion_memory_t *memory, int64_t now);
int control_service_json(const control_service_t *s, int64_t now, bool local, bool ok, char *buffer, size_t size);
// Shared HTTP text codec: v1 TOKEN SEQUENCE EPOCH ACTION VALUE VALUE.
bool control_service_parse(const char *body, size_t length, service_command_t *command);
