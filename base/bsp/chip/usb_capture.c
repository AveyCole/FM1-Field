/* SPDX-License-Identifier: MPL-2.0 */
/* App-owned route for EP4 OUT. Same fill-steered interpolation as uac_play,
 * read before the app's sampler, filter and MASTER. No extra audio block. */
static void uac_capture_begin(uint32_t n)
{
 uint32_t r=uo_r,fill=uo_w-r;float adj;
 uo.step=0;
 if(!uo_ring || !uo.alt || !usb.config) {uo.go=0;return;}
 if(!uo.go) {
  if(fill<UO_TGT) return;
  r=uo_w-UO_TGT;uo.pos=0;uo.err=0;uo.go=1;fill=UO_TGT;
 }
 uo.err+=((float)fill-(float)UO_TGT-uo.err)*0.05f;
 adj=uo.err*2.0f;adj=adj>400?400:adj < -400?-400:adj;
 uo.step=(uint32_t)(65510.0f+adj);
 if(fill<(uint32_t)(((uint64_t)n*uo.step+uo.pos)>>16)+2u) {
  uo.underruns++;uo.go=0;uo.step=0;RING_PUBLISH();uo_r=uo_w;return;
 }
 RING_PUBLISH();uo_r=r;
}
static int uac_capture_read(float *l,float *r)
{
 uint32_t at=uo_r,pos=uo.pos,a,b;float fr;
 *l=*r=0;
 if(!uo.step || !uo.alt || !usb.config) return 0;
 a=uo_ring[at&(UO_N-1u)];b=uo_ring[(at+1u)&(UO_N-1u)];
 fr=(float)pos*(1.0f/65536.0f);
 *l=((float)(int16_t)a+((float)(int16_t)b-(float)(int16_t)a)*fr)*(1.0f/32768.0f);
 *r=((float)(int16_t)(a>>16)+((float)(int16_t)(b>>16)-(float)(int16_t)(a>>16))*fr)*(1.0f/32768.0f);
 pos+=uo.step;uo.pos=pos&65535u;
 RING_PUBLISH();uo_r=at+(pos>>16);return 1;
}
