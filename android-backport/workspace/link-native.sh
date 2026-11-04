#!/bin/sh
set -eu
task_root=/home/ubuntu/vlc-aaudio
jni_root=$task_root/libvlcjni
core=$jni_root/vlc
build=$core/build-android-aarch64-linux-android
contrib=$core/contrib/aarch64-linux-android
modules=$(find "$build/install/lib/vlc/plugins" -name 'lib*plugin.a' -print | tr '\n' ' ')
libs=$(cd "$contrib/lib/pkgconfig" && PKG_CONFIG_LIBDIR="$contrib/lib/pkgconfig" \
    PKG_CONFIG_PATH="$contrib/lib/pkgconfig" pkg-config --libs $(ls *.pc | sed 's/\.pc$//'))
"$task_root/toolchain/native-ndk-build.sh" -j4 -C "$jni_root/libvlc" \
    APP_STL=c++_shared VLC_SRC_DIR="$core" VLC_BUILD_DIR="$build" \
    VLC_CONTRIB="$contrib" VLC_CONTRIB_LDFLAGS="$libs" \
    VLC_MODULES="$modules" VLC_LDFLAGS='-z max-page-size=16384' VLC_BUILD_JNI=1 \
    APP_BUILD_SCRIPT=jni/Android.mk APP_PLATFORM=android-21 APP_ABI=arm64-v8a \
    NDK_PROJECT_PATH=jni NDK_TOOLCHAIN_VERSION=clang NDK_DEBUG=0
