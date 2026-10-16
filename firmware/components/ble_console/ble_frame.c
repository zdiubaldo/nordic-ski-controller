#include "ble_frame.h"
#include <string.h>
static uint16_t get16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
ble_frame_result_t ble_frame_receive(ble_frame_rx_t *r,const uint8_t *p,size_t n,int64_t now)
{
    if(!r)return BLE_FRAME_INVALID;
    if(!p||n<=BLE_FRAME_HEADER||p[0]!=1||p[1]!=1)goto invalid;
    uint16_t id=get16(p+2),total=get16(p+4),offset=get16(p+6);
    size_t length=n-BLE_FRAME_HEADER;
    if(!id||!total||total>BLE_COMMAND_MAX||offset>total||length>(size_t)(total-offset))goto invalid;
    if(!offset){r->id=id;r->total=total;r->used=0;r->started_us=now;}
    if(r->id!=id||r->total!=total||r->used!=offset||now-r->started_us>2000000)goto invalid;
    for(size_t i=0;i<length;i++)if(p[BLE_FRAME_HEADER+i]<32||p[BLE_FRAME_HEADER+i]>126)goto invalid;
    memcpy(r->data+offset,p+BLE_FRAME_HEADER,length);r->used+=(uint16_t)length;
    r->data[r->used]=0;
    if(r->used!=total)return BLE_FRAME_PARTIAL;
    // Caller consumes data synchronously. A subsequent fragment must start at zero.
    r->used=0;return BLE_FRAME_COMPLETE;
invalid:
    memset(r,0,sizeof(*r));return BLE_FRAME_INVALID;
}
size_t ble_frame_encode(uint8_t *out,size_t capacity,uint8_t kind,uint16_t id,const char *body,size_t total,size_t offset)
{
    if(!out||!body||capacity<=BLE_FRAME_HEADER||!id||!total||total>UINT16_MAX||offset>=total)return 0;
    size_t length=total-offset;
    if(length>capacity-BLE_FRAME_HEADER)length=capacity-BLE_FRAME_HEADER;
    out[0]=1;out[1]=kind;put16(out+2,id);put16(out+4,(uint16_t)total);put16(out+6,(uint16_t)offset);
    memcpy(out+BLE_FRAME_HEADER,body+offset,length);return BLE_FRAME_HEADER+length;
}
