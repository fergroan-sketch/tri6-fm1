/* SPDX-License-Identifier: MPL-2.0 */
/* TRI-6 FM-1: a Trigon-6 style polysynth firmware for the M-VAVE FM-1, on fm1-base. Unofficial: not affiliated with
 * or endorsed by Sequential or M-VAVE. This header is the app's half of the contract with the platform. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "TRI-6 FM-1"
#define APP_SPLASH_RGB RGB(226, 160, 64)
#define APP_SPLASH                       /* the boot screen: app_splash (app.c), a striped triangle and a keyboard */
void app_splash(void);
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_TOUCH                        /* velocity from the keys (lib/touch.h) */
#define APP_BLE_AT_BOOT (!tri_ble_off())
int tri_ble_off(void);

/* the store: the settings, four blocks of 32 patches (000-031 ... 096-127), the panel calibration (the
 * platform's), Bluetooth's pairing */
enum { OBJ_SETTINGS, OBJ_BANK0, OBJ_BANK1, OBJ_BANK2, OBJ_BANK3, OBJ_PANEL, OBJ_BLE, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xCC000u, 0xCE000u, 0xD0000u, 0xD2000u, 0xDC000u, 0xDE000u}

/* SysEx from USB: TRI-6 FM-1 patch dumps (its own format) */
#define APP_SYSEX
int app_sysex(const uint8_t *p, uint32_t n);

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
