#!/bin/sh
# host tests: compile the app's logic against the mocks in tools/screenshots/mock
set -e
cd "$(dirname "$0")/.."
CC=${CC:-cc}
FLAGS="-O1 -Wall -Wextra -Wno-unused-parameter -I tools/screenshots/mock -I app"
OUT=${TMPDIR:-/tmp}

$CC $FLAGS tools/adv_test.c app/adv.c app/vendor.c -o "$OUT/adv_test"
$CC $FLAGS tools/input_test.c app/input.c -o "$OUT/input_test"
$CC $FLAGS tools/gatt_event_test.c app/gatt_event.c -o "$OUT/gatt_event_test"

"$OUT/adv_test"
"$OUT/input_test"
"$OUT/gatt_event_test"
