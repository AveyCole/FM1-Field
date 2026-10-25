/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fastmath.h"
static uint8_t flash[0x19000],snapshot[0x19000],clip[24576];
static int fail_step=-1,step,partial;static unsigned mutations;
const uint8_t *plat_data(uint32_t off) {return off>=0x97000 && off<0xB0000?flash+off-0x97000:0;}
int plat_data_erase(uint32_t off)
{
 assert(off>=0x97000 && off+4096<=0xB0000 && !(off&4095));mutations++;
 if(step++==fail_step) return -1;
 memset(flash+off-0x97000,255,4096);return 0;
}
int plat_data_write(uint32_t off,const void *src,uint32_t n)
{
 unsigned count=n;int fail=step++==fail_step;
 assert(off>=0x97000 && off+n<=0xB0000);mutations++;
 if(fail) count=(unsigned)partial<n?(unsigned)partial:n;
 for(unsigned i=0;i<count;i++) flash[off-0x97000+i]&=((const uint8_t *)src)[i];
 return fail?-1:0;
}
#include "../app/sampler.c"
#include "../app/sample_store.c"
static void make_clip(float value,unsigned frames)
{
 sam_init(clip);sam_record();
 while(sam_len<frames) sam_capture(value,value,1);
 sam_finish();
}
int main(void)
{
 memset(flash,255,sizeof flash);sam_init(clip);assert(!sam_restore());
 assert(sam_save()==-2 && mutations==0);
 make_clip(.25f,8000);sam_start=12;sam_end=100;sam_pitch=7;sam_oct=-1;sam_level=83;sam_loop=3;sam_slices=8;
 uint32_t expected=clip_crc(clip,clip_bytes(sam_len));assert(!sam_save());
 memcpy(snapshot,flash,sizeof flash);
 sam_init(clip);assert(sam_restore());assert(sam_len==8000 && sam_start==12 && sam_end==100 && sam_pitch==7 && sam_oct==-1 && sam_level==83 && sam_loop==3 && sam_slices==8);
 assert(clip_crc(clip,clip_bytes(sam_len))==expected);
 /* Cuts during erases, payload, and every incomplete commit-header length
  * must retain the previous logical clip. */
 for(int fail=0;fail<10;fail++) for(int cut=0;cut<(fail==9?(int)sizeof(clip_header_t):8);cut++) {
  memcpy(flash,snapshot,sizeof flash);make_clip(-.4f,SAM_FRAMES);step=0;fail_step=fail;
  partial=fail==9?cut:cut*1500;assert(sam_save()==-1);fail_step=-1;
  sam_init(clip);assert(sam_restore() && sam_len==8000);assert(clip_crc(clip,clip_bytes(sam_len))==expected);
 }
 memcpy(flash,snapshot,sizeof flash);make_clip(-.4f,SAM_FRAMES);assert(!sam_save());
 sam_init(clip);assert(sam_restore() && sam_len==SAM_FRAMES);
 flash[0x9000+CLIP_BANK+64+10]^=1;sam_init(clip);assert(sam_restore() && sam_len==8000); /* bad latest body falls back */
 flash[0x9000+64+10]^=1;sam_init(clip);assert(!sam_restore() && sam_state==SAM_EMPTY);
 memcpy(flash,snapshot,sizeof flash);
 clip_header_t *h=(void *)(flash+0x9000);h->seq=0xFFFFFFFEu;h->hcrc=clip_crc(h,sizeof *h-4u);
 make_clip(.1f,4000);assert(!sam_save());assert(clip_current()->seq==0xFFFFFFFFu);
 make_clip(.2f,6000);assert(!sam_save());assert(clip_current()->seq==0 && clip_current()->len==6000);
 /* Zero in the former reserved byte retains legacy 8 kHz clip behavior. */
 memcpy(flash,snapshot,sizeof flash);sam_init(clip);assert(sam_restore() && sam_rate()==8000);
 for(unsigned q=0;q<4;q++) {
  sam_init(clip);sam_quality=(uint8_t)q;sam_record();
  while(sam_len<10000) sam_capture(.25f,.25f,1);
  sam_finish();sam_quality=0; /* save the clip's actual rate, not a next-capture choice */
  expected=clip_crc(clip,clip_bytes(sam_len));assert(!sam_save());
  sam_init(clip);assert(sam_restore() && sam_rate()==(16000u>>q) && sam_quality==q && sam_len==10000);
  assert(clip_crc(clip,clip_bytes(sam_len))==expected);
 }
 h=(clip_header_t *)clip_current();h->rate_shift=4;h->hcrc=clip_crc(h,sizeof *h-4u);
 sam_init(clip);assert(sam_restore() && sam_rate()==4000); /* invalid newest rate falls back */
 /* Legacy banks remain intact while a larger-format save is staged. */
 memcpy(flash,snapshot,sizeof flash);
 memcpy(flash,flash+0x9000,64+clip_bytes(8000));
 memset(flash+0x9000,255,0x10000);
 h=(void *)flash;h->magic=CLIP_OLD_MAGIC;h->rate_shift=0;h->hcrc=clip_crc(h,sizeof *h-4u);
 sam_init(clip);assert(sam_restore() && sam_rate()==8000 && sam_len==8000);
 memcpy(snapshot,flash,sizeof flash);step=0;fail_step=8;partial=1000;
 assert(sam_save()==-1);fail_step=-1;
 assert(!memcmp(flash,snapshot,0x8000));sam_init(clip);assert(sam_restore() && sam_rate()==8000);
 assert(!sam_save());assert(clip_current()->magic==CLIP_MAGIC);
 assert(!memcmp(flash,snapshot,0x8000));
 puts("sample slot: exact audio/settings restore, replacement, bounded banks, erase/payload/commit cuts, corrupt fallback and sequence wrap pass");
}
