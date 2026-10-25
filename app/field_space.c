/* SPDX-License-Identifier: MPL-2.0 */
/* A compact warm echo + stereo Schroeder room. Owns a separate 12 KiB
 * bank; 4096 echo samples at 5.5125 kHz and a room at 11.025 kHz.
 * The live stereo dry path stays at 44.1 kHz. No allocation in the renderer. */
static volatile uint8_t space_active,space_on=1,space_time=64,space_feedback=48,space_room=70,space_mix=45,space_sync=1;
static uint8_t space_tone=64,space_damping=80,space_width=100,space_room_mix=45,space_live=1,space_page;
static int16_t *space_data;
static const uint16_t SPACE_LENGTH[6]={293,337,421,463,113,173};
static const uint8_t SPACE_NUM[]={1,1,1,3,1,1,3,2,1,3,2};
static const uint8_t SPACE_DEN[]={8,6,4,8,3,2,4,3,1,2,1};
static const char *const SPACE_NOTE[]={"1/32","1/16T","1/16","1/16D","1/8T","1/8","1/8D","1/4T","1/4","1/4D","1/2"};
static struct {
 float sum,dc,delay,echo_sum,echo_lp,damp[4],wet[2],smooth[2],mix;
 uint16_t pos[6],write;
 uint8_t sub,echo_sub;
} space;
typedef char space_fits[(4096+293+337+421+463+113+173<=FX_HIST)?1:-1];
static unsigned space_division(void) {return (unsigned)space_time*11u/128u;}
static unsigned space_wanted(void)
{
 unsigned d=space_division();
 return space_sync?fx_beat*SPACE_NUM[d]/(8u*SPACE_DEN[d]):110u+(unsigned)space_time*3859u/127u;
}
static unsigned space_frames(void) {unsigned n=space_wanted();return n>3969u?3969u:n<55u?55u:n;}
static void space_reset(void) {memset(&space,0,sizeof space);space.delay=(float)space_frames();}
static void space_process(float *l,float *r)
{
 float target=space_on?1.0f:0.0f;
 space.sum+=(*l+*r)*0.125f;
 if(++space.sub==4) {
  float input=space.sum,echo,v[4],p,t;unsigned i,k,off=4096;
  space.sum=0;space.sub=0;
  space.dc+=0.002f*(input-space.dc);input-=space.dc;
  space.echo_sum+=input*0.5f;
  if(++space.echo_sub==2) {
   space.echo_sub=0;
   space.delay+=0.004f*((float)space_frames()-space.delay);
   p=(float)space.write-space.delay;if(p<0) p+=4096.0f;
   i=(unsigned)p;t=p-(float)i;
   echo=((float)space_data[i]+((float)space_data[(i+1u)&4095u]-space_data[i])*t)/32768.0f;
   space.echo_lp+=(0.05f+(float)space_tone*(0.85f/127.0f))*(echo-space.echo_lp);
   space_data[space.write]=(int16_t)(32767.0f*fm_clampf(space.echo_sum*0.65f+space.echo_lp*(float)space_feedback*(0.95f/127.0f),-0.95f,0.95f));
   space.write=(space.write+1u)&4095u;space.echo_sum=0;
  }
  echo=space.echo_lp;
  for(k=0;k<4;k++) {
   unsigned at=off+space.pos[k];v[k]=(float)space_data[at]/32768.0f;
   space.damp[k]+=(0.9f-(float)space_damping*(0.85f/127.0f))*(v[k]-space.damp[k]);
   space_data[at]=(int16_t)(32767.0f*fm_clampf(input*0.22f+echo*0.16f+space.damp[k]*(0.55f+(float)space_room*(0.33f/127.0f)),-0.95f,0.95f));
   if(++space.pos[k]>=SPACE_LENGTH[k]) space.pos[k]=0;
   off+=SPACE_LENGTH[k];
  }
  for(k=0;k<2;k++) {
   unsigned at=off+space.pos[k+4];float x=(v[k]+v[k+2])*0.3f,b=(float)space_data[at]/32768.0f,y=b-0.5f*x;
   space_data[at]=(int16_t)(32767.0f*fm_clampf(x+0.5f*y,-0.95f,0.95f));
   if(++space.pos[k+4]>=SPACE_LENGTH[k+4]) space.pos[k+4]=0;
   off+=SPACE_LENGTH[k+4];space.wet[k]=y;
  }
  p=(space.wet[0]+space.wet[1])*0.5f;
  for(k=0;k<2;k++) space.wet[k]=echo*(float)space_mix*(0.55f/127.0f)+(p+(space.wet[k]-p)*(float)space_width/127.0f)*(float)space_room_mix*(0.65f/127.0f);
 }
 space.mix+=fm_clampf(target-space.mix,-1.0f/220.0f,1.0f/220.0f);
 for(unsigned k=0;k<2;k++) space.smooth[k]+=0.3f*(space.wet[k]-space.smooth[k]);
 *l=fm_clampf(*l+space.mix*space.smooth[0],-1,1);*r=fm_clampf(*r+space.mix*space.smooth[1],-1,1);
}
static void space_route(float *l,float *r,float sample)
{
 float dl=0,dr=0;
 if(space_active && !space_live) {dl=*l;dr=*r;*l=*r=0;}
 *l=fm_clampf(*l+sample,-1,1);*r=fm_clampf(*r+sample,-1,1);
 if(space_active) space_process(l,r);
 *l=fm_clampf(*l+dl,-1,1);*r=fm_clampf(*r+dr,-1,1);
}
