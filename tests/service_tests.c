#include "control_service.h"
#include <math.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "unity.h"
#define CHECK(x) TEST_ASSERT_TRUE(x)
#else
#include <assert.h>
#include <stdio.h>
#define CHECK(x) assert(x)
#endif
static void tick(control_service_t *s,motion_memory_t *m,int64_t now)
{control_service_memory_sample(m,now);control_service_tick(s,now);}
static bool send(control_service_t *s,motion_memory_t *m,int64_t now,service_command_t *c,service_action_t action)
{control_service_memory_sample(m,now);c->protocol_version=1;c->expected_epoch=s->epoch;c->action=action;c->sequence++;if(action==SERVICE_CLAIM)c->sequence=0;return control_service_command(s,now,c);}
static void transport_ownership(void)
{
    control_service_t s;motion_memory_t m;control_service_memory_init(&s,&m,true);
    service_command_t http={.client={SERVICE_HTTP,1},.token=123};
    service_command_t ble={.client={SERVICE_BLE,7},.token=123};
    CHECK(send(&s,&m,0,&http,SERVICE_CLAIM));
    CHECK(!send(&s,&m,0,&ble,SERVICE_CLAIM));
    CHECK(!send(&s,&m,0,&ble,SERVICE_START));
    CHECK(send(&s,&m,0,&http,SERVICE_START));CHECK(m.enables==1);
    http.speed_mps=2;http.grade_percent=3;
    CHECK(send(&s,&m,0,&http,SERVICE_TARGETS));tick(&s,&m,100000);
    CHECK(m.writes>0 && m.last_speed_mps>0 && m.last_speed_mps<2);
    CHECK(send(&s,&m,100000,&http,SERVICE_DISCONNECT));
    CHECK(!s.session.owner && !s.motion.running && m.stops>0);
    CHECK(send(&s,&m,100000,&ble,SERVICE_CLAIM));
    CHECK(!send(&s,&m,100000,&http,SERVICE_STOP));
    CHECK(send(&s,&m,100000,&ble,SERVICE_RESET));
    CHECK(send(&s,&m,100000,&ble,SERVICE_START));
    CHECK(send(&s,&m,100000,&ble,SERVICE_STOP));CHECK(!s.motion.running);
}
static void faults_and_deadline(void)
{
    control_service_t s;motion_memory_t m;control_service_memory_init(&s,&m,true);
    service_command_t c={.client={SERVICE_HTTP,1},.token=1};
    CHECK(send(&s,&m,0,&c,SERVICE_CLAIM));CHECK(send(&s,&m,0,&c,SERVICE_START));
    for(int i=1;i<=30;i++)tick(&s,&m,i*100000);
    CHECK(s.session.control.mode==CONTROL_FAULT && !s.motion.running && !s.session.owner);
    CHECK(m.stops>0 && s.motion.reason==MOTION_STOP_TIMEOUT);
    CHECK(send(&s,&m,3000000,&c,SERVICE_CLAIM));CHECK(send(&s,&m,3000000,&c,SERVICE_RESET));
    CHECK(send(&s,&m,3000000,&c,SERVICE_START));
    m.feedback.interlock_ok=MOTION_NO;tick(&s,&m,3100000);
    CHECK(!s.motion.running && s.session.control.mode==CONTROL_FAULT);
    CHECK(!send(&s,&m,3100000,&c,SERVICE_RESET));
}
static void workout_reaches_driver(void)
{
    control_service_t s;motion_memory_t m;control_service_memory_init(&s,&m,true);
    service_command_t c={.client={SERVICE_BLE,42},.token=2,.duration_minutes=1,.intensity_percent=100,.workout_id="hills"};
    CHECK(send(&s,&m,0,&c,SERVICE_CLAIM));CHECK(send(&s,&m,0,&c,SERVICE_WORKOUT));
    for(int i=1;i<=600;i++){
        int64_t now=(int64_t)i*100000;tick(&s,&m,now);
        if(i%10==0)CHECK(send(&s,&m,now,&c,SERVICE_HEARTBEAT));
        if(i==61){CHECK(s.session.segment==1);CHECK(s.motion.target_grade_percent==1);}
    }
    CHECK(s.session.workout_complete && s.session.control.mode==CONTROL_IDLE);
    CHECK(!s.motion.running && m.stops==1);
    CHECK(m.feedback.speed_mps.value==0 && m.feedback.grade_percent.value==0);
}
static void limits_and_unknown_feedback(void)
{
    control_service_t s;motion_memory_t m;control_service_memory_init(&s,&m,true);
    service_command_t c={.client={SERVICE_HTTP,1},.token=3,.speed_mps=1,.grade_percent=2};
    CHECK(send(&s,&m,0,&c,SERVICE_CLAIM));m.feedback.speed_mps.known=false;
    CHECK(!send(&s,&m,0,&c,SERVICE_START));CHECK(!s.motion.running && m.enables==0);
    // Fresh fixture; active upper limit must reject a climb from either transport.
    control_service_memory_init(&s,&m,true);c.sequence=0;
    CHECK(send(&s,&m,0,&c,SERVICE_CLAIM));CHECK(send(&s,&m,0,&c,SERVICE_START));
    m.feedback.upper_limit=MOTION_YES;CHECK(!send(&s,&m,0,&c,SERVICE_TARGETS));
    CHECK(s.session.control.mode==CONTROL_FAULT && !s.motion.running);
}
static void full_controls_on_every_transport(void)
{
    const service_transport_t transports[]={SERVICE_HTTP,SERVICE_BLE,SERVICE_WIRED,SERVICE_USB};
    for(unsigned t=0;t<4;t++){
        control_service_t s;motion_memory_t m;control_service_memory_init(&s,&m,true);
        s.button_mode=true;s.relay_outputs=true; // The physical capability must not disable targets/workouts.
        service_command_t c={.client={transports[t],1},.token=200+t,
            .duration_minutes=1,.intensity_percent=100,.workout_id="hills"};
        CHECK(send(&s,&m,0,&c,SERVICE_CLAIM));
        CHECK(send(&s,&m,0,&c,SERVICE_START));
        c.speed_mps=2;c.grade_percent=3;
        CHECK(send(&s,&m,0,&c,SERVICE_TARGETS));
        CHECK(s.session.control.requested_speed_mps==2 && s.session.control.requested_grade_percent==3);
        service_command_t duplicate=c;
        CHECK(control_service_execute(&s,0,&duplicate).code==SERVICE_REPLAY);
        c.speed_mps=NAN;CHECK(!send(&s,&m,0,&c,SERVICE_TARGETS));
        CHECK(s.session.control.requested_speed_mps==2);
        CHECK(send(&s,&m,0,&c,SERVICE_STOP));
        CHECK(send(&s,&m,0,&c,SERVICE_WORKOUT)==false); // Stop pulse must finish first.
        for(int i=1;i<=6;i++)tick(&s,&m,i*100000);
        CHECK(send(&s,&m,600000,&c,SERVICE_WORKOUT));
        for(int i=1;i<=600;i++){
            int64_t now=600000+(int64_t)i*100000;tick(&s,&m,now);
            if(i%5==0)CHECK(send(&s,&m,now,&c,SERVICE_HEARTBEAT));
            if(i==20){
                c.speed_mps=2;c.grade_percent=4;
                CHECK(send(&s,&m,now,&c,SERVICE_TARGETS));CHECK(s.session.overridden);
            }
            if(i==61){CHECK(s.session.segment==1);CHECK(!s.session.overridden);CHECK(s.session.control.requested_grade_percent==1);}
        }
        CHECK(s.session.workout_complete && s.session.control.mode==CONTROL_IDLE);
        CHECK(!s.motion.running && s.session.control.requested_speed_mps==0);
        char json[3200];CHECK(control_service_json(&s,60600000,false,true,json,sizeof(json))>0);
        CHECK(strstr(json,"\"numeric_targets\":true"));CHECK(strstr(json,"\"workouts\":true"));
        CHECK(strstr(json,"\"measured_speed_mps\":null"));
        // Cancellation uses the same owner policy and invalidates older target commands.
        for(int i=1;i<=6;i++)tick(&s,&m,60600000+i*100000);
        CHECK(send(&s,&m,61200000,&c,SERVICE_WORKOUT));
        duplicate=c;duplicate.sequence+=2;duplicate.action=SERVICE_TARGETS;
        CHECK(send(&s,&m,61200000,&c,SERVICE_CANCEL_WORKOUT));
        CHECK(control_service_execute(&s,61200000,&duplicate).code==SERVICE_STALE_EPOCH);
        CHECK(s.session.control.mode==CONTROL_IDLE && !s.session.workout_active);
    }
}
void run_service_tests(void)
{
#ifdef ESP_PLATFORM
    RUN_TEST(transport_ownership);RUN_TEST(faults_and_deadline);RUN_TEST(workout_reaches_driver);RUN_TEST(limits_and_unknown_feedback);RUN_TEST(full_controls_on_every_transport);
#else
    transport_ownership();faults_and_deadline();workout_reaches_driver();limits_and_unknown_feedback();full_controls_on_every_transport();
    puts("All 5 shared service integration groups passed.");
#endif
}
#ifndef ESP_PLATFORM
int main(void){run_service_tests();return 0;}
#endif
