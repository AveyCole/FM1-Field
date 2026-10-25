/* SPDX-License-Identifier: MPL-2.0 */
/* One 2.728 s, 8 kHz mono IMA ADPCM clip in the former 12 KiB FX buffer.
 * Independent 64-sample blocks support pitched playback with a tiny cache. */
#define SAM_BYTES 12288u
#define SAM_BLOCK 36u
#define SAM_FRAMES ((SAM_BYTES/SAM_BLOCK)*64u)
#define SAM_RATE 8000u
enum {SAM_EMPTY,SAM_CAPTURE,SAM_READY};
static uint8_t *sam_data;
static volatile uint8_t sam_active,sam_state,sam_voice,sam_gate;
static volatile uint32_t sam_len;
static uint8_t sam_start,sam_end=127,sam_level=110,sam_loop,sam_div,sam_slice;
static int8_t sam_pitch,sam_oct;
static int sam_pred,sam_index;
static uint32_t sam_tick,sam_cached=0xFFFFFFFFu;
static int16_t sam_cache[64];
static float sam_sum,sam_pos,sam_step,sam_env;
static const int16_t sam_steps[89]={
 7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,
 88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,
 544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,
 2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,
 9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767
};
static int sam_decode(unsigned nibble,int *pred,int *index)
{
 static const int8_t adjust[8]={-1,-1,-1,-1,2,4,6,8};
 int step=sam_steps[*index],delta=step>>3;
 if(nibble&1u) delta+=step>>2;
 if(nibble&2u) delta+=step>>1;
 if(nibble&4u) delta+=step;
 *pred+=nibble&8u?-delta:delta;
 if(*pred>32767) *pred=32767;
 if(*pred < -32768) *pred=-32768;
 *index+=adjust[nibble&7u];
 if(*index<0) *index=0;
 if(*index>88) *index=88;
 return *pred;
}
static unsigned sam_encode(int v)
{
 int delta=v-sam_pred,step=sam_steps[sam_index];unsigned code=0;
 if(delta<0) {code=8;delta=-delta;}
 if(delta>=step) {code|=4;delta-=step;}
 if(delta>=step/2) {code|=2;delta-=step/2;}
 if(delta>=step/4) code|=1;
 sam_decode(code,&sam_pred,&sam_index);return code;
}
static int sam_sample(unsigned pos)
{
 unsigned block=pos/64u,i;const uint8_t *b;
 if(block!=sam_cached) {
  int pred,index;
  b=sam_data+block*SAM_BLOCK;
  pred=(int16_t)(b[0]|(unsigned)b[1]<<8);index=b[2];
  for(i=0;i<64;i++) sam_cache[i]=(int16_t)sam_decode((b[4+i/2]>>(4*(i&1u)))&15u,&pred,&index);
  sam_cached=block;
 }
 return sam_cache[pos%64u];
}
static void sam_init(void *buffer)
{
 sam_data=buffer;sam_state=SAM_EMPTY;sam_len=0;sam_voice=sam_gate=0;sam_active=0;
}
static void sam_enter(void) {sam_active=1;}
static void sam_finish(void)
{
 if(sam_state==SAM_CAPTURE) sam_state=sam_len>=64?SAM_READY:SAM_EMPTY;
}
static void sam_exit(void)
{
 if(sam_loop!=2) sam_voice=sam_gate=0;
 sam_active=0;
 sam_finish();
}
static void sam_record(void)
{
 sam_voice=sam_gate=0;sam_env=0;
 if(sam_state==SAM_CAPTURE) {sam_state=sam_len>=64?SAM_READY:SAM_EMPTY;return;}
 sam_state=SAM_EMPTY;sam_len=0;sam_div=0;sam_sum=0;sam_tick=0;sam_pred=sam_index=0;
 sam_start=0;sam_end=127;sam_cached=0xFFFFFFFFu;
 sam_state=SAM_CAPTURE;
}
/* Buffering does not advance capture. Average 5/6 real decoded frames per
 * 8 kHz sample, before the keyboard audio is mixed into the stream. */
static void sam_capture(float l,float r,int fresh)
{
 unsigned pos,at,code;uint8_t *b;
 if(sam_state!=SAM_CAPTURE || !fresh) return;
 sam_sum+=(l+r)*0.5f;sam_div++;sam_tick+=SAM_RATE;
 if(sam_tick<44100u) return;
 sam_tick-=44100u;
 pos=(unsigned)sam_len;at=pos%64u;b=sam_data+(pos/64u)*SAM_BLOCK;
 if(!at) {b[0]=(uint8_t)sam_pred;b[1]=(uint8_t)((unsigned)sam_pred>>8);b[2]=(uint8_t)sam_index;b[3]=0;}
 code=sam_encode((int)(sam_sum*(32767.0f/(float)sam_div)));sam_sum=0;sam_div=0;
 if(at&1u) b[4+at/2]|=(uint8_t)(code<<4);
 else b[4+at/2]=(uint8_t)code;
 sam_len=pos+1u;
 if(sam_len==SAM_FRAMES) sam_state=SAM_READY;
}
static unsigned sam_trim_first(void) {return (unsigned)(sam_len*sam_start/127u);}
static unsigned sam_trim_last(void)
{
 unsigned first=sam_trim_first(),last=(unsigned)(sam_len*sam_end/127u);
 if(last<first+32u) last=first+32u;
 return last>sam_len?(unsigned)sam_len:last;
}
static unsigned sam_first(void)
{
 unsigned first=sam_trim_first();
 return sam_loop==3?first+(sam_trim_last()-first)*sam_slice/27u:first;
}
static unsigned sam_last(void)
{
 unsigned first=sam_trim_first(),last=sam_trim_last();
 return sam_loop==3?first+(last-first)*(sam_slice+1u)/27u:last;
}
static void sam_note(int note)
{
 if(sam_loop==3) sam_slice=(uint8_t)(note<53?0:note>79?26:note-53);
 if(sam_state!=SAM_READY || sam_len<64 || sam_first()+2u>sam_last()) return;
 sam_voice=0;sam_pos=(float)sam_first();sam_env=0;
 sam_step=((float)SAM_RATE/44100.0f)*fm_exp2f((float)((sam_loop==3?0:note-60)+sam_pitch+12*sam_oct)/12.0f);
 sam_gate=1;sam_voice=1;
}
static float sam_next(void)
{
 unsigned first,last,i,j;float t,v,edge;
 if(!sam_voice || sam_state!=SAM_READY) return 0;
 first=sam_first();last=sam_last();
 if(first+2u>last) {sam_voice=0;return 0;}
 if(sam_pos<(float)first) sam_pos=(float)first;
 if(sam_pos>=(float)last) {
  if((sam_loop==1 || sam_loop==2) && sam_gate) sam_pos=(float)first;
  else {sam_voice=0;return 0;}
 }
 i=(unsigned)sam_pos;j=i+1u<last?i+1u:i;t=sam_pos-(float)i;
 v=(float)sam_sample(i);v+=(sam_sample(j)-v)*t;
 sam_env+=(sam_gate?1.0f:-1.0f)/220.0f;
 if(sam_env>1) sam_env=1;
 if(sam_env<=0) {sam_env=0;sam_voice=0;return 0;}
 edge=fm_minf(1.0f,fm_minf(sam_pos-(float)first,(float)last-1.0f-sam_pos)/24.0f);
 sam_pos+=sam_step;
 return v*(1.0f/32768.0f)*sam_env*fm_maxf(edge,0)*(float)sam_level/127.0f;
}
