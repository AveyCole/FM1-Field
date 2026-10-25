/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fastmath.h"
#include "../app/sampler.c"
#include "../app/field_gain.c"
static uint8_t clip[SAM_BYTES];
int main(void)
{
 sam_init(clip);sam_enter();sam_live_mix=64;
 for(unsigned i=0;i<300;i++) {float l=.4f,r=-.2f;sam_monitor(&l,&r);if(i==299){assert(fabsf(l-.4f*64/127)<.00001f);assert(fabsf(r+.2f*64/127)<.00001f);}}
 sam_muted=1;for(unsigned i=0;i<300;i++){float l=.4f,r=-.2f;sam_monitor(&l,&r);if(i==299)assert(l==0 && r==0);}
 sam_muted=0;sam_exit();for(unsigned i=0;i<300;i++){float l=.4f,r=-.2f;sam_monitor(&l,&r);if(i==299)assert(l==.4f && r==-.2f);}
 sam_record();for(unsigned i=0;i<4410;i++)sam_capture(.2f,.2f,1);sam_finish();sam_loop=2;sam_level=127;sam_note(60);
 float sample=0;for(unsigned i=0;i<1000;i++)sample=sam_next();assert(sample>.39f && sample<.41f);
 for(unsigned i=0;i<100000;i++) {
  float l=(i%97u<48?.8f:-.8f)*4,r=l*.3f;field_limit(&l,&r,&field_mix_gain);
  assert(fabsf(l)<=.980001f && fabsf(r)<=.980001f && fabsf(r-l*.3f)<.00001f);
  l*=1.5f;r*=1.5f;field_limit(&l,&r,&field_output_gain);assert(fabsf(l)<=.980001f && fabsf(r)<=.980001f);
 }
 for(unsigned i=0;i<44100;i++){float l=0,r=0;field_limit(&l,&r,&field_output_gain);}assert(field_output_gain>.999f);
 puts("gain/mix: continuous live mix, independent mute, main-page restore, 200% sample level and stereo-linked bounded boost pass");
}
