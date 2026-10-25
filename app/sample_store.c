/* SPDX-License-Identifier: MPL-2.0 */
/* One logical clip, two 16 KiB banks: erase/program inactive, commit last.
 * The FIELD region catalogue is compiled into code; this former place-table
 * space is unused. Neither bank overlaps settings, favorites or OTA staging. */
#define CLIP_BASE 0x97000u
#define CLIP_BANK 0x4000u
#define CLIP_MAGIC 0x31504D53u
typedef struct {
 uint32_t magic,seq,len,crc;
 uint8_t start,end,level,loop,slices;
 int8_t pitch,oct;
 uint8_t reserved;
 uint32_t hcrc;
} clip_header_t;
static uint32_t clip_crc(const void *data,unsigned n)
{
 const uint8_t *p=data;uint32_t c=0xFFFFFFFFu;unsigned bit;
 while(n--) {c^=*p++;for(bit=0;bit<8;bit++) c=(c>>1)^((0u-(c&1u))&0xEDB88320u);}
 return ~c;
}
static unsigned clip_bytes(unsigned frames) {return (frames+63u)/64u*SAM_BLOCK;}
static const clip_header_t *clip_valid(unsigned bank)
{
 const uint8_t *p=plat_data(CLIP_BASE+bank*CLIP_BANK);const clip_header_t *h=(const void *)p;unsigned i;
 if(!p || h->magic!=CLIP_MAGIC || h->len<64 || h->len>SAM_FRAMES ||
    h->start>125 || h->end<=h->start || h->end>127 || h->level>127 || h->loop>3 ||
    (h->slices!=4 && h->slices!=8 && h->slices!=16 && h->slices!=27) ||
    h->pitch < -24 || h->pitch>24 || h->oct < -2 || h->oct>2 ||
    h->hcrc!=clip_crc(h,sizeof *h-4u) || h->crc!=clip_crc(p+64,clip_bytes(h->len))) return 0;
 for(i=0;i<clip_bytes(h->len);i+=SAM_BLOCK) if(p[64+i+2]>88) return 0;
 return h;
}
static const clip_header_t *clip_current(void)
{
 const clip_header_t *a=clip_valid(0),*b=clip_valid(1);
 return b && (!a || (int32_t)(b->seq-a->seq)>0)?b:a;
}
static int sam_restore(void)
{
 const clip_header_t *h=clip_current();
 if(!sam_data || !h) return 0;
 sam_voice=sam_gate=0;sam_state=SAM_EMPTY;
 memcpy(sam_data,(const uint8_t *)h+64,clip_bytes(h->len));
 sam_start=h->start;sam_end=h->end;sam_level=h->level;sam_loop=h->loop;sam_slices=h->slices;
 sam_pitch=h->pitch;sam_oct=h->oct;sam_slice=0;sam_cached=0xFFFFFFFFu;
 sam_len=h->len;sam_state=SAM_READY;return 1;
}
static int sam_save(void)
{
 const clip_header_t *old;clip_header_t h;unsigned bank,off,i,n;
 if(sam_state!=SAM_READY || !sam_data || sam_len<64) return -2;
 old=clip_current();bank=old==(const void *)plat_data(CLIP_BASE)?1u:0u;off=CLIP_BASE+bank*CLIP_BANK;
 memset(&h,0,sizeof h);h.magic=CLIP_MAGIC;h.seq=old?old->seq+1u:1u;h.len=sam_len;n=clip_bytes(h.len);
 h.start=sam_start;h.end=sam_end;h.level=sam_level;h.loop=sam_loop;h.slices=sam_slices;h.pitch=sam_pitch;h.oct=sam_oct;
 h.crc=clip_crc(sam_data,n);h.hcrc=clip_crc(&h,sizeof h-4u);
 for(i=0;i<CLIP_BANK;i+=4096u) if(plat_data_erase(off+i)) return -1;
 if(plat_data_write(off+64,sam_data,n) || memcmp(plat_data(off)+64,sam_data,n) ||
    plat_data_write(off,&h,sizeof h) || !clip_valid(bank)) return -1;
 return 0;
}
