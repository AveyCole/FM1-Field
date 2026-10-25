/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <stdint.h>
unsigned ap_region_total(void);
unsigned ap_country_total(void);
uint16_t ap_country_at(unsigned n);
const char *ap_region_name(uint16_t region);
unsigned ap_region_children(uint16_t region);
unsigned ap_region_locations(uint16_t region);
uint16_t ap_region_parent(uint16_t region);
int ap_regions_ready(void);
uint16_t ap_location_region(uint32_t id);
int ap_region_matches(uint32_t id,uint16_t selected);
