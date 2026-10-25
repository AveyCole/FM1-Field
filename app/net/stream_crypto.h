/* SPDX-License-Identifier: MPL-2.0 */
/* TLS records can contain 16 KiB. Bound the work between cooperative yields,
 * while retaining BearSSL's authenticated AES-GCM record handling. */
#pragma once
#include "bearssl.h"

#define STREAM_CRYPTO_SLICE 512u /* a multiple of both AES and GHASH blocks */
void br_fm1_yield(void);
static const br_block_ctr_class stream_aes_ctr;

static void stream_ctr_init(const br_block_ctr_class **ctx, const void *key, size_t len)
{
    br_aes_ct_ctr_init((br_aes_ct_ctr_keys *)ctx, key, len);
    *ctx = &stream_aes_ctr;
}
static uint32_t stream_ctr_run(const br_block_ctr_class *const *ctx,
                              const void *iv, uint32_t cc, void *data, size_t len)
{
    unsigned char *p = data;
    while (len) {
        size_t n = len > STREAM_CRYPTO_SLICE ? STREAM_CRYPTO_SLICE : len;
        cc = br_aes_ct_ctr_run((const br_aes_ct_ctr_keys *)ctx, iv, cc, p, n);
        p += n; len -= n;
        br_fm1_yield();
    }
    return cc;
}
static const br_block_ctr_class stream_aes_ctr = {
    sizeof(br_aes_ct_ctr_keys), 16, 4, stream_ctr_init, stream_ctr_run
};
static void stream_ghash(void *y, const void *h, const void *data, size_t len)
{
    const unsigned char *p = data;
    while (len) {
        size_t n = len > STREAM_CRYPTO_SLICE ? STREAM_CRYPTO_SLICE : len;
        br_ghash_ctmul32(y, h, p, n);
        p += n; len -= n;
        br_fm1_yield();
    }
}
