#include "control_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static control_service_t s;
static motion_memory_t m;
static int64_t clock_us;
static service_result_t send(service_command_t *c,service_action_t action)
{
    c->protocol_version=1;c->expected_epoch=s.epoch;c->action=action;
    if(action!=SERVICE_REGISTER)c->sequence++;
    control_service_memory_sample(&m,clock_us);
    return control_service_execute(&s,clock_us,c);
}
static void advance(int64_t amount)
{while(amount>0){int64_t step=amount>100000?100000:amount;clock_us+=step;amount-=step;control_service_memory_sample(&m,clock_us);control_service_tick(&s,clock_us);}}
static void setup(void)
{clock_us=0;control_service_memory_init(&s,&m,true);s.button_mode=true;}
int main(void)
{
    setup();assert(control_service_set_policy(&s,SERVICE_SINGLE_OWNER));
    assert(!control_service_set_policy(&s,SERVICE_SHARED_RESERVED));
    service_command_t web={.client={SERVICE_HTTP,1},.token=101};
    service_command_t ble={.client={SERVICE_BLE,1},.token=102};
    service_command_t wired={.client={SERVICE_WIRED,1},.token=103};
    assert(send(&web,SERVICE_REGISTER).accepted);
    assert(send(&ble,SERVICE_REGISTER).accepted);
    assert(send(&wired,SERVICE_REGISTER).accepted);
    assert(send(&web,SERVICE_CLAIM).accepted);
    assert(send(&ble,SERVICE_CLAIM).code==SERVICE_IN_USE);
    assert(send(&wired,SERVICE_START).code==SERVICE_NOT_OWNER);
    assert(send(&web,SERVICE_START).accepted);advance(600000);
    assert(send(&web,SERVICE_SPEED_UP).accepted);assert(s.button_event==1 && s.button==1);
    assert(send(&web,SERVICE_SPEED_UP).code==SERVICE_BUSY);
    uint64_t revision=s.revision;
    service_result_t replay=control_service_execute(&s,clock_us,&web);
    assert(replay.code==SERVICE_REPLAY && s.button_event==1 && s.revision>revision);
    advance(600000);
    assert(send(&web,SERVICE_SPEED_UP).accepted);assert(s.button_event==2);
    assert(s.session.control.requested_speed_mps==0); // Presses do not fabricate speed.
    assert(send(&web,SERVICE_TARGETS).accepted);
    assert(send(&web,SERVICE_WORKOUT).code==SERVICE_BUSY); // A pulse is still active.
    service_command_t queued=web;queued.sequence+=2;queued.action=SERVICE_START;queued.expected_epoch=s.epoch;
    assert(send(&wired,SERVICE_STOP).accepted);assert(s.session.control.mode==CONTROL_IDLE && s.button==0);
    advance(600000);
    assert(control_service_execute(&s,clock_us,&queued).code==SERVICE_STALE_EPOCH);
    web.sequence=queued.sequence;
    assert(send(&web,SERVICE_START).accepted);advance(600000);
    assert(send(&ble,SERVICE_STOP).accepted); // Every authorized transport can Stop.
    service_command_t unknown={.protocol_version=1,.client={SERVICE_HTTP,1},.token=999,.sequence=1,.action=SERVICE_STOP};
    assert(control_service_execute(&s,clock_us,&unknown).code==SERVICE_UNAUTHORIZED);
    service_command_t spoof=web;spoof.client.transport=SERVICE_WIRED;spoof.sequence++;
    assert(control_service_execute(&s,clock_us,&spoof).code==SERVICE_UNAUTHORIZED);
    assert(send(&web,SERVICE_RELEASE).accepted);assert(!s.session.owner);
    assert(send(&ble,SERVICE_CLAIM).accepted);advance(600000);
    assert(send(&ble,SERVICE_START).accepted);
    assert(send(&web,SERVICE_DISCONNECT).accepted);assert(s.session.control.mode==CONTROL_RUNNING);
    // Viewer heartbeats do not renew the owner's lease.
    for(int i=0;i<6;i++){advance(500000);assert(send(&wired,SERVICE_HEARTBEAT).accepted);}
    assert(s.session.control.mode==CONTROL_FAULT && !s.session.owner);
    assert(send(&wired,SERVICE_CLAIM).accepted);
    assert(send(&wired,SERVICE_RESET).accepted);assert(s.session.control.mode==CONTROL_IDLE);
    char json[3200];assert(control_service_json(&s,clock_us,true,true,json,sizeof(json))>0);
    assert(strstr(json,"\"measured_speed_mps\":null"));assert(strstr(json,"\"protocol_version\":1"));
    assert(strstr(json,"\"control_policy\":\"single_owner\""));
    assert(control_service_json(&s,clock_us,true,true,json,8)==-1);
    service_command_t parsed={.client={SERVICE_HTTP,1}};
    const char *good="v1 0000000000000011 5 2 speed_up 0 0";
    assert(control_service_parse(good,strlen(good),&parsed));
    assert(parsed.action==SERVICE_SPEED_UP && parsed.sequence==5 && parsed.expected_epoch==2);
    const char *bad[]={"v2 0000000000000011 5 2 start 0 0","v1 0000000000000011 0 2 start 0 0",
        "v1 0000000000000011 4294967296 2 start 0 0","v1 0000000000000011 5 2 stop 1 0",
        "v1 0000000000000011 5 2 targets nan 0","v1 0000000000000011 5 2 start 0 0 extra"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(!control_service_parse(bad[i],strlen(bad[i]),&parsed));
    setup();
    service_command_t usb={.client={SERVICE_USB,1},.token=104};
    web=(service_command_t){.client={SERVICE_HTTP,1},.token=105};
    assert(send(&usb,SERVICE_REGISTER).accepted);
    assert(send(&web,SERVICE_REGISTER).accepted);
    assert(send(&usb,SERVICE_CLAIM).accepted);
    assert(send(&web,SERVICE_CLAIM).code==SERVICE_IN_USE);
    spoof=usb;spoof.client.transport=SERVICE_HTTP;spoof.action=SERVICE_HEARTBEAT;spoof.sequence++;
    assert(control_service_execute(&s,clock_us,&spoof).code==SERVICE_UNAUTHORIZED);
    assert(send(&usb,SERVICE_START).accepted);advance(600000);
    assert(send(&web,SERVICE_STOP).accepted); // Web retains Stop while USB owns.
    assert(send(&usb,SERVICE_RELEASE).accepted);
    assert(send(&web,SERVICE_CLAIM).accepted);advance(600000);
    assert(send(&web,SERVICE_START).accepted);
    assert(send(&usb,SERVICE_STOP).accepted); // USB observer can Stop the web owner.
    puts("Contract checks passed: 4 transports, observer Stop, ownership, replay, stop epoch, buttons, expiry, release, codec.");
}
