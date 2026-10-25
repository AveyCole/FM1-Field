/* SPDX-License-Identifier: MPL-2.0 */
/* One copy in the network unit (and in the host simulator), never in both app units. */
#include "regions.h"
#include "regions_data.h"
#include "inflate.c"
static uint8_t *ap_geo_page;
static uint32_t ap_geo_cached = 0xFFFFFFFFu;
static unsigned ap_geo_written;
unsigned ap_region_total(void) {return sizeof AP_REGIONS/sizeof AP_REGIONS[0];}
unsigned ap_country_total(void) {return sizeof AP_COUNTRIES/sizeof AP_COUNTRIES[0];}
uint16_t ap_country_at(unsigned n) {return n<ap_country_total()?AP_COUNTRIES[n]:0;}
const char *ap_region_name(uint16_t r) {return AP_REGION_NAMES+AP_REGIONS[r<ap_region_total()?r:0].name;}
unsigned ap_region_children(uint16_t r) {return r<ap_region_total()?AP_REGIONS[r].children:0;}
unsigned ap_region_locations(uint16_t r) {return r<ap_region_total()?AP_REGIONS[r].count:0;}
uint16_t ap_region_parent(uint16_t r) {return r<ap_region_total()?AP_REGIONS[r].parent:0;}
int ap_regions_ready(void)
{
    if (!ap_geo_page) ap_geo_page=malloc(AP_REGION_PAGE_IDS*2u);
    return ap_geo_page!=0;
}
static int ap_geo_byte(void *ctx,uint8_t c)
{
    (void)ctx;
    if(ap_geo_written<AP_REGION_PAGE_IDS*2u) ap_geo_page[ap_geo_written++]=c;
    return ap_geo_written>=AP_REGION_PAGE_IDS*2u;
}
uint16_t ap_location_region(uint32_t id)
{
    uint32_t page=id/AP_REGION_PAGE_IDS, at=(id%AP_REGION_PAGE_IDS)*2u;
    if(id>AP_REGION_MAX_ID || !ap_regions_ready()) return 0;
    if(page!=ap_geo_cached) {
        uint32_t off=AP_REGION_PAGES[page], len=AP_REGION_PAGES[page+1]-off;
        ap_geo_written=0;
        if(inflate_mem(AP_REGION_IDS+off,len,ap_geo_byte,0) || ap_geo_written!=AP_REGION_PAGE_IDS*2u) return 0;
        ap_geo_cached=page;
    }
    return (uint16_t)(ap_geo_page[at] | (uint16_t)ap_geo_page[at+1]<<8);
}
int ap_region_matches(uint32_t id,uint16_t selected)
{
    uint16_t region;
    if(!selected) return 1;
    region=ap_location_region(id);
    return region && (region==selected || ap_region_parent(region)==selected);
}
