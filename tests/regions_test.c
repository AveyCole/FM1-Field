/* SPDX-License-Identifier: MPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "../app/regions.c"
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    unsigned i,matched=0;
    for(i=0;i<=AP_REGION_MAX_ID;i++) {
        unsigned lo=(unsigned)fgetc(f),hi=(unsigned)fgetc(f);
        uint16_t expected=(uint16_t)(lo|hi<<8),got=ap_location_region(i);
        assert(got==expected);if(got) matched++;
        assert(ap_region_matches(i,0));
        if(got) {assert(ap_region_matches(i,got));assert(ap_region_matches(i,ap_region_parent(got)));}
    }
    assert(fgetc(f)==EOF);fclose(f);assert(matched>60000);
    assert(ap_location_region(AP_REGION_MAX_ID+1)==0);
    uint16_t wisconsin=ap_location_region(23019);
    assert(!strcmp(ap_region_name(wisconsin),"Wisconsin"));
    assert(!strcmp(ap_region_name(ap_region_parent(wisconsin)),"United States of America"));
    for(i=0;i<ap_country_total();i++) {
        uint16_t c=ap_country_at(i);unsigned count=0;
        assert(ap_region_parent(c)==0&&ap_region_children(c));
        for(unsigned j=1;j<=ap_region_children(c);j++) {assert(ap_region_parent(c+j)==c);count+=ap_region_locations(c+j);}
        assert(count==ap_region_locations(c));
    }
    printf("PASS: all %u compressed lookups, %u classified locations, country/state membership and hierarchy\n",i=AP_REGION_MAX_ID+1,matched);
    free(ap_geo_page);
}
