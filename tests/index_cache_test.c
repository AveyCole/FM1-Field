/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fastmath.h"
static uint8_t flash[0x21000],snapshot[0x21000],clip[24576];
static int fail_step=-1,step,partial;static unsigned mutations;
const uint8_t *plat_data(uint32_t off) {return off>=0x97000 && off<0xB8000?flash+off-0x97000:0;}
int plat_data_erase(uint32_t off)
{
 assert(off>=0xB0000 && off+4096<=0xB8000 && !(off&4095));mutations++;
 if(step++==fail_step) return -1;
 memset(flash+off-0x97000,255,4096);return 0;
}
int plat_data_write(uint32_t off,const void *src,uint32_t n)
{
 unsigned count=n;int fail=step++==fail_step;
 assert(off>=0xB0000 && off+n<=0xB8000);mutations++;
 if(fail) count=(unsigned)partial<n?(unsigned)partial:n;
 for(unsigned i=0;i<count;i++) flash[off-0x97000+i]&=((const uint8_t *)src)[i];
 return fail?-1:0;
}
#include "../app/sampler.c"
#include "../app/sample_store.c"
typedef struct {int fd;unsigned pos,playlist;char *why;} conn_t;
static unsigned scans,playlists,now;static int tls_taken;
static int radio_net_running(void) {return 0;}
static uint32_t shim_ms(void) {return now++;}
static void os_time_dly(int n) {now+=(unsigned)n*10;}
static void why_to(char *dst,const char *src) {snprintf(dst,32,"%s",src);}
static void radio_entropy(uint8_t *p,unsigned n) {memset(p,77,n);}
static int task_create(void (*f)(void *),void *arg,const char *name) {(void)name;f(arg);return 0;}
static int http_open(conn_t *c,char *u,unsigned cap,int redirects)
{
 (void)cap;(void)redirects;c->pos=0;c->playlist=strstr(u,"getPlaylist")!=0;
 if(c->playlist)playlists++;else scans++;return 0;
}
static int conn_read(conn_t *c,uint8_t *b,unsigned n)
{
 const char *s=c->playlist?"[{\"mp3\":\"https://aporee.org/maps/files/test.mp3\",\"sndid\":\"1\"}]":
 "[{\"locid\":\"1\",\"lat\":\"52.52\",\"lng\":\"13.40\"},{\"locid\":\"2\",\"lat\":\"52.53\",\"lng\":\"13.41\"}]";
 unsigned left=(unsigned)strlen(s)-c->pos;if(n>left)n=left;
 memcpy(b,s+c->pos,n);c->pos+=n;return (int)n;
}
static void conn_close(conn_t *c) {(void)c;}
static void js_put(char *d,unsigned max,const char *s,unsigned n)
{if(n>=max)n=max-1;memcpy(d,s,n);d[n]=0;}
#include "../app/net/aporee_net.c"
#include "../app/field_index_cache.c"
static void reboot_net(void)
{
 free(ap_locs);ap_locs=ap_world_locs=0;ap_n=ap_world_n=ap_seen=0;
 ap_cache_region=ap_cache_near=ap_refresh=0;ap_cache_lat=ap_cache_lon=0;ap_rng=ap_last=0;ap_state=AP_IDLE;
}
int main(void)
{
 ap_loc_t a[32],b[32];memset(flash,255,sizeof flash);sam_init(clip);
 for(unsigned i=0;i<32;i++)a[i]=(ap_loc_t){i+1,525200+(int)i,134000+(int)i};
 assert(!ap_cache_load(0,0,0,0,b));ap_cache_save(0,0,0,0,a,32);
 assert(ap_cache_load(0,0,0,0,b)==32 && !memcmp(a,b,sizeof a));
 unsigned count=mutations;ap_cache_save(0,0,0,0,a,32);assert(mutations==count);
 memcpy(snapshot,flash,sizeof flash);a[0].id=99;
 for(int f=0;f<3;f++)for(int cut=0;cut<(f==2?4:410);cut++) {
  memcpy(flash,snapshot,sizeof flash);fail_step=f;step=0;partial=cut;
  ap_cache_save(0,0,0,0,a,32);fail_step=-1;
  assert(ap_cache_load(0,0,0,0,b)==32 && b[0].id==1);
 }
 memcpy(flash,snapshot,sizeof flash);ap_cache_save(0,0,0,0,a,32);
 assert(ap_cache_load(0,0,0,0,b)==32 && b[0].id==99);
 ((uint8_t *)index_valid(1))[30]^=1;
 assert(ap_cache_load(0,0,0,0,b)==32 && b[0].id==1);
 for(unsigned i=1;i<30;i++)ap_cache_save(0,1,525200+(int)i,134000,a,32);
 assert(ap_cache_load(0,0,0,0,b)==32 && b[0].id==1);
 assert(ap_cache_load(0,1,525229,134000,b)==32);
 assert(!ap_cache_load(0,1,525229,134001,b));
 for(unsigned i=0;i<0x19000;i++)assert(flash[i]==255); /* samples untouched */
 memset(flash,255,sizeof flash);assert(!ap_start(0) && ap_state==AP_DONE && scans==1);
 for(unsigned i=0;i<8;i++)assert(!ap_start(0) && ap_state==AP_DONE);
 assert(scans==1 && playlists==9);reboot_net();
 assert(!ap_start(0) && ap_state==AP_DONE && scans==1); /* actual boot path */
 assert(!ap_start_near(525200,134000) && ap_state==AP_DONE && scans==2);
 assert(!ap_start(0) && ap_state==AP_DONE && scans==2);
 assert(!ap_refresh_index());assert(!ap_start(0) && ap_state==AP_DONE && scans==3);
 ap_state=AP_BUSY;assert(ap_refresh_index()==-1);ap_state=AP_DONE;
 reboot_net();assert(!ap_start_near(525200,134000) && ap_state==AP_DONE && scans==3);
 puts("PASS index cache: power cuts, corruption fallback, wear, eviction, world retention, sample isolation; real network boot/exhaustion/refresh paths skip full scans");
 return 0;
}
