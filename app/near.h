/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "../base/include/fastmath.h"
/* Haversine threshold on a 6,371 km sphere. Wrapping longitude via sine
 * handles the date line; cosine handles high latitudes without a square box. */
static int ap_near_contains(int32_t lat,int32_t lon,int32_t center_lat,int32_t center_lon)
{
    float a,b,h,s;
    if(lat-center_lat>4500 || center_lat-lat>4500) return 0;
    a=fm_sinf((lat-center_lat)*(FM_PI/3600000.0f));
    b=fm_sinf((lon-center_lon)*(FM_PI/3600000.0f));
    h=a*a+fm_cosf(lat*(FM_PI/1800000.0f))*fm_cosf(center_lat*(FM_PI/1800000.0f))*b*b;
    s=fm_sinf(50.0f/12742.0f);
    return h<=s*s;
}
