/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <stdint.h>
enum { AP_IDLE, AP_BUSY, AP_DONE, AP_ERROR };
typedef struct {
    char url[512], title[96], place[96], artist[64], licence[32], id[16];
    int32_t lat, lon; /* degrees * 10000 */
} ap_track_t;
extern ap_track_t ap_track;
extern volatile int ap_state;
extern volatile uint32_t ap_bytes;
extern char ap_why[32];
int ap_start(uint16_t region);
int ap_start_near(int32_t lat,int32_t lon);
