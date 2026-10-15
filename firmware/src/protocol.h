#pragma once
// Wire protocol, host -> device, written to the command characteristic.
// BLIT:  [0x02, x:u16le, y:u16le, w:u16le, h:u16le]
// DATA:  [0x03, bytes...]  1 bpp, MSB-first, rows padded to bytes, 1 = black
#define DISP_NAME         "SharpDisplay"
#define DISP_SERVICE_UUID "D15A0001-5B7A-4C3E-9F10-2A6E8C4B7D01"
#define DISP_CMD_UUID     "D15A0002-5B7A-4C3E-9F10-2A6E8C4B7D01"
#define CMD_CLEAR 0x01
#define CMD_BLIT  0x02
#define CMD_DATA  0x03
#define CMD_FLUSH 0x04
#define CMD_SET_FILTER 0x10   // [0x10, ascii glob...] name filter for the vario scan, e.g. "XC*"
#define CMD_SET_TZ  0x11   // [0x11, minutes:i16le] local time offset from UTC
#define CMD_SIM     0x12   // [0x12, 0|1] built-in flight simulator for bench testing
