# VLC Android 3.7.2 暂停恢复实验修复

工作分支：`android/3.7.2-resume-fix`。这是基于 LibVLC 3.0.25 的个人构建，当前手机为 Redmi K50 Ultra、Android 13 / MIUI 14。

## 已完成的修改

1. AAudio 输出移植，Android 9 及以上可手动选择；用户实测仅增加 AAudio 没有解决暂停恢复卡顿。
2. 有界的暂停恢复诊断日志，以及 Android 应用日志服务和导出修复。
3. AudioTrack 恢复时唤醒音频发送线程，避免队列已有声音却等下一批解码数据才继续发送。添加两条诊断，记录恢复唤醒和首次发送之间的耗时。
4. AudioTrack 在厂商实现中可能于蓝牙路由恢复时让“非阻塞”写入阻塞约 384ms。新版在进入 Java 写入前检查内部缓冲的已播放位置，满缓冲时不调用写入，并在单次调用超过 50ms 时记录耗时。
5. 暂停恢复后的短窗口内，播放头估时暂时失真时不再按单次读数清空音频或补入静音，而由已有重采样器逐步收敛；连续稳定后恢复 VLC 3 原有同步规则。显式 flush、seek 和输出重启会退出恢复状态。
6. 将 API 23 `WRITE_BYTEARRAYV23` 的厂商写入从播放器生产锁中解耦：发送线程先把一块 PCM 暂存到私有 staging 区并释放 `p_sys->lock`，再调用可能阻塞的 Java `AudioTrack.write()`；另用 `write_lock` 串行化 pause/play/flush。generation 和 `i_samples_inflight` 负责 flush 失效及时间估算。
7. 修复版 5 曾尝试在恢复窗口内平移共享输入时钟，让视频等待已经排队的音频。该方案会让画面短暂停顿，已被用户否决；当前修复版 6 已撤掉该运行时行为，不把它作为默认同步策略。
8. 参考 VLC 4 的暂停恢复时间修正：AudioTrack 的系统时间戳和播放头平滑缓存跨暂停后按暂停时长平移，不再把暂停期间误算成已播放。视频时钟保持原速，不主动等待音频。异常播放头估时会记录发送帧、AudioTrack 帧、环形队列和 staging 帧，便于定位厂商音频栈行为。

第 3–8 项均由实际手机日志推动实施。修复版 2 的真机日志确认先前的满缓冲写入被避开，但播放头估时瞬态仍触发破坏性纠偏。修复版 3 消除了断音，却仍会出现持续音画偏差。修复版 4 解除了厂商写入与生产锁的耦合；真机确认写入锁不再是主要矛盾，但恢复后仍有约 333–953ms 的持续偏差，而且手动 seek 后立即恢复。修复版 5 尝试让视频短暂等待音频，但该设计未经过真机确认，且用户明确不接受画面停顿。修复版 6 改为移植 VLC 4 的暂停后时间缓存修正，不主动延迟视频；仍需手机实测。证据和限制见 [诊断说明](diagnosis.md)。

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
  -I ../build-resume-fix6.init.gradle \
  -Pandroid.aapt2FromMavenOverride=/home/ubuntu/vlc-aaudio/toolchain/host-tools/aapt2 \
  :application:app:assembleRelease --console=plain --max-workers=3
cd /home/ubuntu/vlc-aaudio
python3 package-resume-fix5.py
```

打包使用本机 `apksigner`，签名身份从私有 `toolchain/signing/` 读取。公开保存的旧证书摘要只用于验证覆盖升级身份，没有私钥。当前实验包：`3.7.2-resume-fix6`、versionCode `13070208`、包名 `org.videolan.vlc.aaudio`、arm64-v8a。

## 验证

```sh
cd /home/ubuntu/vlc-aaudio/libvlcjni/vlc
sh android-backport/tests/test-audiotrack-resume.sh
sh android-backport/tests/test-audiotrack-write-lock.sh
sh android-backport/tests/test-aout-resume-recovery.sh
cd /home/ubuntu/vlc-aaudio
sh test-aaudio.sh
sh test-logger/run.sh
```

AudioTrack 测试支持 `VLC_CORE_DIR`、`VLC_NDK_DIR`、`JAVA_HOME`、`TEST_OUTPUT` 和 `CC` 覆盖路径。恢复测试的负对照确认旧源码在队列已有 200ms PCM 时仍会等待后续信号；修复后 20 次均无需新解码数据就能发出完整 PCM，并检查暂停时长会正确平移时间戳和播放头平滑缓存。写入解耦测试让厂商 `write()` 故意阻塞 150ms：修复版 3 会让生产者全程等待，修复版 4 可在阻塞期间继续入队；另验证单次 partial write 后发生 Stop 时，非空 staging 不会令发送线程空转。核心同步决策测试直接调用实际 `aout_DecSynchronize()`，验证恢复窗口不清空/补静音，也不请求延迟共享视频时钟。以上测试均通过 ASan/UBSan。模拟 JNI 的测试不能证明手机厂商音频栈上的问题已经解决。

手机测试请选择 **AudioTrack**，启用调试日志，暂停约 5 秒后继续，重复 3–5 次，每次继续后播放至少 5 秒，再停止记录并导出日志。重点观察 `timing_shift`、`staged_write blocked_us`、`backend_play call_us`、`defer_flush_resume`、`defer_silence_resume`、`resample_up` / `resample_down`、`resume_recovery_end`，以及恢复窗口之后是否仍有 `flush_late` / `insert_silence`；同时用正脸说话画面确认声音连续、画面不停顿且口型没有明显错位。

`releases/resume-fix4.json` 保留修复版 4 的构建与真机失败记录；`releases/resume-fix5.json` 记录未实测即被用户否决的“画面等待”方案；`releases/resume-fix6.json` 保存当前实验包的 SHA-256、原生库哈希、播放源码提交和自动校验项目。APK 本身保存在构建工作区 `artifacts/resume-fix6/`。修复版 6 的 Release 编译、lint、覆盖升级签名身份、打包标记、恢复测试和写入解耦测试均已通过，但 `device_tested` 为 `false`，必须由手机实测决定下一轮方向。
