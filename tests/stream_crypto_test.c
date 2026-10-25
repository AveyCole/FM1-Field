/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../app/net/stream_crypto.h"
static unsigned yields;
void br_fm1_yield(void) { yields++; }
static unsigned char a[16401], b[16401], original[16401];
int main(void)
{
    unsigned char key[32], iv[12], h[16], ya[16], yb[16], ta[16], tb[16];
    const unsigned lengths[] = {0,1,15,16,17,31,32,33,511,512,513,1024,16384,16401};
    unsigned i, k, bits;
    for(i=0;i<sizeof key;i++) key[i]=(unsigned char)(i*13+7);
    for(i=0;i<sizeof iv;i++) iv[i]=(unsigned char)(i*17);
    for(i=0;i<sizeof h;i++) h[i]=(unsigned char)(i*19);
    for(i=0;i<sizeof a;i++) original[i]=(unsigned char)(i*31);
    for(bits=16;bits<=32;bits+=8) for(k=0;k<sizeof lengths/sizeof lengths[0];k++) {
        unsigned n=lengths[k];
        br_aes_small_ctr_keys old;
        br_aes_ct_ctr_keys next;
        br_gcm_context ga,gb;
        memcpy(a,original,n);memcpy(b,a,n);
        br_aes_small_ctr_init(&old,key,bits);
        stream_ctr_init(&next.vtable,key,bits);
        yields=0;
        assert(br_aes_small_ctr_run(&old,iv,42,a,n)==stream_ctr_run(&next.vtable,iv,42,b,n));
        assert(!memcmp(a,b,n));
        assert(yields==(n+511)/512);
        memset(ya,0,16);memset(yb,0,16);yields=0;
        br_ghash_ctmul32(ya,h,a,n);stream_ghash(yb,h,a,n);
        assert(!memcmp(ya,yb,16));assert(yields==(n+511)/512);
        memcpy(a,original,n);memcpy(b,a,n);
        br_gcm_init(&ga,&old.vtable,br_ghash_ctmul32);
        br_gcm_init(&gb,&next.vtable,stream_ghash);
        br_gcm_reset(&ga,iv,12);br_gcm_reset(&gb,iv,12);
        br_gcm_aad_inject(&ga,key,13);br_gcm_aad_inject(&gb,key,13);
        br_gcm_flip(&ga);br_gcm_flip(&gb);
        br_gcm_run(&ga,1,a,n);br_gcm_run(&gb,1,b,n);
        br_gcm_get_tag(&ga,ta);br_gcm_get_tag(&gb,tb);
        assert(!memcmp(a,b,n));assert(!memcmp(ta,tb,16));
        br_gcm_reset(&gb,iv,12);br_gcm_aad_inject(&gb,key,13);br_gcm_flip(&gb);
        br_gcm_run(&gb,0,b,n);assert(br_gcm_check_tag(&gb,ta));assert(!memcmp(b,original,n));
        ta[0]^=1;
        br_gcm_reset(&gb,iv,12);br_gcm_aad_inject(&gb,key,13);br_gcm_flip(&gb);
        br_gcm_run(&gb,0,a,n);assert(!br_gcm_check_tag(&gb,ta));
    }
    puts("AES-128/192/256 CTR + GHASH + GCM match original; partial/full TLS records authenticate; tampered tags rejected; yields every 512 bytes");
    return 0;
}
