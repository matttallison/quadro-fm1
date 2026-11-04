#!/bin/sh
# QUADRO's tests: the sound of every factory program (tests/audio_test.c, twice: a phrase, and one key held), then
# the firmware in the host simulator (app/tests/basic.txt). Run from anywhere; a failure exits non-zero.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
CC=${CC:-cc}
$CC -O2 -w -Iapp -o build/audio_test tests/audio_test.c -lm
build/audio_test build/audio | tail -n 1
$CC -O2 -w -Iapp -o build/feature_test tests/feature_test.c -lm
build/feature_test | tail -n 1
$CC -O2 -w -Iapp -DHOLD -o build/hold_test tests/audio_test.c -lm
DRY=1 build/hold_test build/hold | tail -n 1
python3 base/tools/build.py app --host > build/host_build.log 2>&1 || { cat build/host_build.log; exit 1; }
mkdir -p build/host
app/build/host/quadro_host app/tests/basic.txt build/host
app/build/host/quadro_host app/tests/persist.txt build/host
echo "QUADRO tests: all ok"
