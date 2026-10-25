#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
cd "$(dirname "$0")/.."
mkdir -p app/build/tests
cc -O1 -Wall -Wno-unused-function -Ibase/include tests/sampler_test.c -lm -o app/build/tests/sampler_test
app/build/tests/sampler_test
cc -O1 -Wall -Wno-unused-function -Ibase/include tests/sample_store_test.c -lm -o app/build/tests/sample_store_test
app/build/tests/sample_store_test
cc -O1 -Wall -Wno-unused-function -Ibase/include tests/filter_test.c -lm -o app/build/tests/filter_test
app/build/tests/filter_test
cc -O1 -Wall -Wno-unused-function -Ibase/include tests/space_test.c -lm -o app/build/tests/space_test
app/build/tests/space_test
cc -O1 tests/loader_link_test.c -o app/build/tests/loader_link_test
app/build/tests/loader_link_test
"${PYTHON:-python3}" base/tools/build.py app --host
"${PYTHON:-python3}" tests/font_padding_test.py
app/build/host/field_host tests/field_startup.host app/build/tests
app/build/host/field_host tests/field_octaves.host app/build/tests
if [ -n "${APOREE_TEST_STREAM:-}" ]; then
    app/build/host/field_host tests/field_sampler.host app/build/tests
    app/build/host/field_host tests/field_tempo.host app/build/tests
    app/build/host/field_host tests/field_filter.host app/build/tests
    app/build/host/field_host tests/field_sampler_audio.host app/build/tests
    app/build/host/field_host tests/field_sample_save.host app/build/tests
    app/build/host/field_host tests/field_space.host app/build/tests
    app/build/host/field_host tests/field_display.host app/build/tests
    if command -v node >/dev/null 2>&1; then
        node tests/screen_stream_test.mjs
        node tests/viewer_test.mjs
    else
        echo "Run node tests/screen_stream_test.mjs and node tests/viewer_test.mjs after this script."
    fi
else
    echo "MP3 capture UI test skipped: set APOREE_TEST_STREAM to a long MP3 fixture."
fi
