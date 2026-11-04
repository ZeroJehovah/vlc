# VLC Android 3.7.2 暂停恢复实验修复

工作分支：`android/3.7.2-resume-fix`。这是基于 LibVLC 3.0.25 的个人构建，当前手机为 Redmi K50 Ultra、Android 13 / MIUI 14。

## 已完成的修改

1. AAudio 输出移植，Android 9 及以上可手动选择；用户实测仅增加 AAudio 没有解决暂停恢复卡顿。
2. 有界的暂停恢复诊断日志，以及 Android 应用日志服务和导出修复。
3. AudioTrack 恢复时唤醒音频发送线程，避免队列已有声音却等下一批解码数据才继续发送。添加两条诊断，记录恢复唤醒和首次发送之间的耗时。
4. AudioTrack 在厂商实现中可能于蓝牙路由恢复时让“非阻塞”写入阻塞约 384ms。新版在进入 Java 写入前检查内部缓冲的已播放位置，满缓冲时不调用写入，并在单次调用超过 50ms 时记录耗时。
5. 暂停恢复后的短窗口内，播放头估时暂时失真时不再按单次读数清空音频或补入静音，而由已有重采样器逐步收敛；连续稳定后恢复 VLC 3 原有同步规则。显式 flush、seek 和输出重启会退出恢复状态。

第 3、4 项已由实际手机日志推动实施；修复版 2 的真机日志确认写入阻塞消失，但播放头估时瞬态仍触发破坏性纠偏。视频继续而只有声音中断，进一步把问题定位到 VLC 3 的音频同步分支。修复版 3 尚未在用户手机上验证，未移植 VLC 4 的整体时钟架构。证据和限制见 [诊断说明](diagnosis.md)。

## 仓库内容

- 播放核心的修改直接保存在本分支，提交历史包括官方 Android 补丁和本次自定义提交。
- `patches/vlc-android.patch`：Android 应用完整改动，包含新增资源和 Java 文件。
- `patches/libvlcjni.patch`：ARM 构建主机适配，以及让插件静态库变更触发重新链接的依赖声明。
- `tests/`：实际 AudioTrack 模块的原生回归测试。
- `workspace/`：现有工作区的构建脚本、AAudio 和日志预览测试、主机适配工具、先前版本的公开构建校验记录。将其复制到下述工作区根目录使用。

签名私钥、签名密码、原始手机日志、SDK/NDK、二进制构建产物均不提交。签名材料须独立安全备份；仅恢复 Git 仓库不能恢复原来的 APK 签名身份。

## 精确基线

| 组件 | 官方仓库和提交 |
| --- | --- |
| 应用 3.7.2 | `https://code.videolan.org/videolan/vlc-android.git` / `e5740f713d995f6c8e7d92e64134abc3a3b6987d` |
| JNI | `https://code.videolan.org/videolan/libvlcjni.git` / `0b8dc65efb203c86a0476bc337ddd99ecf1c0ef6` |
| LibVLC 3.0.25 | `84177b2273abc5c4300234e6e18b55a4d2dd5a03`，再加 JNI 的 20 个 Android 补丁；本分支已经包含这些补丁，不要重复应用 |
| 最后一个官方 Android 补丁 | `562f0cfdc4`，也是 AudioTrack 回归测试的负对照基线 |
| contrib | `58f19304cbc30a42bc0a954b10d122a0155c1f2d`，官方 android-arm64 预编译包 |

Contrib 包：<https://artifacts.videolan.org/vlc-3.0/android-arm64/vlc-contrib-aarch64-linux-android-58f19304cbc30a42bc0a954b10d122a0155c1f2d.tar.zst>。

构建工具：NDK r27c / 27.2.12479018、SDK / build-tools 36、Gradle 9.3.1、AGP 9.1.1、Kotlin 2.2.10、Java 17。原版媒体库 `0.13.24` 和远程访问依赖 `0.24.0` 保持不变。

## 恢复工作区

现有脚本为 Ubuntu 24 ARM 主机及 `/home/ubuntu/vlc-aaudio` 编写，部分使用绝对路径；换目录须统一调整。准备目录：

```text
/home/ubuntu/vlc-aaudio/
  app-stable/          # 官方应用上述精确提交
  libvlcjni/           # 官方 JNI 上述精确提交
    vlc/               # 本 fork 的 android/3.7.2-resume-fix 分支
  toolchain/           # SDK、NDK、Gradle、QEMU amd64 运行库和适配脚本
```

在干净的、对应提交的应用/JNI 工作树中，分别运行 `git apply` 应用 `patches/vlc-android.patch`、`patches/libvlcjni.patch`。先用 `git apply --check` 确认基线。把 `workspace/` 的内容复制到工作区根目录。不要在已有修改的工作树重复应用补丁。

应用的 Gradle 设置需要 `app-stable/libvlcjni`；在干净应用目录运行 `ln -s ../libvlcjni libvlcjni`，指向上述相邻 JNI 工作树。

