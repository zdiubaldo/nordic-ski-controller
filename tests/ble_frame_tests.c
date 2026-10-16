#include "ble_frame.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static size_t frame(uint8_t *out,size_t size,uint16_t id,const char *text,size_t offset)
{return ble_frame_encode(out,size,1,id,text,strlen(text),offset);}
int main(void)
{
    const char *command="v1 1234567890abcdef 25 4 targets 1.5 2";
    uint8_t bytes[244];ble_frame_rx_t rx={0};size_t offset=0;
    while(offset<strlen(command)){
        size_t n=frame(bytes,20,42,command,offset);assert(n>8&&n<=20);
        ble_frame_result_t result=ble_frame_receive(&rx,bytes,n,1000000+(int64_t)offset);
        offset+=n-8;assert(result==(offset==strlen(command)?BLE_FRAME_COMPLETE:BLE_FRAME_PARTIAL));
    }
    assert(!strcmp(rx.data,command));assert(rx.id==42);
    // An old final fragment cannot execute twice.
    size_t n=frame(bytes,20,42,command,24);assert(ble_frame_receive(&rx,bytes,n,1100000)==BLE_FRAME_INVALID);
    n=frame(bytes,20,1,command,0);assert(ble_frame_receive(&rx,bytes,n,0)==BLE_FRAME_PARTIAL);
    n=frame(bytes,20,1,command,12);assert(ble_frame_receive(&rx,bytes,n,2000001)==BLE_FRAME_INVALID);
    // Interleaved ID, gap, overlap and changed total invalidate the assembly.
    for(unsigned test=0;test<4;test++){
        n=frame(bytes,20,2,command,0);assert(ble_frame_receive(&rx,bytes,n,0)==BLE_FRAME_PARTIAL);
        n=frame(bytes,20,2,command,12);
        if(test==0)bytes[2]=3;
        if(test==1)bytes[6]=13;
        if(test==2)bytes[6]=11;
        if(test==3)bytes[4]++;
        assert(ble_frame_receive(&rx,bytes,n,1)==BLE_FRAME_INVALID);
    }
    // Invalid versions/types, zero IDs, empty/oversized commands, control bytes.
    for(unsigned test=0;test<7;test++){
        n=frame(bytes,244,3,"status",0);
        if(test==0)bytes[0]=2;
        if(test==1)bytes[1]=2;
        if(test==2)bytes[2]=0;
        if(test==3)bytes[4]=0;
        if(test==4)bytes[4]=192;
        if(test==5)bytes[8]=0;
        if(test==6)bytes[8]=127;
        assert(ble_frame_receive(&rx,bytes,n,0)==BLE_FRAME_INVALID);
    }
    assert(ble_frame_receive(&rx,bytes,8,0)==BLE_FRAME_INVALID);
    char maximum[192];memset(maximum,'x',191);maximum[191]=0;
    n=frame(bytes,sizeof(bytes),4,maximum,0);assert(ble_frame_receive(&rx,bytes,n,0)==BLE_FRAME_COMPLETE);
    // Minimum-MTU status fragment roundtrip supports payloads beyond the ATT attribute limit.
    char status[3200];memset(status,'s',sizeof(status));offset=0;
    while(offset<sizeof(status)){
        n=ble_frame_encode(bytes,20,3,65535,status,sizeof(status),offset);assert(n>8&&n<=20);
        assert(bytes[0]==1&&bytes[1]==3&&bytes[2]==255&&bytes[3]==255);
        assert((size_t)(bytes[6]|bytes[7]<<8)==offset);assert(!memcmp(bytes+8,status+offset,n-8));offset+=n-8;
    }
    assert(!ble_frame_encode(bytes,8,1,1,"x",1,0));assert(!ble_frame_encode(bytes,20,1,0,"x",1,0));
    puts("BLE framing: minimum MTU, exact bounds, malformed fragments, timeout and replay checks passed");
}
