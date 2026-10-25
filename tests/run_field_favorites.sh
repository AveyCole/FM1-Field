#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
cd "$(dirname "$0")/.."
mkdir -p app/build/tests
cc -O1 -Wall -Wno-unused-function tests/field_fav_test.c -o app/build/tests/field_fav_test
app/build/tests/field_fav_test
"${PYTHON:-python3}" base/tools/build.py app --host
app/build/host/field_host tests/field_favorites.host app/build/tests
APOREE_TEST_METADATA_DELAY=800 app/build/host/field_host tests/field_favorite_pending.host app/build/tests
app/build/host/field_host tests/aporee_regions.host app/build/tests
