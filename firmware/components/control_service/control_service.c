#include "control_service.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
static bool client_valid(service_client_t c)
{
    return (c.transport==SERVICE_HTTP || c.transport==SERVICE_BLE || c.transport==SERVICE_WIRED || c.transport==SERVICE_USB) && c.peer;
}
static bool same(service_client_t a, service_client_t b)
{
    return a.transport==b.transport && a.peer==b.peer;
}
static void fault_session(control_service_t *s)
{
    if(s->motion.running)motion_stop(&s->motion);
    control_tick(&s->session.control, s->session.control.now_us, false);
    // A failed enable can happen before CONTROL_RUNNING.
    s->session.control.mode=CONTROL_FAULT;
    if(s->session.control.fault==FAULT_NONE)s->session.control.fault=FAULT_INTERLOCK;
    s->session.control.requested_speed_mps=0;
    s->session.control.requested_grade_percent=0;
    s->session.workout_active=false;
    s->session.workout_complete=false;
}
void control_service_init(control_service_t *s, bool enabled, motion_driver_t driver, motion_config_t config)
{
    *s=(control_service_t){.epoch=1,.revision=1,.pulse_ms=500};
    test_session_init(&s->session,enabled);
    motion_init(&s->motion,driver);
    bool configured=motion_configure(&s->motion,config);
    s->session.control.config=(control_config_t){enabled && configured,config.max_speed_mps,
        config.max_grade_percent,config.command_timeout_us};
}
static void sync_motion(control_service_t *s, int64_t now)
{
    if(s->session.control.mode!=CONTROL_RUNNING){
        if(s->motion.running)motion_stop(&s->motion);
    }else if(s->motion.running){
        // Workout ticks may update demand, but may not extend the lease watchdog.
        int64_t last=s->session.control.last_command_us;
        if(!motion_targets(&s->motion,now,s->session.control.requested_speed_mps,
                           s->session.control.requested_grade_percent))fault_session(s);
        s->motion.last_command_us=last;
    }
    if(s->motion.faulted)fault_session(s);
}
static bool owner_is(const control_service_t *s, const service_connection_t *c)
{ return c && c->token && c->token==s->session.owner && same(c->client,s->owner); }
static void invalidate_pending(control_service_t *s)
{ s->epoch++;s->button=0;s->button_busy_until=0; }
bool control_service_set_policy(control_service_t *s,service_policy_t policy)
{
    if(policy!=SERVICE_SINGLE_OWNER || s->session.owner)return false;
    s->policy=policy;return true;
}
void control_service_tick(control_service_t *s, int64_t now)
{
    bool running=s->session.control.mode==CONTROL_RUNNING;
    uint64_t owner=s->session.owner;
    test_session_tick(&s->session,now);
    motion_tick(&s->motion,now);
    sync_motion(s,now);
    if(!s->session.owner)s->owner=(service_client_t){0};
    if((running && s->session.control.mode!=CONTROL_RUNNING) || (owner && !s->session.owner))
        invalidate_pending(s);
    for(unsigned i=0;i<SERVICE_MAX_CLIENTS;i++){
        service_connection_t *c=&s->clients[i];
        if(c->token && (now<c->last_seen_us || now-c->last_seen_us>=3000000))
            *c=(service_connection_t){0};
    }
    s->revision++; // Monotonic snapshot revision within boot_id, including timed transitions.
}
const char *control_service_reason(service_code_t code)
{
    static const char *names[]={"accepted","unsupported_version","disabled","unauthorized",
        "not_owner","control_in_use","replayed_sequence","stale_epoch","invalid_command",
        "wrong_state","unsupported_command","output_busy","driver_fault","capacity"};
    return (unsigned)code<sizeof(names)/sizeof(names[0])?names[code]:"invalid_command";
}
static service_result_t result(control_service_t *s,const service_command_t *cmd,
                               const service_connection_t *client,service_code_t code)
{
    s->revision++;
    s->last_result=(service_result_t){code==SERVICE_OK,code,client?client->id:0,
        cmd?cmd->sequence:0,s->epoch,s->revision};
    return s->last_result;
}
service_result_t control_service_execute(control_service_t *s,int64_t now,const service_command_t *c)
{
    control_service_tick(s,now);
    if(!c)return result(s,c,NULL,SERVICE_INVALID);
    if(c->protocol_version!=SERVICE_PROTOCOL_VERSION)return result(s,c,NULL,SERVICE_BAD_VERSION);
    if(!s->session.enabled)return result(s,c,NULL,SERVICE_DISABLED);
    if(!client_valid(c->client)||!c->token)return result(s,c,NULL,SERVICE_UNAUTHORIZED);
    service_connection_t *client=NULL,*free_slot=NULL;
    for(unsigned i=0;i<SERVICE_MAX_CLIENTS;i++){
        service_connection_t *candidate=&s->clients[i];
        if(candidate->token==c->token && same(candidate->client,c->client))client=candidate;
        if(!candidate->token && !free_slot)free_slot=candidate;
    }
    if(!client && (c->action==SERVICE_REGISTER || c->action==SERVICE_CLAIM)){
        if(c->sequence)return result(s,c,NULL,SERVICE_INVALID);
        if(c->action==SERVICE_CLAIM && s->session.owner)return result(s,c,NULL,SERVICE_IN_USE);
        if(!free_slot)return result(s,c,NULL,SERVICE_CAPACITY);
        client=free_slot;*client=(service_connection_t){c->client,c->token,++s->next_client_id,0,now};
    }else{
        if(!client)return result(s,c,NULL,SERVICE_UNAUTHORIZED);
        if(!c->sequence || c->sequence<=client->sequence)return result(s,c,client,SERVICE_REPLAY);
        // Consume even rejected requests. They must never become executable later.
        client->sequence=c->sequence;
    }
    if(c->action==SERVICE_REGISTER)return result(s,c,client,SERVICE_OK);
    bool owns=owner_is(s,client);
    if(c->action==SERVICE_STOP || c->action==SERVICE_CANCEL_WORKOUT){
        if(c->action==SERVICE_CANCEL_WORKOUT && !owns)return result(s,c,client,SERVICE_NOT_OWNER);
        control_stop(&s->session.control);s->session.workout_active=false;s->session.workout_complete=false;
        invalidate_pending(s);s->button_busy_until=now+(int64_t)s->pulse_ms*1000+50000;
        bool stopped=motion_stop(&s->motion);
        client->last_seen_us=now;
        if(owns)s->session.last_seen_us=now;
        return result(s,c,client,stopped?SERVICE_OK:SERVICE_DRIVER_FAULT);
    }
    if(c->action==SERVICE_HEARTBEAT && !owns){client->last_seen_us=now;return result(s,c,client,SERVICE_OK);}
    if(c->action==SERVICE_DISCONNECT || c->action==SERVICE_RELEASE){
        if(owns){
            motion_stop(&s->motion);control_stop(&s->session.control);
            s->session.workout_active=false;s->session.owner=0;s->owner=(service_client_t){0};
            invalidate_pending(s);
            if(c->action==SERVICE_DISCONNECT)fault_session(s);
        }
        service_result_t r=result(s,c,client,SERVICE_OK);
        if(c->action==SERVICE_DISCONNECT)*client=(service_connection_t){0};
        return r;
    }
    if(c->action==SERVICE_CLAIM){
        if(s->session.owner)return result(s,c,client,SERVICE_IN_USE);
        if(!test_session_claim(&s->session,now,c->token))return result(s,c,client,SERVICE_WRONG_STATE);
        s->owner=c->client;client->last_seen_us=now;
        return result(s,c,client,SERVICE_OK);
    }
    if(!owns)return result(s,c,client,SERVICE_NOT_OWNER);
    if(c->action!=SERVICE_HEARTBEAT && c->expected_epoch!=s->epoch)
        return result(s,c,client,SERVICE_STALE_EPOCH);
    if(c->action>=SERVICE_SPEED_UP && c->action<=SERVICE_INCLINE_DOWN){
        if(!s->button_mode)return result(s,c,client,SERVICE_UNSUPPORTED);
        if(s->session.control.mode!=CONTROL_RUNNING)return result(s,c,client,SERVICE_WRONG_STATE);
        if(now<s->button_busy_until)return result(s,c,client,SERVICE_BUSY);
        s->button=(unsigned)(c->action-SERVICE_SPEED_UP)+1;s->button_event++;
        s->button_busy_until=now+(int64_t)s->pulse_ms*1000+100000;
        s->session.control.last_command_us=now;s->motion.last_command_us=now;
        s->session.last_seen_us=now;client->last_seen_us=now;
        return result(s,c,client,SERVICE_OK);
    }
    if((c->action==SERVICE_START || c->action==SERVICE_WORKOUT) && now<s->button_busy_until)return result(s,c,client,SERVICE_BUSY);
    if(c->action==SERVICE_RESET && s->motion.faulted && !motion_reset(&s->motion,now))
        return result(s,c,client,SERVICE_DRIVER_FAULT);
    bool ok=false;
    if(c->action==SERVICE_WORKOUT){
        if(!memchr(c->workout_id,0,sizeof(c->workout_id)))return result(s,c,client,SERVICE_INVALID);
        ok=test_session_start_workout(&s->session,now,c->token,c->sequence,c->workout_id,c->duration_minutes,c->intensity_percent);
    }else{
        test_action_t action;
        switch(c->action){
            case SERVICE_START:action=TEST_START;break;
            case SERVICE_TARGETS:action=TEST_TARGETS;break;
            case SERVICE_HEARTBEAT:action=TEST_HEARTBEAT;break;
            case SERVICE_RESET:action=TEST_RESET;break;
            default:return result(s,c,client,SERVICE_INVALID);
        }
        ok=test_session_command(&s->session,now,c->token,c->sequence,action,c->speed_mps,c->grade_percent);
    }
    if(ok&&(c->action==SERVICE_START||c->action==SERVICE_WORKOUT)){
        if(!motion_enable(&s->motion,now)){motion_stop(&s->motion);fault_session(s);ok=false;}
        if(s->button_mode)s->button_busy_until=now+(int64_t)s->pulse_ms*1000+50000;
    }
    sync_motion(s,now);
    if(ok)client->last_seen_us=now;
    return result(s,c,client,s->motion.faulted?SERVICE_DRIVER_FAULT:ok?SERVICE_OK:
        c->action==SERVICE_TARGETS||c->action==SERVICE_WORKOUT?SERVICE_INVALID:SERVICE_WRONG_STATE);
}
bool control_service_command(control_service_t *s,int64_t now,const service_command_t *c)
{ return control_service_execute(s,now,c).accepted; }
void control_service_memory_init(control_service_t *s, motion_memory_t *m, bool enabled)
{
    motion_memory_init(m);
    // Explicitly injected stationary fixture; never synthesized from requests.
    m->feedback=(motion_feedback_t){.timestamp_known=true,.ready=MOTION_YES,
        .interlock_ok=MOTION_YES,.fault=MOTION_NO,.upper_limit=MOTION_NO,.lower_limit=MOTION_NO,
        .speed_mps={true,0},.grade_percent={true,0}};
    control_service_init(s,enabled,motion_memory_driver(m),
        (motion_config_t){enabled,5,0,10,1,2,1,1000000,3000000,500000});
    s->memory_fixture=true;
}
void control_service_memory_sample(motion_memory_t *m, int64_t now)
{
    m->feedback.sampled_us=now;
}
int control_service_json(const control_service_t *s, int64_t now, bool local, bool ok, char *buffer, size_t size)
{
    if(!s->memory_fixture)return -1; // This wire format currently describes only the software fixture.
    char workout[400];test_session_workout_json(&s->session,workout,sizeof(workout));
    const control_t *c=&s->session.control;
    int length=snprintf(buffer,size,
        "{\"ok\":%s,\"local_preview\":%s,\"software_test\":%s,\"mode\":\"%s\",\"fault\":%d,"
        "\"commissioned\":false,\"motion_available\":false,\"hardware_verified\":false,\"physical_outputs\":%s,"
        "\"session_active\":%s,\"owner_transport\":%d,\"requested_speed_mps\":%.3f,\"requested_grade_percent\":%.3f,"
        "\"uptime_ms\":%" PRId64 ",\"sample_age_ms\":%" PRId64 ",\"workout\":%s,"
        "\"driver\":{\"type\":\"memory\",\"running\":%s,\"fault\":%d,\"stop_reason\":%d,\"stop_acknowledged\":%s,"
        "\"commanded_speed_mps\":%.3f,\"commanded_grade_percent\":%.3f,"
        "\"measured_speed_mps\":null,\"measured_grade_percent\":null,\"feedback_source\":\"injected\"}}",
        ok?"true":"false",local?"true":"false",s->session.enabled?"true":"false",
        c->mode==CONTROL_IDLE?"idle":c->mode==CONTROL_RUNNING?"running":"fault",(int)c->fault,
        s->relay_outputs?"true":"false",s->session.owner?"true":"false",(int)s->owner.transport,
        (double)c->requested_speed_mps,(double)c->requested_grade_percent,now/1000,
        (now-c->now_us)/1000,workout,s->motion.running?"true":"false",s->motion.faulted?(int)s->motion.reason:0,(int)s->motion.reason,
        s->motion.stop_acknowledged?"true":"false",(double)s->motion.sent_speed_mps,(double)s->motion.sent_grade_percent);
    if(length<0 || (size_t)length>=size)return -1;
    uint32_t owner_id=0;unsigned connected=0;
    for(unsigned i=0;i<SERVICE_MAX_CLIENTS;i++){
        if(s->clients[i].token)connected++;
        if(owner_is(s,&s->clients[i]))owner_id=s->clients[i].id;
    }
    int tail=snprintf(buffer+length-1,size-(size_t)length+1,
        ",\"protocol_version\":1,\"boot_id\":%u,\"revision\":\"%" PRIu64 "\",\"control_epoch\":%u,"
        "\"control_policy\":\"single_owner\",\"owner_client_id\":%u,\"connected_clients\":%u,"
        "\"capabilities\":{\"button_controls\":%s,\"numeric_targets\":true,\"calibrated_targets\":false,\"workouts\":true,\"shared_control\":false},"
        "\"button_event\":%u,\"button_busy\":%s,"
        "\"command_result\":{\"accepted\":%s,\"reason\":\"%s\",\"client_id\":%u,\"sequence\":%u,\"revision\":\"%" PRIu64 "\"},"
        "\"measured_speed_mps\":null,\"measured_grade_percent\":null}",
        (unsigned)s->boot_id,s->revision,(unsigned)s->epoch,(unsigned)owner_id,connected,
        s->button_mode?"true":"false",
        (unsigned)s->button_event,now<s->button_busy_until?"true":"false",
        s->last_result.accepted?"true":"false",control_service_reason(s->last_result.code),
        (unsigned)s->last_result.client_id,(unsigned)s->last_result.sequence,s->last_result.revision);
    if(tail<0 || (size_t)tail>=size-(size_t)length+1)return -1;
    return length+tail-1;

}

