#!/bin/sh
set -eu
cd /home/ubuntu/vlc-aaudio
core=libvlcjni/vlc
mkdir -p artifacts/test-include
ln -sfn /home/ubuntu/vlc-aaudio/toolchain/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/aaudio artifacts/test-include/aaudio
clang-18 -g -O1 -D_GNU_SOURCE '-DMODULE_STRING="aaudio"' -DMODULE_NAME=aaudio \
    '-D__INTRODUCED_IN(x)=' '-D__DEPRECATED_IN(x)=' '-D__REMOVED_IN(x)=' \
    -D_Nullable= -D_Nonnull= -I"$core/include" -Iartifacts/test-include \
    -ffunction-sections -fdata-sections -fsanitize=address,undefined \
    test-aaudio.c "$core/src/audio_output/common.c" \
    -Wl,--gc-sections -lm -lpthread -o artifacts/test-aaudio
ASAN_OPTIONS=detect_leaks=1 artifacts/test-aaudio
