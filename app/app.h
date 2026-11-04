/* SPDX-License-Identifier: GPL-3.0-only */
/* QUADRO on fm1-base: the app's half of the contract with the platform (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "QUADRO"
#define APP_TAGLINE "BASS STRINGS POLY LEAD"
#define APP_SPLASH_RGB RGB(244, 180, 52)
#define APP_BLOCK 128u                   /* frames per audio block (2.9 ms), four of QUADRO's 32 */
#define APP_CANVAS_PX (240u * 90u)       /* the biggest region: the picture */
#define APP_TOUCH                        /* velocity and pressure from the keys: the lead's TOUCH SENSOR, MIDI out */
#define APP_BLE_AT_BOOT (!quadro_ble_off())
int quadro_ble_off(void);
#define APP_TICK                         /* the MIDI clock out, 1 kHz */
void app_tick(void);
#define APP_OVERLOAD                     /* the render ran late: fewer voices */
void app_overload(void);

/* the store: the programs and the settings; the panel calibration and Bluetooth's (the platform's) */
enum { OBJ_PROGS, OBJ_PANEL, OBJ_BLE, OBJ_PROGS2, OBJ_PROGS3, OBJ_PROGS4, OBJ_NOBJ };   /* the programs in four parts */
#define APP_STORE_MAP {0xDC000u, 0xD4000u, 0xD6000u, 0xDA000u, 0xD8000u, 0xD2000u}

/* the host simulator's checks (host/plat_host.c "expect"): prog (1-based), pwave, bwave, lwave, ppw, nprog, section, page */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
