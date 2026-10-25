#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
cd "$(dirname "$0")/.."
mkdir -p app/build/tests
cc -O1 -Wall -Wno-unused-function -Ibase/include tests/sampler_test.c -lm -o app/build/tests/sampler_test
app/build/tests/sampler_test
cc -O1 tests/loader_link_test.c -o app/build/tests/loader_link_test
app/build/tests/loader_link_test
"${PYTHON:-python3}" base/tools/build.py app --host
app/build/host/field_host tests/field_startup.host app/build/tests
app/build/host/field_host tests/field_octaves.host app/build/tests
if [ -n "${APOREE_TEST_STREAM:-}" ]; then
    app/build/host/field_host tests/field_sampler.host app/build/tests
else
    echo "MP3 capture UI test skipped: set APOREE_TEST_STREAM to a long MP3 fixture."
fi
