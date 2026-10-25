/* SPDX-License-Identifier: MPL-2.0 */
/* Run the actual streaming parser + selection code against captured public API
 * responses, with a filesystem transport. No device or live network required. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
typedef struct { int sock; int a,b; char *why; } conn_t;
static FILE *response;
static int tls_taken, request_count;
static const char *index_path, *playlist_path;
static uint32_t shim_ms(void) { static uint32_t t; return t+=100000; }
static void os_time_dly(int n) {(void)n;}
static void why_to(char *d,const char *s) {snprintf(d,32,"%s",s);}
static int http_open(conn_t *c,char *u,unsigned max,int json) {
    (void)c;(void)max;(void)json; request_count++;
    response=fopen(strstr(u,"getAllLocations")?index_path:playlist_path,"rb");
    return response?0:-1;
}
static int conn_read(conn_t *c,uint8_t *b,unsigned n) {(void)c;return (int)fread(b,1,n,response);}
static void conn_close(conn_t *c) {(void)c;if(response)fclose(response);response=0;}
static int radio_net_running(void) {return 0;}
static void radio_entropy(uint8_t *b,unsigned n) {memset(b,42,n);}
static int task_create(void (*fn)(void *),void *a,const char *name) {(void)name;fn(a);return 0;}
static void js_put(char *d,unsigned max,const char *s,unsigned n) {if(n>=max)n=max-1;memcpy(d,s,n);d[n]=0;}
#include "../app/net/aporee_net.c"
static unsigned fields,objects;
static void count_field(void *c,const char *k,const char *v,int cut) {(void)c;(void)k;(void)v;(void)cut;fields++;}
static void count_object(void *c) {(void)c;objects++;}
static void parser_cases(void) {
    const char *good="[{\"lat\":-12.34,\"title\":\"quote \\\" \\u00e9\",\"locid\":\"42\"}]";
    ap_json_t j={0};j.field=count_field;j.object=count_object;
    for(unsigned i=0;i<strlen(good);i++)apj_feed(&j,(const uint8_t *)good+i,1);
    assert(apj_ok(&j)&&fields==3&&objects==1);
    memset(&j,0,sizeof j);j.field=count_field;j.object=count_object;
    apj_feed(&j,(const uint8_t *)good,strlen(good)-1);assert(!apj_ok(&j));
    memset(&j,0,sizeof j);j.field=count_field;j.object=count_object;
    apj_feed(&j,(const uint8_t *)"radio aporee",12);assert(!apj_ok(&j));
    int32_t out;assert(ap_coord("-179.123456",&out,180)&&out==-1791234);
    assert(ap_coord("0",&out,90)&&out==0);assert(!ap_coord("91",&out,90));
    assert(!ap_coord("NaN",&out,180));assert(!ap_coord("180.01",&out,180));
}
int main(int argc,char **argv) {
    assert(argc==3);index_path=argv[1];playlist_path=argv[2];parser_cases();
    uint32_t seen[32];
    for(unsigned i=0;i<32;i++) {
        assert(ap_start(0)==0&&ap_state==AP_DONE);
        assert(ap_track.url[0]&&ap_track.title[0]&&ap_track.artist[0]&&ap_track.licence[0]);
        assert(ap_track.lat>=-900000&&ap_track.lat<=900000);
        assert(ap_track.lon>=-1800000&&ap_track.lon<=1800000);
        for(unsigned k=0;k<i;k++)assert(seen[k]!=ap_last);
        seen[i]=ap_last;
    }
    assert(request_count==33&&ap_n==0&&ap_seen>60000);
    assert(ap_start(0)==0&&request_count==35);
    unsigned world_remaining=ap_n;
    uint16_t germany=0,texas=0,single=0;
    for(unsigned i=1;i<ap_region_total();i++) {
        if(!strcmp(ap_region_name(i),"Germany")&&!ap_region_parent(i))germany=(uint16_t)i;
        if(!strcmp(ap_region_name(i),"Texas"))texas=(uint16_t)i;
        if(!ap_region_children(i)&&ap_region_locations(i)==1)single=(uint16_t)i;
    }
    assert(germany&&texas&&single);
    assert(ap_start(germany)==0&&ap_state==AP_DONE);
    assert(ap_seen==ap_region_locations(germany)&&ap_region_matches(ap_last,germany));
    for(unsigned i=0;i<ap_n;i++)assert(ap_region_matches(ap_locs[i].id,germany));
    assert(ap_start(texas)==0&&ap_state==AP_DONE);
    assert(ap_seen==ap_region_locations(texas)&&ap_region_matches(ap_last,texas));
    for(unsigned i=0;i<ap_n;i++)assert(ap_region_matches(ap_locs[i].id,texas));
    assert(ap_start(single)==0&&ap_state==AP_DONE&&ap_seen==1);
    uint32_t only=ap_last;
    assert(ap_start(single)==0&&ap_state==AP_DONE&&ap_last==only);
    unsigned requests=(unsigned)request_count;
    assert(ap_start(0)==0&&ap_state==AP_DONE);
    assert((unsigned)request_count==requests+1u&&ap_n==world_remaining-1u);
    assert(ap_start((uint16_t)ap_region_total())==-1);
    unsigned saved_world=ap_n;
    assert(ap_start_near(525200,134050)==0 && ap_state==AP_DONE && ap_cache_near);
    assert(ap_near_contains(ap_track.lat,ap_track.lon,525200,134050));
    for(unsigned i=0;i<ap_n;i++) assert(ap_near_contains(ap_locs[i].lat,ap_locs[i].lon,525200,134050));
    requests=(unsigned)request_count;
    assert(ap_start_near(525200,134050)==0 && ap_state==AP_DONE && (unsigned)request_count==requests+1u);
    assert(ap_start(0)==0 && ap_state==AP_DONE && !ap_cache_near && ap_n==saved_world-1);
    assert(ap_start_near(910000,0)==-1);
    assert(ap_near_contains(0,-1799000,0,1799000));
    assert(ap_near_contains(899000,1800000,899000,0));
    assert(ap_near_contains(4490,0,0,0) && !ap_near_contains(4510,0,0,0));
    puts("PASS: fixed 50 km Near filter, date line/poles, nearby cache and world restoration");
    puts("PASS: country/state filtering, one-location regions, worldwide cache restoration, invalid region rejection");
    puts("PASS: real Aporee index/playlist, 32 unique locations, refill, bytewise JSON, coordinates, incomplete/error responses");
}
