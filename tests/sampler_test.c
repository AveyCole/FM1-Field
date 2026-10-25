/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fastmath.h"
#include "../app/sampler.c"
int main(void)
{
    uint8_t guarded[SAM_BYTES+32];double signal=0,error=0;unsigned i;
    memset(guarded,0xA5,sizeof guarded);sam_init(guarded+16);sam_enter();sam_record();
    for(i=0;i<44100;i++) sam_capture(0.5f,0.5f,0);
    assert(sam_len==0); /* stalls cannot create a silent sample */
    for(i=0;i<44100*3;i++) {
        float v=(float)(0.45*sin(2*3.141592653589793*220*i/44100));
        sam_capture(v,v,1);
    }
    assert(sam_state==SAM_READY && sam_len==SAM_FRAMES);
    for(i=0;i<16;i++) assert(guarded[i]==0xA5 && guarded[SAM_BYTES+16+i]==0xA5);
    /* Known 220 Hz input survives decimation and ADPCM with useful fidelity. */
    for(i=128;i<SAM_FRAMES;i++) {
        double expected=0.45*32767*sin(2*3.141592653589793*220*(i+0.5)/8000);
        double actual=sam_sample(i),d=expected-actual;
        signal+=expected*expected;error+=d*d;
    }
    assert(10*log10(signal/error)>18);
    sam_note(60);for(i=0;i<1000;i++) {float v=sam_next();assert(isfinite(v) && fabsf(v)<=1);}
    assert(sam_voice);
    sam_exit();assert(sam_state==SAM_READY && sam_len==SAM_FRAMES);
    sam_note(72);for(i=0;i<1000;i++) sam_next();assert(sam_voice); /* main-page playback */
    sam_gate=0;for(i=0;i<300;i++) sam_next();assert(!sam_voice);
    sam_loop=3;sam_pitch=sam_oct=0;
    unsigned end=0;
    for(i=0;i<27;i++) {
        sam_note(53+(int)i);
        assert(sam_first()==end);end=sam_last();
        assert(sam_last()>sam_first());
        for(unsigned j=0;j<6000;j++) assert(isfinite(sam_next()));
        assert(!sam_voice); /* slices never loop and stay within their range */
    }
    assert(end==SAM_FRAMES);
    sam_start=32;sam_end=110;end=sam_trim_first();
    for(i=0;i<27;i++) {sam_note(53+(int)i);assert(sam_first()==end);end=sam_last();}
    assert(end==sam_trim_last());sam_start=0;sam_end=127;sam_loop=0;
    sam_enter();sam_record();for(i=0;i<4410;i++) sam_capture(0.1f,0.1f,1);
    sam_record();assert(sam_state==SAM_READY && sam_len==800);
    sam_start=125;sam_end=126;sam_note(60);
    for(i=0;i<10000;i++) assert(isfinite(sam_next()));
    sam_init(guarded+16);assert(!sam_len && sam_state==SAM_EMPTY);
    printf("sampler: %.1f dB SNR, full/partial capture, guards, stall, trim and main-page replay passed\n",10*log10(signal/error));
}
