/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "fastmath.h"
#define FX_HIST 6144u
static int16_t guarded[FX_HIST+32],*fx_hist=guarded+16;
static volatile uint32_t fx_beat=22050;
#include "../app/field_space.c"
static void reset(void){memset(guarded,0x5A,sizeof guarded);memset(fx_hist,0,FX_HIST*2);space_reset();space_on=1;space_mix=127;}
int main(void)
{
 double early=0,late=0,stereo=0;int first=-1;
 space_time=64;space_sync=1;space_feedback=100;space_room=100;reset();
 for(unsigned i=0;i<44100*5;i++) {
  float l=i==0?.8f:0,r=l;space_process(&l,&r);
  if(first<0 && fabsf(space.echo_lp)>0.0001f) first=(int)i;
  if(i>1000 && i<44100) early+=l*l+r*r;
  if(i>44100*4) late+=l*l+r*r;
  stereo+=fabsf(l-r);assert(isfinite(l) && isfinite(r) && fabsf(l)<=1 && fabsf(r)<=1);
 }
 assert(first>=11020 && first<=11040);assert(early>0.001 && late<early*0.01 && stereo>.01);
 for(unsigned time=0;time<2;time++) for(unsigned feedback=0;feedback<2;feedback++)
 for(unsigned room=0;room<2;room++) for(unsigned sync=0;sync<2;sync++) {
  space_time=time*127;space_feedback=feedback*127;space_room=room*127;space_sync=sync;reset();
  for(unsigned i=0;i<44100;i++) {
   float l=i%89<44?.95f:-.95f,r=-.3f*l;space_process(&l,&r);
   assert(isfinite(l) && isfinite(r) && fabsf(l)<=1 && fabsf(r)<=1);
   assert(space.write<4096 && space.delay>=55 && space.delay<=3969);
   for(unsigned k=0;k<6;k++) assert(space.pos[k]<SPACE_LENGTH[k]);
  }
  for(unsigned i=0;i<16;i++) assert(guarded[i]==0x5A5A && guarded[FX_HIST+16+i]==0x5A5A);
 }
 space_on=0;float l,r;for(unsigned i=0;i<300;i++){l=.123f;r=-.314f;space_process(&l,&r);}assert(l==.123f && r==-.314f && space.mix==0);
 space_sync=1;space_time=64;fx_beat=22050;assert(space_frames()==2756);fx_beat=11025;assert(space_frames()==1378);
 fx_beat=66150;assert(space_frames()==3969 && space_wanted()>3969);
 puts("space: quarter-second echo, stereo diffusion, natural tail decay, exact bypass, 16 stability corners, guard bounds and tempo/limit pass");
}
