/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../app/aporee.h"
#define PLAT_STORE_MAX 3840u
#define w_cpy(d,s,n) snprintf(d,n,"%s",s)
#include "../app/field_fav.c"
int main(void)
{
    field_favs_t f,bad;
    ap_track_t t={0},out;
    unsigned i;
    field_favs_clear(&f);
    assert(field_favs_valid(&f,8));
    strcpy(t.url,"https://aporee.org/maps/test.mp3");strcpy(t.id,"100");
    strcpy(t.title,"A shoreline");strcpy(t.place,"Somewhere");
    strcpy(t.artist,"Recordist");strcpy(t.licence,"CC BY");t.lat=525200;t.lon=134050;
    assert(!field_favs_add(&f,&t));
    assert(field_favs_add(&f,&t)==-2);
    strcpy(t.url,"https://aporee.org/maps/renamed.mp3");
    assert(field_favs_add(&f,&t)==-2); /* stable sound id */
    field_favs_read(&f,0,&out);
    assert(!strcmp(out.url,"https://aporee.org/maps/test.mp3"));
    assert(!strcmp(out.artist,t.artist) && out.lat==t.lat && !strcmp(out.licence,t.licence));
    bad=f;bad.data[8]=0;assert(!field_favs_valid(&bad,8+bad.used));
    bad=f;bad.used++;assert(!field_favs_valid(&bad,8+bad.used));
    bad=f;bad.magic=0x46415631;assert(!field_favs_valid(&bad,8+bad.used));
    assert(!field_favs_valid(&f,7+f.used));
    for(i=1;i<FIELD_FAV_MAX;i++) {
        snprintf(t.id,sizeof t.id,"%u",100+i);
        snprintf(t.url,sizeof t.url,"https://aporee.org/maps/%u.mp3",i);
        assert(!field_favs_add(&f,&t));
    }
    strcpy(t.id,"999");strcpy(t.url,"https://aporee.org/maps/extra.mp3");
    assert(field_favs_add(&f,&t)==-1);
    field_favs_delete(&f,5);assert(f.count==15 && field_favs_valid(&f,8+f.used));
    field_favs_read(&f,5,&out);assert(!strcmp(out.id,"106"));
    assert(!field_favs_add(&f,&t));
    while(f.count) field_favs_delete(&f,0);
    assert(f.used==0 && field_favs_valid(&f,8));
    /* Maximum-length URLs round trip intact; byte capacity can fill before
     * the entry limit. A failed append leaves all existing entries intact. */
    memset(t.url,'x',sizeof t.url-1);t.url[sizeof t.url-1]=0;
    for(i=0;i<16;i++) {
        int result;
        snprintf(t.id,sizeof t.id,"%u",i);t.url[0]=(char)('a'+i);
        bad=f;result=field_favs_add(&f,&t);
        if(result==-1) {assert(!memcmp(&f,&bad,sizeof f));break;}
        assert(!result && field_favs_valid(&f,8+f.used));
        field_favs_read(&f,i,&out);assert(!strcmp(t.url,out.url));
    }
    assert(i>0 && i<16);
    puts("field favorites: exact replay data, duplicates, capacity, removal and malformed storage passed");
    return 0;
}
