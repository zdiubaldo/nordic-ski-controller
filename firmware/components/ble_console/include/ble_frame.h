#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BLE_FRAME_HEADER 8
#define BLE_COMMAND_MAX 191
#define BLE_FRAME_VERSION 1
// Little endian header: version, kind, message ID, total length, byte offset.
typedef struct { uint16_t id, total, used; int64_t started_us; char data[BLE_COMMAND_MAX+1]; } ble_frame_rx_t;
typedef enum { BLE_FRAME_INVALID=-1, BLE_FRAME_PARTIAL=0, BLE_FRAME_COMPLETE=1 } ble_frame_result_t;
ble_frame_result_t ble_frame_receive(ble_frame_rx_t *rx,const uint8_t *data,size_t size,int64_t now);
size_t ble_frame_encode(uint8_t *out,size_t capacity,uint8_t kind,uint16_t id,const char *body,size_t total,size_t offset);
