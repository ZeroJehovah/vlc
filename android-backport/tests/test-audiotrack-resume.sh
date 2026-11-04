#!/bin/sh
# Native-host regression using the actual VLC module with a fake JNI sink.
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
task_core=${VLC_CORE_DIR:-$(CDPATH= cd -- "$test_dir/../.." && pwd)}
task_root=$(CDPATH= cd -- "$task_core/../.." && pwd)
ndk=${VLC_NDK_DIR:-$task_root/toolchain/android-ndk-r27c}
jdk=${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")}
test_output=${TEST_OUTPUT:-$task_root/artifacts/resume-fix1/test}
mkdir -p "$test_output/include"
ln -sfn "$ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android" "$test_output/include/android"
python3 - "$task_core" "$test_output" <<'PY'
import pathlib, subprocess, sys
core = pathlib.Path(sys.argv[1])
source = subprocess.check_output(['git', 'show', '562f0cfdc4:modules/audio_output/audiotrack.c'], cwd=core, text=True)
source = source.replace('"../video_output/android/utils.h"', '"' + str(core / 'modules/video_output/android/utils.h') + '"')
(pathlib.Path(sys.argv[2]) / 'audiotrack-before.c').write_text(source)
PY
for variant in before after; do
    if [ "$variant" = before ]; then
        source_file="$test_output/audiotrack-before.c"
        guard_flag=
    else
        source_file="$task_core/modules/audio_output/audiotrack.c"
        guard_flag=-DEXPECT_V23_BUFFER_GUARD
    fi
    "${CC:-clang-18}" -g -O1 -D_GNU_SOURCE '-DMODULE_STRING="android_audiotrack"' -DMODULE_NAME=android_audiotrack \
        '-D__INTRODUCED_IN(x)=' '-D__INTRODUCED_IN_64(x)=' '-D__DEPRECATED_IN(...)=' '-D__REMOVED_IN(...)=' \
        -D_Nullable= -D_Nonnull= -D__ANDROID_API__=35 \
        $guard_flag \
        -DAUDIOTRACK_SOURCE="\"$source_file\"" \
        -I"$task_core/include" -I"$test_output/include" \
        -I"$jdk/include" -I"$jdk/include/linux" \
        -ffunction-sections -fdata-sections -fsanitize=address,undefined \
        "$test_dir/test-audiotrack-resume.c" -Wl,--gc-sections -lm -lpthread -o "$test_output/$variant"
done
ASAN_OPTIONS=detect_leaks=1 "$test_output/before" --expect-stall
ASAN_OPTIONS=detect_leaks=1 "$test_output/after"
