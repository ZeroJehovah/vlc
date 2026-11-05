# VLC Android 3.7.2 共享输出时钟个人构建

当前工作分支：`android/3.7.2-v4-clock`；旧实验保留在 `android/3.7.2-resume-fix`。这是基于 LibVLC 3.0.25 的个人构建，当前手机为 Redmi K50 Ultra、Android 13 / MIUI 14。

fix6 已经真机测试失败：恢复后仍有明显延迟。当前改为移植 VLC 4 共享输出时钟，接入音频位置反馈、视频显示日期及字幕时间域，保留 3.x 应用、解码器和字幕/截图实现。具体范围、测试与限制见 [v4-clock 说明](v4-clock.md)。下面的 fix1–fix6 内容是历史记录，不能作为当前版本已修复的证据。

`v4-clock2` 已于 2026-11-05 通过用户真机测试，视频、音频与功能均正常且无卡顿；日志复核见 [验证记录](validation/v4-clock2-20261105.md)。收尾代码检查额外发现自然播放结束时 AudioTrack 排空等待的死锁，`v4-clock3` 修复了该顺序，暂停恢复时钟算法保持不变。当前交付为 `3.7.2-v4-clock3`、versionCode `13070211`；新包仍需补测自然结束及自动下一集。

## 既有实验记录

1. AAudio 输出移植，Android 9 及以上可手动选择；用户实测仅增加 AAudio 没有解决暂停恢复卡顿。
2. 有界的暂停恢复诊断日志，以及 Android 应用日志服务和导出修复。
3. AudioTrack 恢复时唤醒音频发送线程，避免队列已有声音却等下一批解码数据才继续发送。添加两条诊断，记录恢复唤醒和首次发送之间的耗时。
4. AudioTrack 在厂商实现中可能于蓝牙路由恢复时让“非阻塞”写入阻塞约 384ms。新版在进入 Java 写入前检查内部缓冲的已播放位置，满缓冲时不调用写入，并在单次调用超过 50ms 时记录耗时。
5. 暂停恢复后的短窗口内，播放头估时暂时失真时不再按单次读数清空音频或补入静音，而由已有重采样器逐步收敛；连续稳定后恢复 VLC 3 原有同步规则。显式 flush、seek 和输出重启会退出恢复状态。
6. 将 API 23 `WRITE_BYTEARRAYV23` 的厂商写入从播放器生产锁中解耦：发送线程先把一块 PCM 暂存到私有 staging 区并释放 `p_sys->lock`，再调用可能阻塞的 Java `AudioTrack.write()`；另用 `write_lock` 串行化 pause/play/flush。generation 和 `i_samples_inflight` 负责 flush 失效及时间估算。
7. 修复版 5 曾尝试在恢复窗口内平移共享输入时钟，让视频等待已经排队的音频。该方案会让画面短暂停顿，已被用户否决；当前共享时钟版本已移除这套输入时钟延迟 API。
8. fix6 尝试按暂停时长平移 AudioTrack 缓存，实测仍有明显不同步；旧版原本就会清空估时缓存，因此“把整个暂停时长误算成已播放”并非已证实根因。这项试验已由当前共享时钟方案替代。

第 3–8 项均由实际手机日志推动实施。fix2 避开了满缓冲写入阻塞，但仍触发破坏性纠偏；fix3 消除了断音，却留下音画偏差；fix4 解耦了写入锁，仍有约 333–953ms 的持续偏差；fix5 的视频等待设计被用户否决；fix6 的缓存调整实测失败，两个恢复窗口的末次偏差仍约 430ms / 781ms。当前转向完整的共享输出时钟接入，证据和限制见 [诊断说明](diagnosis.md)。

## 仓库内容

- 播放核心的修改直接保存在本分支，提交历史包括官方 Android 补丁和本次自定义提交。
- `patches/vlc-android.patch`：Android 应用完整改动，包含新增资源和 Java 文件。
- `patches/libvlcjni.patch`：ARM 构建主机适配，以及让插件静态库变更触发重新链接的依赖声明。
- `tests/`：实际 AudioTrack 模块的原生回归测试。
- `workspace/`：现有工作区的构建脚本、AAudio 和日志预览测试、主机适配工具、各版本的公开构建校验脚本。将其复制到下述工作区根目录使用。

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
    vlc/               # 本 fork 的 android/3.7.2-v4-clock 分支
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

现有构建树增量编译：

```sh
cd /home/ubuntu/vlc-aaudio/libvlcjni/vlc/build-android-aarch64-linux-android
make -j4
make install
cd /home/ubuntu/vlc-aaudio
sh link-native.sh -B
```

`-B` 强制完成 JNI 重链接。`link-native.sh` 会调用 `prepare-static-plugins.py`，根据官方构建生成的模块清单，从 `build/modules` 刷新全部静态插件并按官方规则重命名符号，输出到独立的 `build/ndk/static-plugins`。不要递归链接 `install/lib/vlc/plugins`：`make install` 可能在分类子目录中安装新插件，而顶层仍保留旧归档，造成链接器选中旧代码。模块配置变化时须重新运行完整构建脚本以刷新模块清单。

