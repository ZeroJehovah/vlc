#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
task_core=${VLC_CORE_DIR:-$(CDPATH= cd -- "$test_dir/../.." && pwd)}
task_root=$(CDPATH= cd -- "$task_core/../.." && pwd)
ndk=${VLC_NDK_DIR:-$task_root/toolchain/android-ndk-r27c}
jdk=${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")}
test_output=${TEST_OUTPUT:-$task_root/artifacts/v4-clock3/test-drain}
mkdir -p "$test_output/include"
ln -sfn "$ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android" "$test_output/include/android"
git show 1bad5e168e:modules/audio_output/audiotrack.c > "$test_output/audiotrack-clock2.c"
sed -i.bak 's|"\.\./video_output/android/utils.h"|"'"$task_core"'/modules/video_output/android/utils.h"|' "$test_output/audiotrack-clock2.c"
for variant in before after; do
    source_file="$task_core/modules/audio_output/audiotrack.c"
    if [ "$variant" = before ]; then source_file="$test_output/audiotrack-clock2.c"; fi
    "${CC:-clang-18}" -g -O1 -D_GNU_SOURCE '-DMODULE_STRING="android_audiotrack"' -DMODULE_NAME=android_audiotrack \
        '-D__INTRODUCED_IN(x)=' '-D__INTRODUCED_IN_64(x)=' '-D__DEPRECATED_IN(...)=' '-D__REMOVED_IN(...)=' \
        -D_Nullable= -D_Nonnull= -D__ANDROID_API__=35 \
        -DAUDIOTRACK_SOURCE="\"$source_file\"" \
        -I"$task_core/include" -I"$test_output/include" -I"$jdk/include" -I"$jdk/include/linux" \
        -ffunction-sections -fdata-sections -fsanitize=address,undefined \
        "$test_dir/test-audiotrack-drain.c" -Wl,--gc-sections -lm -lpthread -o "$test_output/$variant"
done
ASAN_OPTIONS=detect_leaks=1 "$test_output/before" --expect-stall
ASAN_OPTIONS=detect_leaks=1 "$test_output/after"
