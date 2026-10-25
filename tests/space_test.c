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
static void reset(void){space_data=fx_hist;memset(guarded,0x5A,sizeof guarded);memset(fx_hist,0,FX_HIST*2);space_reset();space_on=1;space_mix=127;}
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
 space_sync=1;space_time=64;fx_beat=22050;assert(space_frames()==1378);fx_beat=11025;assert(space_frames()==689);
 fx_beat=66150;assert(space_frames()==3969 && space_wanted()>3969);
 /* Musical note lengths at 120 BPM: eighth=250 ms, quarter=500 ms,
  * quarter triplet=333 ms, dotted quarter=750 ms exceeds 720 ms. */
 fx_beat=22050;space_time=64;assert(space_division()==5 && space_frames()==1378);
 space_time=94;assert(space_division()==8 && space_frames()==2756);
 space_time=82;assert(space_division()==7 && space_frames()==1837);
 space_time=105;assert(space_division()==9 && space_frames()==3969 && space_wanted()==4134);
 for(unsigned i=0;i<11;i++) {space_time=(i*128u+10u)/11u;assert(space_division()==i);assert(space_frames()>=55 && space_frames()<=3969);}
 space_time=64;space_mix=127;space_room_mix=127;space_active=1;space_live=0;reset();
 for(unsigned i=0;i<44100;i++) {
  float dl=i%89<44?.2f:-.2f,dr=-.3f*dl;l=dl;r=dr;space_route(&l,&r,0);assert(l==dl && r==dr);
 }
 /* The sampler still enters the effect with a dry live return. */
 double tail=0;reset();
 for(unsigned i=0;i<44100;i++) {l=.1f;r=-.1f;space_route(&l,&r,i==0?.7f:0);if(i>1000) tail+=fabsf(l-.1f)+fabsf(r+.1f);}
 assert(tail>.01);
 space_live=1;reset();tail=0;
 for(unsigned i=0;i<44100;i++) {l=i==0?.7f:0;r=l;space_route(&l,&r,0);if(i>1000) tail+=fabsf(l)+fabsf(r);}
 assert(tail>.01);
 /* Separate delay/reverb levels: delay-only is mono; width=0 centers room. */
 space_room_mix=0;space_mix=127;reset();
 for(unsigned i=0;i<44100;i++){l=i==0?.7f:0;r=l;space_process(&l,&r);assert(l==r);}
 space_room_mix=127;space_mix=0;space_width=0;reset();tail=0;
 for(unsigned i=0;i<44100;i++){l=i==0?.7f:0;r=l;space_process(&l,&r);assert(l==r);if(i>1000)tail+=fabsf(l);}
 assert(tail>.01);
 for(unsigned tone=0;tone<2;tone++) for(unsigned damp=0;damp<2;damp++) for(unsigned width=0;width<2;width++) {
  space_tone=tone*127;space_damping=damp*127;space_width=width*127;
  space_feedback=space_room=space_room_mix=space_mix=127;reset();
  for(unsigned i=0;i<44100*3;i++) {
   l=i%89<44?.95f:-.95f;r=-.3f*l;space_process(&l,&r);
   assert(isfinite(l) && isfinite(r) && fabsf(l)<=1 && fabsf(r)<=1);
  }
  for(unsigned i=0;i<16;i++) assert(guarded[i]==0x5A5A && guarded[FX_HIST+16+i]==0x5A5A);
 }
 puts("space: timing/divisions/limit, independent levels, dry-live/sample-wet routing, natural tails, exact bypass, 24 stability corners and guards pass");
}
