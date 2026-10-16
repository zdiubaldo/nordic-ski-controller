#include "relay_mapping.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void){
 relay_mapping_t map={1,6,2,3,4,5,500};assert(relay_mapping_valid(map));
 relay_mapping_t invalid=map;invalid.stop=1;assert(!relay_mapping_valid(invalid));
 invalid=map;invalid.start=9;assert(!relay_mapping_valid(invalid));
 invalid=map;invalid.pulse_ms=0;assert(!relay_mapping_valid(invalid));
 relay_mapping_state_t s={.map=map};
 relay_mapping_update(&s,false,false,0,0,0);assert(relay_mapping_mask(&s,0)==0);
 relay_mapping_update(&s,true,false,0,0,100);assert(relay_mapping_mask(&s,100)==1);
 relay_mapping_update(&s,true,false,0,0,400000);
 assert(relay_mapping_mask(&s,500099)==1);assert(relay_mapping_mask(&s,500100)==0);
 relay_mapping_update(&s,true,false,1,1,600000);assert(relay_mapping_mask(&s,600000)==0);
 assert(relay_mapping_mask(&s,650000)==(2|8));
 relay_mapping_update(&s,true,false,1,1,900000);assert(relay_mapping_mask(&s,1150000)==0);
 relay_mapping_update(&s,true,false,.5f,.5f,1200000);assert(relay_mapping_mask(&s,1250000)==(4|16));
 relay_mapping_update(&s,true,false,2,2,1260000);assert(relay_mapping_mask(&s,1260000)==0);
 assert(relay_mapping_mask(&s,1310000)==(2|8));
 // Stop preempts active directions; idle publications do not extend the pulse.
 relay_mapping_update(&s,false,true,0,0,1320000);assert(relay_mapping_mask(&s,1320000)==32);
 relay_mapping_update(&s,false,false,0,0,1400000);assert(relay_mapping_mask(&s,1820000)==0);
 // Explicit Stop also works when already idle, but no startup Stop is generated.
 relay_mapping_update(&s,false,true,0,0,1900000);assert(relay_mapping_mask(&s,1900000)==32);
 // Rapid restart cannot overlap Start and Stop.
 relay_mapping_update(&s,true,false,0,0,2000000);assert(relay_mapping_mask(&s,2000000)==32);
 relay_mapping_update(&s,true,false,0,0,2400000);assert(relay_mapping_mask(&s,2400000)==1);
 // Stopping during Start cancels Start immediately (also covers timeout transition).
 relay_mapping_update(&s,false,false,0,0,2450000);assert(relay_mapping_mask(&s,2450000)==32);
 relay_mapping_update(&s,true,false,0,0,3000000);
 relay_mapping_update(&s,true,false,NAN,0,3100000);assert(relay_mapping_mask(&s,3100000)==32);
 s=(relay_mapping_state_t){.map={8,0,7,6,5,0,100}};assert(relay_mapping_valid(s.map));
 relay_mapping_update(&s,true,false,1,1,4000000);assert(relay_mapping_mask(&s,4050000)==(128|64|16));
 assert(relay_mapping_mask(&s,4100000)==(64|16));
 assert(relay_mapping_mask(&s,4150000)==0);
 relay_mapping_update(&s,false,true,0,0,4200000);assert(relay_mapping_mask(&s,4200000)==0);
 s=(relay_mapping_state_t){.map=map};
 relay_mapping_update(&s,true,false,0,0,0);
 assert(!relay_mapping_press(&s,1,100)); // Start still pressed.
 assert(relay_mapping_press(&s,1,600000));
 assert(relay_mapping_mask(&s,600000)==0);
 assert(relay_mapping_mask(&s,650000)==2);
 assert(!relay_mapping_press(&s,2,700000));
 assert(relay_mapping_mask(&s,1150000)==0);
 assert(relay_mapping_press(&s,1,1200000)); // Same button can be pressed again.
 relay_mapping_update(&s,false,true,0,0,1250000);assert(relay_mapping_mask(&s,1250000)==32);
 assert(!relay_mapping_press(&s,1,1300000));
 puts("Relay mapping: momentary Start/Stop, stop priority, timeout transition, expiry, reversal, disabled/remapped channels passed.");
}