JNI 补丁中的 `LOCAL_ADDITIONAL_DEPENDENCIES` 仍记录插件归档依赖，但当前 NDK make 对增量重链接没有稳定触发所有场景。打包脚本同时检查插件归档来源及哈希、APK 内新库的诊断字符串与哈希。

应用编译与签名：

```sh
cd /home/ubuntu/vlc-aaudio/app-stable
JAVA_HOME=/usr/lib/jvm/java-17-openjdk-arm64 \
  ../toolchain/gradle-9.3.1/bin/gradle \
  -I ../build-v4-clock3.init.gradle \
  -Pandroid.aapt2FromMavenOverride=/home/ubuntu/vlc-aaudio/toolchain/host-tools/aapt2 \
  :application:app:assembleRelease :application:app:lintRelease --console=plain --max-workers=3
cd /home/ubuntu/vlc-aaudio
python3 package-v4-clock.py
```

打包使用本机 `apksigner`，签名身份从私有 `toolchain/signing/` 读取。公开保存的旧证书摘要只用于验证覆盖升级身份，没有私钥。当前收尾包：`3.7.2-v4-clock3`、versionCode `13070211`、包名 `org.videolan.vlc.aaudio`、arm64-v8a。已通过真机测试的 v4-clock2 APK 与原始打包记录保留在旧交付目录，不重新签名或覆盖。

## 验证

```sh
cd /home/ubuntu/vlc-aaudio/libvlcjni/vlc
sh android-backport/tests/test-output-clock.sh
sh android-backport/tests/test-audiotrack-resume.sh
sh android-backport/tests/test-audiotrack-write-lock.sh
sh android-backport/tests/test-audiotrack-drain.sh
sh android-backport/tests/test-aout-resume-recovery.sh
cd /home/ubuntu/vlc-aaudio
sh test-aaudio.sh
sh test-logger/run.sh
```

共享时钟测试直接编译移植的 VLC 4 算法、输入时钟适配和音频位置适配，覆盖音频反馈更新视频日期、暂停恢复、暂停中跳转、队列中的变速音频和主时钟替换。AudioTrack 测试支持 `VLC_CORE_DIR`、`VLC_NDK_DIR`、`JAVA_HOME`、`TEST_OUTPUT` 和 `CC` 覆盖路径。恢复测试的负对照确认旧源码在队列已有 200ms PCM 时仍会等待后续信号；修复后 20 次均无需新解码数据就能发出完整 PCM，并拒绝暂停前时间戳、未写入的位置及异常回绕。写入解耦测试让厂商 `write()` 故意阻塞 150ms，验证生产者仍能继续入队，以及 partial write 后 Stop 可退出。旧 `aout_DecSynchronize()` 测试只覆盖保留的兼容路径，新主音频路径不再使用该纠偏逻辑。以上测试均通过 ASan/UBSan，但不能证明手机厂商音频栈上的问题已经解决。

手机测试请选择 **AudioTrack**，启用调试日志，暂停约 5 秒后继续，重复 3–5 次，每次继续后播放至少 5 秒，再停止记录并导出日志。还应测试暂停期间跳转、播放中跳转、字幕及截图。新路径标记为 `[v4-clock] enabled master=1 native_timing=1`、`AudioTrack source=timestamp` 或 `raw-head`、`feedback system_us=`、`video_clock attached=1`、`video media_us=`。同时用正脸说话画面确认声音连续、画面流畅且口型没有明显错位。

`releases/resume-fix4.json` 和 `releases/resume-fix6.json` 保留旧构建及真机失败记录；`releases/resume-fix5.json` 记录用户否决的“画面等待”方案。`releases/v4-clock1.json` 记录首版真机视频冻结问题；`releases/v4-clock2.json` 保存已测试 APK 的哈希与真机结果；`releases/v4-clock3.json` 保存收尾 APK 的哈希与自动验证。安装包保存在构建工作区 `artifacts/v4-clock3/`，新包 `device_tested=false`，不将 v4-clock2 的手机结论自动视作新包已验证。

完整 `lintRelease` 已执行，报告仍有收尾前已有的 6 个 Android TV manifest 错误与 9 个警告；应用上游的 `abortOnError=false` 允许构建继续，因此不能把 Gradle 成功当作 lint 零问题。打包会检查报告新鲜度并记录实际数量；报告随本地交付保存，详细范围见真机验证文档。

## 长期保存与使用

固定源码标签为 `android-3.7.2-v4-clock3`。保存同目录的签名 APK、`SHA256SUMS`、构建元数据、源码改动包与 Git bundle，便于恢复和核对；Git bundle 以前述 LibVLC 3.0.25 官方提交为前提，包含全部后续 Android 和自定义提交。完整恢复还需要应用/JNI 官方基线，以及单独安全保管的签名身份。

日常继续选择已经测试的 AudioTrack。停止调试记录，并在高级设置关闭“详细日志/Verbose”；以后遇到问题可重新启用。原有“安装每日构建”仍指向官方普通 nightly 通道，不能更新本个人 fork，且官方包不携带本次移植。
