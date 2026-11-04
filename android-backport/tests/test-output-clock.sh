#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
task_core=$(CDPATH= cd -- "$test_dir/../.." && pwd)
output=${TEST_OUTPUT:-$task_core/../../artifacts/v4-clock/test}
mkdir -p "$output"
"${CC:-clang-18}" -g -O1 -D_GNU_SOURCE -DMODULE_STRING='"clock-test"' \
    -I"$task_core/include" -I"$task_core/src" \
    -I"$task_core/build-android-aarch64-linux-android" \
    -ffunction-sections -fdata-sections -fsanitize=address,undefined \
    "$test_dir/test-output-clock.c" \
    "$task_core/src/clock/clock.c" "$task_core/src/clock/clock_internal.c" \
    "$task_core/src/input/clock.c" "$task_core/src/audio_output/timing.c" \
    -Wl,--gc-sections -lm -lpthread -o "$output/test-output-clock"
ASAN_OPTIONS=detect_leaks=1 "$output/test-output-clock"
