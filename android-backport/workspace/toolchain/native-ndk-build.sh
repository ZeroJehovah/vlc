#!/bin/sh
set -eu
ndk_root=/home/ubuntu/vlc-aaudio/toolchain/android-ndk-r27c
exec /usr/bin/make -O -f "$ndk_root/build/core/build-local.mk" HOST_ARCH=x86_64 "$@"
