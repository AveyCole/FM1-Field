#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
cd "$(dirname "$0")/.."
mkdir -p app/build/tests
bs=app/third_party/bearssl
cc -O2 -Wall -Wextra -Werror -I"$bs/inc" -I"$bs/src" tests/stream_crypto_test.c \
 "$bs/src/symcipher/aes_small_ctr.c" "$bs/src/symcipher/aes_small_enc.c" \
 "$bs/src/symcipher/aes_common.c" "$bs/src/symcipher/aes_ct.c" \
 "$bs/src/symcipher/aes_ct_enc.c" "$bs/src/symcipher/aes_ct_ctr.c" \
 "$bs/src/hash/ghash_ctmul32.c" "$bs/src/aead/gcm.c" -o app/build/tests/stream_crypto_test
app/build/tests/stream_crypto_test
cc -O2 -Wall -Wno-unused-function -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
 -ffunction-sections -fdata-sections tests/stream_schedule_test.c -Wl,--gc-sections -o app/build/tests/stream_schedule_test
app/build/tests/stream_schedule_test
