/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fastmath.h"
#include "../app/sampler.c"
static uint8_t guarded[SAM_BYTES+32];
int main(void)
{
 for(unsigned q=0;q<3;q++) {
  unsigned rate=8000u>>q,i,frames=(SAM_FRAMES*44100u+rate-1u)/rate;
  memset(guarded,0x5A,sizeof guarded);sam_init(guarded+16);
  sam_quality=(uint8_t)q;sam_pitch=sam_oct=0;sam_start=0;sam_end=127;sam_loop=0;sam_record();
  assert(sam_rate()==rate);
  for(i=0;i<44100;i++) sam_capture(.2f,.2f,0);
  assert(!sam_len);
  for(i=0;i<frames+1;i++) {float v=.4f*sinf(6.28318530718f*220*(float)i/44100);sam_capture(v,v,1);}
  assert(sam_state==SAM_READY && sam_len==SAM_FRAMES);
  double energy=0,error=0;
  for(i=128;i<SAM_FRAMES;i++) {
   double expected=.4*32767*sin(6.28318530718*220*(i+.5)/rate),delta=expected-sam_sample(i);
   energy+=expected*expected;error+=delta*delta;
  }
  assert(10*log10(energy/error)>14);
  sam_quality=(uint8_t)((q+1u)%3u); /* next capture choice cannot retune this clip */
  sam_note(60);assert(fabsf(sam_step*44100-(float)rate)<.001f);
  for(i=0;sam_voice && i<frames+3;i++) {float v=sam_next();assert(isfinite(v) && fabsf(v)<=1);}
  assert(!sam_voice && i>=frames && i<=frames+2);
  sam_layer=1;sam_record();assert(sam_dubbing && sam_rate()==rate && sam_len==SAM_FRAMES);
  for(i=0;i<frames+1;i++) sam_capture(.1f,.1f,1);
  assert(!sam_dubbing && sam_state==SAM_READY && sam_len==SAM_FRAMES && sam_rate()==rate);
  for(i=0;i<16;i++) assert(guarded[i]==0x5A && guarded[SAM_BYTES+16+i]==0x5A);
  printf("lofi: %u Hz, %u ms, %.1f dB SNR, correct-pitch/duration playback and same-rate overdub pass\n",rate,SAM_FRAMES*1000u/rate,10*log10(energy/error));
 }
}
