#!/bin/sh
set -eu
exec /usr/bin/qemu-x86_64 -L /home/ubuntu/vlc-aaudio/toolchain/x86-root \
    /home/ubuntu/vlc-aaudio/toolchain/android-sdk/build-tools/36.0.0/aapt2 "$@"
