/* SPDX-License-Identifier: MPL-2.0 */
/* Eight rotating sectors for 32-location reservoirs, separate from samples.
 * Payload first, magic last; retain the newest matching and worldwide copies. */
#include "aporee.h"
#include "regions.h"
#define INDEX_BASE 0xB0000u
#define INDEX_SLOTS 8u
#define INDEX_MAGIC 0x31435849u
typedef struct {
 uint32_t magic,seq;
 uint16_t region;
 uint8_t near,n;
 int32_t lat,lon;
 ap_loc_t locs[32];
 uint32_t crc;
} index_record_t;
static const index_record_t *index_valid(unsigned slot)
{
 const index_record_t *p=(const void *)plat_data(INDEX_BASE+slot*4096u);unsigned i;
 if(!p || p->magic!=INDEX_MAGIC || !p->n || p->n>32 || p->near>1 || p->region>=ap_region_total() ||
    p->lat < -900000 || p->lat>900000 || p->lon < -1800000 || p->lon>1800000 ||
    p->crc!=clip_crc(&p->seq,sizeof *p-8u)) return 0;
 for(i=0;i<p->n;i++) if(!p->locs[i].id || p->locs[i].lat < -900000 || p->locs[i].lat>900000 || p->locs[i].lon < -1800000 || p->locs[i].lon>1800000) return 0;
 return p;
}
static int index_key(const index_record_t *p,uint16_t region,int near,int32_t lat,int32_t lon)
{return p->region==region && p->near==(near!=0) && (!near || (p->lat==lat && p->lon==lon));}
unsigned ap_cache_load(uint16_t region,int near,int32_t lat,int32_t lon,ap_loc_t *locs)
{
 const index_record_t *best=0,*p;unsigned i;
 for(i=0;i<INDEX_SLOTS;i++) if((p=index_valid(i)) && index_key(p,region,near,lat,lon) && (!best || (int32_t)(p->seq-best->seq)>0)) best=p;
 if(!best) return 0;
 memcpy(locs,best->locs,best->n*sizeof *locs);return best->n;
}
void ap_cache_save(uint16_t region,int near,int32_t lat,int32_t lon,const ap_loc_t *locs,unsigned n)
{
 index_record_t out;const index_record_t *newest=0,*same=0,*world=0,*oldest=0,*p;
 unsigned i,slot=INDEX_SLOTS;uint32_t off;
 if(!n || n>32 || region>=ap_region_total()) return;
 for(i=0;i<INDEX_SLOTS;i++) {
  p=index_valid(i);if(!p) {if(slot==INDEX_SLOTS)slot=i;continue;}
  if(!newest || (int32_t)(p->seq-newest->seq)>0) newest=p;
  if(index_key(p,region,near,lat,lon) && (!same || (int32_t)(p->seq-same->seq)>0)) same=p;
  if(!p->region && !p->near && (!world || (int32_t)(p->seq-world->seq)>0)) world=p;
 }
 if(same && same->n==n && !memcmp(same->locs,locs,n*sizeof *locs)) return;
 if(slot==INDEX_SLOTS) for(i=0;i<INDEX_SLOTS;i++) {
  p=index_valid(i);if(p==same || p==world)continue;
  if(!oldest || (int32_t)(p->seq-oldest->seq)<0) {oldest=p;slot=i;}
 }
 if(slot==INDEX_SLOTS) return;
 memset(&out,0,sizeof out);out.magic=INDEX_MAGIC;out.seq=newest?newest->seq+1u:1u;
 out.region=region;out.near=(uint8_t)(near!=0);out.n=(uint8_t)n;out.lat=near?lat:0;out.lon=near?lon:0;
 memcpy(out.locs,locs,n*sizeof *locs);out.crc=clip_crc(&out.seq,sizeof out-8u);off=INDEX_BASE+slot*4096u;
 if(plat_data_erase(off) || plat_data_write(off+4,(uint8_t *)&out+4,sizeof out-4u) ||
    memcmp(plat_data(off)+4,(uint8_t *)&out+4,sizeof out-4u)) return;
 if(plat_data_write(off,&out.magic,4)) return;
 (void)index_valid(slot);
}
