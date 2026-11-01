# 项目目录与清理记录

整理日期：2026-11-01。

## 这份代码是什么

这是本机 MotoNav 工作区的源码保存分支，基于原本的 `3b37e44`，保留尚未提交的 Android 8.3.25 修改、BOX0 方屏移植、CYD 横屏移植和测试。它没有将较新的上游 main 合并进来；根 README 及 RELEASE_VERSION 中的正式发布版本信息沿用原记录，不能将本分支当作一次新的正式发布。

GitHub 保存位置为 `Zhuym07/MotoNav` 的 `cleanup/box0-cyd-20261101` 分支。`origin` 指向个人 fork，`upstream` 指向 `zyn950929/MotoNav`。本次没有删除或覆盖其他远程仓库、分支或发布。

## 应保留并提交 Git 的内容

| 路径 | 用途 |
| --- | --- |
| `android/app/src/` | Android 应用源码、界面、图标及资源 |
| `android/tests/` | Android 主机回归测试 |
| `android/*.kts`、`gradle.properties`、`gradlew*`、`gradle/wrapper/` | 构建定义与 Gradle Wrapper；wrapper JAR 是必要构建文件 |
| `ios/` | iOS 应用、分享扩展、Xcode 工程、依赖清单和测试 |
| `firmware/boards/` | 原有圆屏、BOX0、CYD 固件源码及硬件配置 |
| `firmware/libraries/` | 固件实际依赖的第三方源码、字体、资源和许可，不能当作下载缓存删除 |
| `firmware/tests/` | 固件检查、诊断抓屏和回放工具源码 |
| `firmware/windows-flasher/`、`firmware/build.*`、`firmware/flash_macos.py` | 构建与烧录脚本 |
| `firmware/releases/7.12.1/` | 已有发布输入分片；release-firmware 工作流仍依赖，保留 |
| `.github/`、`scripts/` | 已有构建和发布流程 |
| `docs/`、`licenses/`、`LICENSE.md`、`README.md`、版本文件、根目录构建入口 | 项目文档、展示素材、许可和入口 |
| `.git/` | 版本历史和远程配置；本地保留，不作为普通源码再次上传 |

`docs/media/` 的展示图片与视频仍被 README 引用，属于项目资源。不要按 `.jar`、`.bin`、图片或视频等扩展名一刀切删除。

## 清理的可再生成内容

清理前，外层 `MotoNav` 目录共 47,228 个文件，内容大小为 **9,584,537,121 字节（9.58 GB / 8.93 GiB）**。这是本次扫描得到的文件长度总和，不是 Windows“占用空间”，也不是此前 18 GB 的历史测量。

| 路径 | 清理前大小 | 原因 |
| --- | ---: | --- |
| `.tools/` | 8,174,958,015 字节 | 本地 Arduino CLI、ESP32 编译工具链、板卡包和下载归档，可重新安装；其中下载 staging 占 1,841,934,558 字节 |
| `artifacts/` | 526,822,134 字节 | 旧 APK、固件镜像、ELF、反编译 SDK、生成的 C++ 测试程序、手机截图和串口日志 |
| `firmware/.build/` | 325,085,073 字节 | 固件编译中间文件及源码副本 |
| `firmware/dist/` | 37,418,325 字节 | 构建生成的固件与校验清单 |
| `android/app/build/`、`android/.gradle/`、`android/.kotlin/` | Android 目录原共约 341 MB，其中大部分为构建产物 | APK、重复原生库、编译中间文件和项目缓存 |
| `android/.local/test-classes/`、`android/.local/test-temp/` | 很小 | 主机测试的生成文件 |
| 外层和根目录 `serial-*.txt`、`phone-screen.png`、`quote-test.txt`、旧 `.bak` | 很小 | 临时抓取、测试文件和已被当前代码替代的备份 |

清理前已把 `artifacts/box0-test/build_box0_replay.py` 移至 `firmware/tests/`，把 BOX0 历史验证记录和崩溃诊断移至 `docs/BOX0-VALIDATION-20261019.md` 与 `docs/BOX0-CRASH-20261019.txt`，避免将有用的源码和说明当作产物丢弃。

## 清理结果

清理后工作区约 **177 MB（169 MiB）**，其中约 60 MB 是保留的 Git 历史；相比最初文件内容大小，净减少约 **9.41 GB（98.2%）**。以上清理目标已删除，Git 工作区核查无源码缺失或未提交改动，个人 fork 上的源码提交已核验一致。后续文档提交会使 `.git` 体积有少量变化。

保留内容的主要占用：`firmware/` 约 82 MB（第三方源码/字体、必要发布输入、各板型源码），`docs/` 约 33 MB（含 README 使用的图片与视频），`android/` 约 1.6 MB，`ios/` 约 0.57 MB。

## 仅本地保留，不上传

- `android/.local/debug.keystore`：当前开发签名，删除后重新生成将改变签名，可能影响覆盖安装和高德绑定；保留且忽略。
- `android/local.properties`：本机 Android SDK 路径，保留且忽略。
- 如以后存在 `android/personal.properties`、`android/keystore/` 或服务密钥，也应单独备份并保持 Git 忽略。

## 清理后如何构建

- Android：保留系统中已有的 JDK、Android SDK 和全局 Gradle 缓存；本次只清项目内缓存。按根 README 运行一键打包或 `android/gradlew.bat :app:assembleDebug`，缺少的依赖需要重新下载。
- 固件：重新安装 Arduino CLI 和 Espressif ESP32 Arduino Core **3.3.10**，按 `firmware/README.md` 使用 `build.ps1` 构建相应板型。若继续使用项目内工具，重新提供 `.tools/arduino-cli.yaml`，并通过 `-CliPath` 和 `-ConfigFile` 指定实际路径。原来的 `.tools` 绝对路径配置不提交。
- `firmware/.build/`、`firmware/dist/`、`android/app/build/` 由构建脚本重建。清理后不能在尚未重新构建或下载发布包时直接执行烧录。
- `firmware/tests/build_box0_replay.py` 是保留的历史诊断工具，需要先构建 BOX0，并自行提供原脚本读取的本地地图包和构建属性文件；含实际位置的地图、设备截图及日志不上传。它不属于正常固件构建所需文件。

## 本次检查

- `android/tests/run.ps1`：7 组测试通过，合计 137 项检查。
- `python -B -m unittest discover -s firmware/tests -p test_flash_macos.py`：5 项通过；临时目录设置在项目内以符合当前沙箱权限。
- 固件 Python 工具语法检查通过；待提交文本的私钥和常见凭据模式检查无匹配。
- 本次工作为目录整理与源码保存，没有重新进行完整 Android/iOS/固件构建或实机导航、触摸、BLE 验收。历史实机记录不视作本次复验。
