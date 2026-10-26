/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fastmath.h"
#include "../app/sampler.c"
static uint8_t guarded[SAM_BYTES+32],before[SAM_BYTES];
static int16_t original[SAM_FRAMES];
static float tone(unsigned i,unsigned hz,float amp) {return amp*sinf(6.28318530718f*(float)hz*(float)i/44100.0f);}
int main(void)
{
 unsigned i;double energy=0,error=0,monitor=0;
 memset(guarded,0xA5,sizeof guarded);sam_init(guarded+16);sam_record();
 for(i=0;i<44100*6;i++) {float v=tone(i,220,.4f);sam_capture(v,v,1);}
 assert(sam_state==SAM_READY && sam_len==SAM_FRAMES);
 for(i=0;i<SAM_FRAMES;i++) original[i]=(int16_t)sam_sample(i);
 memcpy(before,sam_data,SAM_BYTES);
 sam_layer=1;sam_start=20;sam_end=110;sam_pitch=3;sam_loop=3;sam_slices=8;sam_record();
 assert(sam_dubbing && sam_len==SAM_FRAMES && !sam_voice);
 for(i=0;i<44100;i++) {sam_capture(.2f,.2f,0);assert(sam_next()==0);}
 assert(sam_dub_pos==0 && !memcmp(before,sam_data,SAM_BYTES));
 for(i=0;i<44100*6;i++) {float v=tone(i,440,.3f);sam_capture(v,v,1);float out=sam_next();assert(isfinite(out) && fabsf(out)<=1);monitor+=fabsf(out);}
 assert(sam_state==SAM_READY && !sam_dubbing && sam_len==SAM_FRAMES && monitor>100);
 assert(sam_start==20 && sam_end==110 && sam_pitch==3 && sam_loop==3 && sam_slices==8);
 for(i=128;i<SAM_FRAMES;i++) {
  double expected=.5*original[i]+.15*32767*sin(6.28318530718*440*(i+.5)/8000);
  double delta=expected-sam_sample(i);energy+=expected*expected;error+=delta*delta;
 }
 assert(10*log10(energy/error)>18);
 /* A stopped layer changes only completed blocks, keeping the old tail exact. */
 memcpy(before,sam_data,SAM_BYTES);sam_record();
 for(i=0;i<1000;i++) sam_capture(-.2f,-.2f,1);
 unsigned committed=sam_dub_pos/64u*SAM_BLOCK;
 assert(committed && sam_dub_pos%64u);sam_record();
 assert(sam_state==SAM_READY && !sam_dubbing && sam_len==SAM_FRAMES);
 assert(memcmp(before,sam_data,committed));
 assert(!memcmp(before+committed,sam_data+committed,SAM_BYTES-committed));
 /* Exit before a block completes leaves the entire clip unchanged. */
 memcpy(before,sam_data,SAM_BYTES);sam_record();
 for(i=0;i<100;i++) sam_capture(.8f,.8f,1);
 sam_exit();assert(sam_state==SAM_READY && !memcmp(before,sam_data,SAM_BYTES));
 /* Short, non-block-aligned clips retain their exact duration. */
 sam_layer=0;sam_record();for(i=0;i<1000;i++) sam_capture(.1f,.1f,1);sam_finish();
 unsigned length=sam_len;assert(length==181);sam_layer=1;sam_record();
 for(i=0;i<2000;i++) sam_capture(-.1f,-.1f,1);
 assert(sam_state==SAM_READY && sam_len==length && !sam_dubbing);
 sam_cached=0xFFFFFFFFu;for(i=64;i<length;i++) assert(abs(sam_sample(i))<1000);
 for(i=0;i<16;i++) assert(guarded[i]==0xA5 && guarded[SAM_BYTES+16+i]==0xA5);
 sam_init(guarded+16);sam_layer=1;sam_record();assert(!sam_dubbing);
 for(i=0;i<4410;i++) sam_capture(.2f,.2f,1);
 sam_finish();assert(sam_len==800 && sam_state==SAM_READY);
 sam_layer=0;sam_record();assert(sam_len==0 && !sam_dubbing);
 /* Live mix controls overdub strength, independently of monitor mute.
  * Zero is a byte-exact no-op, including partial final blocks. */
 for(unsigned q=0;q<3;q++) {
  sam_init(guarded+16);sam_record();while(sam_len<1001)sam_capture(.2f,.2f,1);sam_finish();
  unsigned count=sam_len;memcpy(before,sam_data,SAM_BYTES);
  sam_live_mix=(uint8_t)(q==0?0:q==1?64:127);sam_muted=1;sam_layer=1;sam_record();
  while(sam_state==SAM_CAPTURE)sam_capture(-.2f,-.2f,1);
  assert(sam_len==count);
  if(!q)assert(!memcmp(before,sam_data,SAM_BYTES));
  float expected=.2f*(1.0f-(float)sam_live_mix/127.0f);
  for(unsigned j=128;j<count;j++)assert(fabsf((float)sam_sample(j)/32768.0f-expected)<.006f);
 }
 /* Live mix can change within an overdub block, without losing the change. */
 sam_init(guarded+16);sam_record();while(sam_len<1001)sam_capture(.2f,.2f,1);sam_finish();
 memcpy(before,sam_data,SAM_BYTES);sam_layer=1;sam_live_mix=0;sam_record();
 while(sam_dub_pos<96)sam_capture(-.2f,-.2f,1);
 sam_live_mix=127;while(sam_state==SAM_CAPTURE)sam_capture(-.2f,-.2f,1);
 assert(!memcmp(before,sam_data,SAM_BLOCK));assert(memcmp(before+SAM_BLOCK,sam_data+SAM_BLOCK,SAM_BLOCK));
 puts("overdub mix: zero preserves clip bytes; intermediate/full blend, independent mute and mid-block knob changes pass");
 printf("layer: %.1f dB two-tone blend SNR, old-clip monitor, stall pause, preserved length/settings/tail, short clips, guards and replace fallback pass\n",10*log10(energy/error));
}
