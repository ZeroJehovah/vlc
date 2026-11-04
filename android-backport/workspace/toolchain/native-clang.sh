#!/bin/sh
# Run the native ARM-host LLVM driver with the official Android NDK sysroot.
set -eu
ndk_prebuilt=/home/ubuntu/vlc-aaudio/toolchain/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64
case "${0##*/}" in
    *++) driver=/usr/bin/clang++-18 ;;
    *) driver=/usr/bin/clang-18 ;;
esac
exec "$driver" --sysroot="$ndk_prebuilt/sysroot" \
    -resource-dir "$ndk_prebuilt/lib/clang/18" \
    --rtlib=compiler-rt --unwindlib=libunwind \
    --ld-path=/usr/bin/ld.lld-18 "$@"
