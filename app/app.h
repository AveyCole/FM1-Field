/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Radio: an internet radio for the FM-1, on fm1-base's WiFi. Radio Garden's places built in, a place's stations
 * fetched live, MP3 streams over HTTP or HTTPS, punch-in effects on the keys. This header is the app's half of the
 * contract with the platform (fm1-base: device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "FIELD"
/* The target package may carry stock or third-party startup code. Reset the
 * peripherals too; OTA has already verified its power-loss-safe flash record. */
#define FM1_UPDATE_REBOOT fm1_reboot
#define APP_SCREEN_STREAM
void app_screen_blit(uint32_t x,uint32_t y,uint32_t w,uint32_t h,const uint16_t *pixels);
void app_screen_fill(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint16_t color);
#define APP_SPLASH
void app_splash(void);
#ifdef PLAT_HOST
#define APP_HOST_CMD
int app_host_cmd(const char *cmd,const char *a,const char *b,const char *c,const char *d);
#endif
#define APP_SPLASH_RGB RGB(48, 78, 56)
#define APP_BLOCK 256u
#define APP_CANVAS_PX (240u * 44u)       /* strips, including the 42-row password keyboard */
#define APP_BIG_FONT FONT_B             /* compact splash leaves flash for the offline region index */
#define FM1B_UAC_RING_POOL 1             /* USB audio's ring in the pool: the RAM is full (fm1-base unity.c) */
#define APP_WIFI_TX_RATES 0x7FFu          /* WiFi sends at 1 .. 18M and MCS0-2 (21.7M) only: the fast rates fail at
                                         * the FM-1's range (-75 dBm and under), and a stream needs < 1M */
#define FM1B_TRS 0                       /* no MIDI in from the jack: a radio (the flash goes to the places) */

/* the store: the panel calibration (the platform's), the radio's address and calibration (OBJ_BLE), the networks
 * kept (OBJ_WIFI), the station of the last session and the synth's settings (OBJ_RADIO), the favorites (OBJ_FAV:
 * the places area ends below it) */
enum { OBJ_PANEL, OBJ_BLE, OBJ_WIFI, OBJ_RADIO, OBJ_FAV, OBJ_NOBJ };
#define APP_STORE_MAP {0xDC000u, 0xDE000u, 0xDA000u, 0xD8000u, 0xD6000u}
#define APP_HOST_EXPECT                  /* the host simulator's "expect" asks app_expect (app.c) first */
int app_expect(const char *what, int *got);

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
#define APP_LOOP                         /* every pass of the main loop: the decoder and the tuning */
void app_loop(void);
#define APP_SYSEX                        /* Radio's own commands over USB (F0 7D 57 ...: tools/radio_ctl.py) */
int app_sysex(const uint8_t *p, uint32_t n);
