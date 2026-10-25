/* SPDX-License-Identifier: MPL-2.0 */
/* Stereo state-variable filter after the live/sample mix. No history bank.
 * Control coefficients run at 689 Hz, audio at 44.1 kHz. At the 6 kHz ceiling,
 * f<=0.83 and damping>=0.2 remain inside the Chamberlin stability region. */
static volatile uint8_t fil_on=1,fil_seen,fil_type,fil_cut=96,fil_res=24,fil_depth,fil_rate=64,fil_sync,fil_restart;
static struct {float st[2][2],phase,f,q,cut,res,wet;unsigned tick;} fil;
static const float FIL_BEATS[6]={8,4,2,1,0.5f,0.25f};
static unsigned fil_division(void) {return (unsigned)fil_rate*6u/128u;}
static float fil_hz(void) {return 80.0f*fm_exp2f((float)fil_cut*(6.22881869f/127.0f));}
static float fil_speed(void)
{
 return fil_sync?44100.0f/((float)fx_beat*FIL_BEATS[fil_division()]):0.05f*fm_exp2f((float)fil_rate*(7.32192809f/127.0f));
}
static void fil_init(void)
{
 memset(&fil,0,sizeof fil);fil_on=1;fil_seen=fil_type=fil_depth=fil_sync=fil_restart=0;
 fil_cut=96;fil_res=24;fil_rate=64;fil.cut=96;fil.res=24;
}
static void fil_process(float *l,float *r)
{
 unsigned ch;float in[2],target=fil_seen && fil_on?1.0f:0.0f;
 if(!target && fil.wet<=0) return; /* exact bypass; no filter work before first use */
 if(!fil.tick) {
  float triangle,oct,hz;
  if(fil_restart) {fil.phase=0;fil_restart=0;}
  triangle=1.0f-4.0f*fm_fabsf(fil.phase-0.5f);
  fil.cut+=0.1f*((float)fil_cut-fil.cut);fil.res+=0.1f*((float)fil_res-fil.res);
  oct=fm_clampf(fil.cut*(6.22881869f/127.0f)+triangle*(float)fil_depth*(3.0f/127.0f),0,6.22881869f);
  hz=80.0f*fm_exp2f(oct);fil.f=2.0f*fm_sinf(FM_PI*hz/44100.0f);
  fil.q=1.4f-fil.res*(1.2f/127.0f);
  fil.phase+=fil_speed()*(64.0f/44100.0f);
  if(fil.phase>=1) fil.phase-=1;
 }
 fil.tick=(fil.tick+1u)%64u;
 fil.wet=fm_clampf(fil.wet+(target?1.0f:-1.0f)/220.0f,0,1);
 in[0]=*l;in[1]=*r;
 for(ch=0;ch<2;ch++) {
  float *s=fil.st[ch],lo=s[0]+fil.f*s[1],hi=in[ch]-lo-fil.q*s[1],bd=s[1]+fil.f*hi;
  float out=fil_type==0?lo:fil_type==1?hi:bd*fil.q;
  s[0]=lo;s[1]=bd;
  in[ch]=fm_clampf(in[ch]+fil.wet*(out-in[ch]),-1,1);
 }
 *l=in[0];*r=in[1];
 if(!target && fil.wet<=0) memset(fil.st,0,sizeof fil.st);
}
