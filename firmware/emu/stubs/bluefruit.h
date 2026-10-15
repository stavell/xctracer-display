#pragma once
#include <stdint.h>
typedef struct { uint8_t addr_id_peer : 1; uint8_t addr_type : 7; uint8_t addr[6]; } ble_gap_addr_t;
