#!/bin/sh
set -eu
ndk_root=/home/ubuntu/vlc-aaudio/toolchain/android-ndk-r27c
ndk_bin="$ndk_root/toolchains/llvm/prebuilt/linux-x86_64/bin"
replace_tool() {
    tool_path=$1
    replacement=$2
    if [ ! -e "$tool_path.ndk-original" ] && [ ! -L "$tool_path.ndk-original" ]; then
        mv "$tool_path" "$tool_path.ndk-original"
        ln -s "$replacement" "$tool_path"
    fi
}
replace_tool "$ndk_bin/clang" /home/ubuntu/vlc-aaudio/toolchain/native-clang.sh
replace_tool "$ndk_bin/clang++" /home/ubuntu/vlc-aaudio/toolchain/native-clang.sh
replace_tool "$ndk_bin/ld.lld" /usr/bin/ld.lld-18
for tool in ar as nm objcopy objdump ranlib readelf size strings strip; do
    if [ -e "$ndk_bin/llvm-$tool" ] && [ -x "/usr/bin/llvm-$tool-18" ]; then
        replace_tool "$ndk_bin/llvm-$tool" "/usr/bin/llvm-$tool-18"
    fi
done
replace_tool "$ndk_root/prebuilt/linux-x86_64/bin/make" /usr/bin/make
replace_tool "$ndk_root/ndk-build" /home/ubuntu/vlc-aaudio/toolchain/native-ndk-build.sh