bool control_service_parse(const char *body,size_t length,service_command_t *c)
{
    if(!body || length>=160 || strlen(body)!=length)return false;
    char version[8],token[17],seq[11],epoch[11],action[32];float a,b;int end=0;
    if(sscanf(body,"%7s %16[0-9a-f] %10[0-9] %10[0-9] %31s %f %f %n",
        version,token,seq,epoch,action,&a,&b,&end)!=7 || (size_t)end!=length ||
        strcmp(version,"v1") || strlen(token)!=16 || !isfinite(a)||!isfinite(b))return false;
    unsigned long long n=strtoull(seq,NULL,10),e=strtoull(epoch,NULL,10);
    if(!n||n>UINT32_MAX||e>UINT32_MAX)return false;
    c->protocol_version=1;c->token=strtoull(token,NULL,16);c->sequence=(uint32_t)n;c->expected_epoch=(uint32_t)e;
    c->speed_mps=a;c->grade_percent=b;c->duration_minutes=a;c->intensity_percent=b;
    const char *names[]={"claim","start","stop","targets","heartbeat","reset","release",
        "speed_up","speed_down","incline_up","incline_down","cancel_workout","disconnect"};
    const service_action_t actions[]={SERVICE_CLAIM,SERVICE_START,SERVICE_STOP,SERVICE_TARGETS,
        SERVICE_HEARTBEAT,SERVICE_RESET,SERVICE_RELEASE,SERVICE_SPEED_UP,SERVICE_SPEED_DOWN,
        SERVICE_INCLINE_UP,SERVICE_INCLINE_DOWN,SERVICE_CANCEL_WORKOUT,SERVICE_DISCONNECT};
    for(unsigned i=0;i<sizeof(actions)/sizeof(actions[0]);i++)if(!strcmp(action,names[i])){
        c->action=actions[i];return c->action==SERVICE_TARGETS || (a==0 && b==0);
    }
    if(!strncmp(action,"w:",2) && action[2] && strlen(action+2)<sizeof(c->workout_id)){
        c->action=SERVICE_WORKOUT;strcpy(c->workout_id,action+2);return true;
    }
    return false;
}
