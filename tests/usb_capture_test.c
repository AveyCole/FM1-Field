/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fastmath.h"
#define UO_N 512u
#define UO_TGT 384u
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
static uint32_t ring_data[UO_N],*uo_ring=ring_data,uo_w,uo_r;
static struct {unsigned config;} usb;
static struct {uint8_t alt,go;uint32_t step,pos;float err;unsigned underruns;} uo;
#include "../base/bsp/chip/usb_capture.c"
#include "../app/sampler.c"
static uint8_t clip[SAM_BYTES];
static unsigned sent,packets,overruns,valid;
static double signal,noise;
static void packet(void)
{
 unsigned n=44+(++packets%10==0);
 if(uo_w-uo_r+n>UO_N) {overruns++;sent+=n;return;}
 for(unsigned i=0;i<n;i++) {
  int16_t l=(int16_t)(8000*sin(sent*6.283185307179586*220/44100));
  int16_t r=(int16_t)(4000*sin(sent*6.283185307179586*660/44100));sent++;
  uo_ring[uo_w++&(UO_N-1u)]=(uint16_t)l | (uint32_t)(uint16_t)r<<16;
 }
}
int main(void)
{
 float l=1,r=1;uac_capture_begin(256);assert(!uac_capture_read(&l,&r) && l==0 && r==0);
 usb.config=uo.alt=1;uo_ring=0;uac_capture_begin(256);assert(!uac_capture_read(&l,&r));uo_ring=ring_data;sam_init(clip);sam_enter();sam_record();
 unsigned next_ms=1;
 for(unsigned block=0;block<20000;block++) {
  double time=block*256000.0/(750000.0/17.0);
  while(next_ms<=time) {packet();next_ms++;}
  uac_capture_begin(256);
  for(unsigned i=0;i<256;i++) {
   unsigned at=uo_r;double phase=uo.pos/65536.0;
   int fresh=uac_capture_read(&l,&r);sam_capture(l,r,fresh);
   if(fresh) {
    /* One continuous producer, no overruns or flushes after initial prime. */
    double ref=8000*sin((at+phase)*6.283185307179586*220/44100)/32768;
    if(block>20) {signal+=ref*ref;noise+=(l-ref)*(l-ref);}
    assert(fabs(l)<.245 && fabs(r)<.123);valid++;
   } else assert(l==0 && r==0);
  }
  assert(uo_w-uo_r<=UO_N);
 }
 assert(!overruns && !uo.underruns && valid>5100000);
 assert(sam_state==SAM_READY && sam_len==SAM_FRAMES);
 assert(10*log10(signal/noise)>60);
 /* An input stall pauses capture, rather than recording fake silence or
  * repeating a packet. Streamed zeroes, in contrast, are valid audio. */
 sam_record();unsigned before=uo.underruns;
 for(unsigned b=0;b<8;b++) {uac_capture_begin(256);for(unsigned i=0;i<256;i++){int f=uac_capture_read(&l,&r);sam_capture(l,r,f);}}
 assert(uo.underruns>before);unsigned len=sam_len;
 uac_capture_begin(256);for(unsigned i=0;i<256;i++){assert(!uac_capture_read(&l,&r));sam_capture(l,r,0);}assert(sam_len==len);
 uo_r=uo_w=0;uo.pos=0;uo.go=0;memset(uo_ring,0,sizeof ring_data);uo_w=UO_TGT;
 uac_capture_begin(256);for(unsigned i=0;i<256;i++){assert(uac_capture_read(&l,&r) && l==0 && r==0);sam_capture(l,r,1);}assert(sam_len>len);
 uo.alt=0;assert(!uac_capture_read(&l,&r));usb.config=0;uac_capture_begin(256);assert(!uo.step);
 printf("USB capture: 116 s simulated clock drift, stereo interpolation %.1f dB SNR, bounded 2 KiB ring, no over/underruns; stall pause, valid silence, disconnect and full sampler pass\n",10*log10(signal/noise));
 return 0;
}
