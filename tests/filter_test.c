/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fastmath.h"
static uint32_t fx_beat=22050;
#include "../app/field_filter.c"
static double response(unsigned type,double hz)
{
 double sum=0;fil_init();fil_seen=1;fil_type=type;fil_cut=74;fil.cut=74;
 for(unsigned i=0;i<22050;i++) {
  float l=(float)(0.1*sin(2*3.141592653589793*hz*i/44100)),r=0;
  fil_process(&l,&r);assert(r==0 && isfinite(l));
  if(i>=4410) sum+=l*l;
 }
 return sqrt(sum/17640.0)*sqrt(2.0)/0.1;
}
int main(void)
{
 fil_init();float l=0.314159f,r=-0.2f;fil_process(&l,&r);assert(l==0.314159f && r==-0.2f && fil.tick==0);
 double lpLow=response(0,150),lpHigh=response(0,6000),hpLow=response(1,150),hpHigh=response(1,6000);
 assert(lpLow>.8 && lpHigh<.1 && hpLow<.08 && hpHigh>.8);
 assert(response(2,1000)>.6 && response(2,150)<.3 && response(2,6000)<.3);
 for(unsigned type=0;type<3;type++) for(unsigned c=0;c<2;c++) for(unsigned res=0;res<2;res++)
 for(unsigned depth=0;depth<2;depth++) for(unsigned sync=0;sync<2;sync++) for(unsigned rate=0;rate<2;rate++) {
  fil_init();fil_seen=1;fil_type=type;fil_cut=c*127;fil_res=res*127;fil_depth=depth*127;fil_sync=sync;fil_rate=rate*127;
  fx_beat=11025; /* 240 BPM, fastest synced modulation */
  for(unsigned i=0;i<44100;i++) {
   l=(i%83<41?0.7f:-0.7f);r=-l;fil_process(&l,&r);
   assert(isfinite(l) && isfinite(r) && fabsf(l)<=1 && fabsf(r)<=1);
   for(unsigned ch=0;ch<2;ch++) for(unsigned s=0;s<2;s++) assert(isfinite(fil.st[ch][s]) && fabsf(fil.st[ch][s])<64);
   assert(fil.phase>=0 && fil.phase<1);
  }
 }
 fil_on=0;for(unsigned i=0;i<300;i++){l=.2f;r=-.3f;fil_process(&l,&r);}
 assert(l==.2f && r==-.3f && fil.wet==0);
 fil_sync=1;fil_rate=64;fx_beat=22050;assert(fabsf(fil_speed()-2)<.0001f);fx_beat=33075;assert(fabsf(fil_speed()-4.0f/3)<.0001f);
 printf("filter: LP %.3f/%.3f, HP %.3f/%.3f; band response, stereo isolation, exact bypass, 96 stability corners and tempo sync pass\n",lpLow,lpHigh,hpLow,hpHigh);
}
