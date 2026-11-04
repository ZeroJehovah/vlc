#!/bin/sh
# Demonstrate that a blocked vendor write must not hold the AudioTrack producer lock.
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
task_core=${VLC_CORE_DIR:-$(CDPATH= cd -- "$test_dir/../.." && pwd)}
task_root=$(CDPATH= cd -- "$task_core/../.." && pwd)
ndk=${VLC_NDK_DIR:-$task_root/toolchain/android-ndk-r27c}
jdk=${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")}
test_output=${TEST_OUTPUT:-$task_root/artifacts/resume-fix4/test-write-lock}
mkdir -p "$test_output/include"
ln -sfn "$ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android" "$test_output/include/android"
git show 0e7b261864:modules/audio_output/audiotrack.c > "$test_output/audiotrack-fix3.c"
sed -i.bak 's|"\.\./video_output/android/utils.h"|"'"$task_core"'/modules/video_output/android/utils.h"|' "$test_output/audiotrack-fix3.c"

for variant in before after; do
    if [ "$variant" = before ]; then
        source_file="$test_output/audiotrack-fix3.c"
        define=
    else
        source_file="$task_core/modules/audio_output/audiotrack.c"
        define=-DEXPECT_WRITE_DECOUPLING
    fi
    "${CC:-clang-18}" -g -O1 -D_GNU_SOURCE '-DMODULE_STRING="android_audiotrack"' -DMODULE_NAME=android_audiotrack \
        '-D__INTRODUCED_IN(x)=' '-D__INTRODUCED_IN_64(x)=' '-D__DEPRECATED_IN(...)=' '-D__REMOVED_IN(...)=' \
        -D_Nullable= -D_Nonnull= -D__ANDROID_API__=35 $define \
        -DAUDIOTRACK_SOURCE="\"$source_file\"" \
        -I"$task_core/include" -I"$test_output/include" \
        -I"$jdk/include" -I"$jdk/include/linux" \
        -ffunction-sections -fdata-sections -fsanitize=address,undefined \
        "$test_dir/test-audiotrack-write-lock.c" -Wl,--gc-sections -lm -lpthread -o "$test_output/$variant"
done
ASAN_OPTIONS=detect_leaks=1 "$test_output/before"
ASAN_OPTIONS=detect_leaks=1 "$test_output/after"
