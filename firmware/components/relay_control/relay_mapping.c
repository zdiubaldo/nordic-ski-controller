#include "relay_mapping.h"
#include <math.h>
static uint8_t bit(unsigned channel) { return channel ? (uint8_t)(1u << (channel-1)) : 0; }
bool relay_mapping_valid(relay_mapping_t m)
{
    unsigned channels[]={m.start,m.stop,m.speed_up,m.speed_down,m.incline_up,m.incline_down};
    unsigned used=0;
    if(m.pulse_ms<100 || m.pulse_ms>2000)return false;
    for(unsigned i=0;i<6;i++){
        if(channels[i]>8)return false;
        unsigned b=bit(channels[i]);
        if(used&b)return false;
        used|=b;
    }
    return true;
}
static void change(float value,float previous,int *direction,int64_t *start,int64_t *end,int64_t now,unsigned duration)
{
    if(value==previous)return;
    *direction=value>previous?1:-1;
    // Each new direction starts with both contacts off for 50 ms.
    *start=now+50000;
    *end=*start+(int64_t)duration*1000;
}
void relay_mapping_update(relay_mapping_state_t *s,bool running,bool stop_requested,float speed,float grade,int64_t now)
{
    if(!running || stop_requested || !isfinite(speed) || !isfinite(grade)){
        // A stop cancels all other presses. Repeated idle updates never extend it.
        if((s->running || stop_requested) && now>=s->stop_end)
            s->stop_end=now+(int64_t)s->map.pulse_ms*1000;
        s->running=false;s->start_end=0;s->speed=0;s->grade=0;
        s->speed_direction=0;s->grade_direction=0;
        s->speed_end=0;s->grade_end=0;return;
    }
    // Finish releasing Stop before accepting a new Start.
    if(now<s->stop_end)return;
    if(!s->running)s->start_end=now+(int64_t)s->map.pulse_ms*1000;
    change(speed,s->speed,&s->speed_direction,&s->speed_start,&s->speed_end,now,s->map.pulse_ms);
    change(grade,s->grade,&s->grade_direction,&s->grade_start,&s->grade_end,now,s->map.pulse_ms);
    s->running=true;s->speed=speed;s->grade=grade;
}
uint8_t relay_mapping_mask(const relay_mapping_state_t *s,int64_t now)
{
    if(now<s->stop_end)return bit(s->map.stop);
    if(!s->running)return 0;
    uint8_t mask=now<s->start_end?bit(s->map.start):0;
    if(now>=s->speed_start && now<s->speed_end)
        mask|=bit(s->speed_direction>0?s->map.speed_up:s->map.speed_down);
    if(now>=s->grade_start && now<s->grade_end)
        mask|=bit(s->grade_direction>0?s->map.incline_up:s->map.incline_down);
    return mask;
}

bool relay_mapping_press(relay_mapping_state_t *s,unsigned button,int64_t now)
{
    if(!s->running || now<s->stop_end || now<s->start_end || now<s->speed_end || now<s->grade_end || button<1 || button>4)return false;
    if(button<=2){s->speed_direction=button==1?1:-1;s->speed_start=now+50000;s->speed_end=s->speed_start+(int64_t)s->map.pulse_ms*1000;}
    else{s->grade_direction=button==3?1:-1;s->grade_start=now+50000;s->grade_end=s->grade_start+(int64_t)s->map.pulse_ms*1000;}
    return true;
}
