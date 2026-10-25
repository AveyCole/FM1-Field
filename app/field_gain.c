/* SPDX-License-Identifier: MPL-2.0 */
/* Linked stereo peak limiter: immediate attenuation, smooth ~45 ms release.
 * Makeup gain lifts quiet material without overflowing the output range. */
static float field_mix_gain=1,field_output_gain=1;
static void field_limit(float *l,float *r,float *gain)
{
 float peak=fm_maxf(fm_fabsf(*l),fm_fabsf(*r));
 float target=peak>.98f?.98f/peak:1;
 if(target<*gain) *gain=target;else *gain+=.0005f*(target-*gain);
 *l*=*gain;*r*=*gain;
}
