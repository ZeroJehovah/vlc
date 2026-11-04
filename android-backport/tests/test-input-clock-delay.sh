#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
task_core=${VLC_CORE_DIR:-$(CDPATH= cd -- "$test_dir/../.." && pwd)}
output=${TEST_OUTPUT:-$task_core/../../artifacts/resume-fix5/test}
mkdir -p "$output"
"${CC:-clang-18}" -g -O1 -D_GNU_SOURCE \
    -DCLOCK_SOURCE='"'"$task_core/src/input/clock.c"'"' \
    -I"$task_core/include" \
    -I"$task_core/src" \
    -I"$task_core/build-android-aarch64-linux-android" \
    -ffunction-sections -fdata-sections -fsanitize=address,undefined \
    "$test_dir/test-input-clock-delay.c" -Wl,--gc-sections -lm -lpthread \
    -o "$output/test-input-clock-delay"
ASAN_OPTIONS=detect_leaks=1 "$output/test-input-clock-delay"