ARM 主机使用本机 LLVM/Clang/LLD 18 配合官方 NDK sysroot；`toolchain/adapt-ndk.sh` 为本工作区修改 NDK 主机工具入口。SDK 的 AAPT2/AIDL/zipalign 使用 QEMU x86_64。`prepare-x86-runtime.py` 读取 Ubuntu noble amd64 的 `Packages.gz`（保存为 `toolchain/amd64-packages.gz`），校验包 SHA-256 后解压到 `toolchain/x86-root/`。创建 `toolchain/host-tools/aapt2` 指向 `../aapt2-qemu.sh`；原 SDK `aidl` 保存为 `aidl.x86-original` 后用 `sdk-qemu.sh` 包装。不要对系统 SDK/NDK 执行这些任务目录专用适配。

在 `app-stable/local.properties` 配置：

```properties
sdk.dir=/home/ubuntu/vlc-aaudio/toolchain/android-sdk
android.ndkFullVersion=27.2.12479018
android.ndkPath=/home/ubuntu/vlc-aaudio/toolchain/android-ndk-r27c
```

完整原生构建在 `libvlcjni/` 下运行，先设置 `ANDROID_NDK` 指向上述 NDK；`VLC_PREBUILT_CONTRIBS_URL` 指向上述 contrib 包：

```sh
./buildsystem/compile-libvlc.sh -a arm64-v8a --release --with-prebuilt-contribs
```

现有构建树仅修改 AudioTrack 时：

```sh
cd /home/ubuntu/vlc-aaudio/libvlcjni/vlc/build-android-aarch64-linux-android/modules
make -j4 libandroid_audiotrack_plugin.la
install -m 644 .libs/libandroid_audiotrack_plugin.a ../install/lib/vlc/plugins/libandroid_audiotrack_plugin.a
cd /home/ubuntu/vlc-aaudio
sh link-native.sh -B
```

`-B` 强制完成 JNI 重链接。JNI 补丁中的 `LOCAL_ADDITIONAL_DEPENDENCIES` 仍记录插件归档依赖，但当前 NDK make 对这条规则没有稳定触发所有增量重链接场景。插件归档原先只列在链接参数中，make 可能将旧 `libvlc.so` 打入新 APK。打包脚本同时检查新库的诊断字符串与哈希。

应用编译与签名：

```sh
cd /home/ubuntu/vlc-aaudio/app-stable
JAVA_HOME=/usr/lib/jvm/java-17-openjdk-arm64 \
  ../toolchain/gradle-9.3.1/bin/gradle \
  -I ../build-resume-fix3.init.gradle \
  -Pandroid.aapt2FromMavenOverride=/home/ubuntu/vlc-aaudio/toolchain/host-tools/aapt2 \
  :application:app:assembleRelease --console=plain --max-workers=3
cd /home/ubuntu/vlc-aaudio
python3 package-resume-fix3.py
```

打包使用本机 `apksigner`，签名身份从私有 `toolchain/signing/` 读取。公开保存的旧证书摘要只用于验证覆盖升级身份，没有私钥。当前包：`3.7.2-resume-fix3`、versionCode `13070205`、包名 `org.videolan.vlc.aaudio`、arm64-v8a。

## 验证

```sh
cd /home/ubuntu/vlc-aaudio/libvlcjni/vlc
sh android-backport/tests/test-audiotrack-resume.sh
cd /home/ubuntu/vlc-aaudio
sh test-aaudio.sh
sh test-logger/run.sh
```

AudioTrack 测试支持 `VLC_CORE_DIR`、`VLC_NDK_DIR`、`JAVA_HOME`、`TEST_OUTPUT` 和 `CC` 覆盖路径。负对照确认旧源码在队列已有 200ms PCM 时仍会等待后续信号；修复后 20 次均无需新解码数据就能发出完整 PCM，通过 ASan/UBSan。模拟 JNI 的测试无法证明手机厂商音频栈上的卡顿已经解决。

手机测试请选择 **AudioTrack**，启用调试日志，暂停约 5 秒后继续，重复 3–5 次，每次继续后播放至少 5 秒，再停止记录并导出日志。重点观察 `defer_flush_resume`、`defer_silence_resume`、`resample_up` / `resample_down`、`resume_recovery_end`，以及恢复窗口之后是否仍有 `flush_late` / `insert_silence`；同时确认声音是否连续，以及是否出现可见的音画不同步。

`releases/resume-fix3.json` 保存当前交付 APK 的 SHA-256、原生库哈希、播放源码提交和校验项目；APK 本身保存在构建工作区 `artifacts/resume-fix3/`。实际 `dec.c` 同步路径的 sanitizer 测试、Release 编译、lint、覆盖升级签名身份、实际打包的三条恢复标记与日志服务声明均已检查；真机结果仍待反馈。
