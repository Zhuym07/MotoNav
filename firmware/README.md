# 通用圆屏固件 7.12.1

适用微雪 ESP32-S3-Touch-AMOLED-1.75（466×466），Android 8.3.21 公开源码/分享打包版与 iOS 0.13.8 共用。源码位于 `boards/175/MotoNav`，本次融合不适用于 1.85B 或 M5Stack StopWatch。

本版以 7.12.0 为基础，保留 HID/AMS 音乐、歌词及旋转支持，合入 7.8.13 的五分钟无导航关机和关机崩溃重启修复。导航、巡航中不关机；普通心跳、天气和音乐更新不重置空闲计时。详细兼容范围及实测限制见 [更新说明](../docs/RELEASE-FIRMWARE-7.12.1.md)。

## 下载与安装

从 [firmware-v7.12.1 Release](https://github.com/zyn950929/MotoNav/releases/tag/firmware-v7.12.1) 下载成品。

Windows：完整解压 `MotoNav-1.75-v7.12.1-Universal-Flasher.zip`，双击“一键烧录.bat”，核对板型和 COM 号。默认升级保留设置；完整刷写会整片擦除。

Mac：下载 `MotoNav-universal-firmware-7.12.1.zip`，解压到仓库根目录，保留 `firmware/dist/175/panel0`。已安装 Arduino CLI / ESP32 Core 3.3.10 和 esptool 后，运行根目录 `./一键烧录固件.command --check` 检查，再双击或执行烧录入口。默认写应用到 `0x10000`，要求既有 16 MB / app3M_fat9M_16MB 分区；首次完整安装按原入口提示确认。

## 编译

安装 Arduino CLI 和 Espressif ESP32 Arduino Core 3.3.10：

```sh
python3 firmware/build.py
```

Windows 也可执行 `./firmware/build.ps1 -Board 175`，用 `-CliPath`、`-ConfigFile` 指定工具。镜像与大小/哈希清单位于 `firmware/dist/175/panel0`。清单的 `deviceTested: false` 保持保守标记，具体已完成和未完成的硬件验证以上述更新说明为准。

## 测试

```sh
for test in firmware/tests/*Test.cpp; do g++ -std=c++17 "$test" -o /tmp/motonav-test && /tmp/motonav-test || exit 1; done
cc firmware/tests/idle-power-off-test.c -o /tmp/motonav-idle && /tmp/motonav-idle
python3 -m unittest discover -s firmware/tests -p 'test_flash_macos.py'
```

`windows-flasher/` 保存独立包的脚本源码。`releases/7.12.1/` 保存本次已校验包的发布输入分片，发布流程校验并组装原 ZIP；日常下载请使用 Release 成品。

## BOX0 移植版（240×240）

目标硬件为正点原子 ESP32-S3 BOX0：ST7789 240×240、SPI2、GPIO39/40/38/41、背光 GPIO42、电源保持 GPIO2。固件入口是 `boards/box0/MotoNav/MotoNav.ino`，目标芯片仍为 ESP32-S3、16 MB Flash、Octal PSRAM，分区使用 `app3M_fat9M_16MB`。

构建命令：

```powershell
./firmware/build.ps1 -Board box0
```

产物写入 `firmware/dist/box0/panel0`，包含应用镜像 `*_app_0x10000.bin`、完整镜像 `*_full_0x0.bin` 和清单。构建清单标记 `deviceTested: false`；编译通过不等于实机验收。

### 操作与显示

- 左键（GPIO3，低有效）：短按切换首页/音乐页；长按降低亮度。
- 中键（GPIO4，高有效）：短按接听来电、关闭到达特效、天气页操作或音乐页操作；长按 3 秒关机。
- 右键（GPIO0，低有效）：短按切换首页/天气页；长按提高亮度。右键同时参与启动模式，下载模式异常时先确认没有按住右键。
- 启动时先把 GPIO2 电源保持拉高，再初始化显示、蓝牙和网络；屏幕先同步刷出黑色首帧，再打开背光。
- BOX0 使用原生 240×240 画布，地图覆盖四角；导航、首页、配对、音乐、天气、来电和到达动画针对方屏排版。进度和告警沿圆角矩形边框显示。

### 尚未验证的硬件边界

移植包只确认了 LCD、基本按键和电源保持所需的引脚。触摸、TF 卡、音频、IMU、充电检测和电池电压/电量均没有已验证接线或标定，因此本固件不初始化、不猜测这些外设，也不伪造电量、充电状态或低压保护。首次覆盖原厂固件前，请先备份完整 16 MiB Flash；不要在未确认恢复路径时擦除设备。不要刷写或探测未知 GPIO47/48。

Android 发送端仍使用 `android/` 工程，构建命令：

```powershell
cd android
$env:JAVA_HOME='C:\Program Files\Microsoft\jdk-21.0.10.7-hotspot'
.\gradlew.bat :app:assembleDebug --offline
```

APK 输出为 `android/app/build/outputs/apk/debug/app-debug.apk`。

### BOX0 7.8.14 修复与骑行导航

7.8.14 使用原生 240×240 方屏布局：地图覆盖四角，导航进度和超速提醒沿圆角矩形边框显示。旧版圆形暗角向画布写入负行号和超过 239 的行，导致导航时 Cache error；当前暗角只遍历屏幕范围，分行渲染入口也统一裁剪。

长按中键关机时不再调用 `BLEDevice::deinit(true)`。当前 ESP32 Core 3.3.10 在停止 NimBLE 任务前销毁 BLEServer，断开回调可能访问已释放对象。BOX0 先熄屏，等中键释放后拉低电源保持并进入深睡，由断电或深睡停止蓝牙。

Android 路线页新增“驾车 / 骑行（自行车）”选择，并保存偏好。骑行调用高德 Android SDK 的骑行算路接口；骑行模式隐藏高速和途经点选项，移除已有途经点需确认。真实导航和模拟导航都沿用所选交通方式，骑行模拟速度为 10 / 20 / 30 km/h。导入的高德分享路线保留其原交通方式。

回归工具：`tests/test_box0_bounds.py`、`tests/test_box0_power_off.py` 提取生产函数生成 ARM64 测试程序；`tests/prepare_box0_probe.py` 生成独立诊断固件，在画布前后安装保护区；`tests/capture_box0.ps1` 与 `tests/decode_box0_capture.py` 保存串口和真实 RGB565 帧缓冲。诊断固件仅用于测试；正常发布镜像不包含串口场景指令。

## CYD 2.8 英寸横屏移植版

目标硬件为 ESP32-2432S028R 双 USB 版本（USB-C + micro-USB）：ST7789 320×240 横屏、XPT2046 电阻单点触摸、ESP32-WROOM、4 MB Flash、无 PSRAM。显示使用 GPIO14/13/15/2/21；触摸使用独立 SPI GPIO25/32/39、CS 33、IRQ 36。固件通过 MotoNav BLE UART 服务接收转向、距离、车速、限速、行程和 ETA。

该板使用独立的精简固件，不复用要求 ESP32-S3/PSRAM 的圆屏或 BOX0 全功能渲染器。UI 按 320×240 横屏排布，支持点按标签和左右滑动切换页面；设置页可对四角校准电阻触摸，校准结果保存在 NVS。触摸通知使用 App 既有的 `T,P,x,y` 格式。

在仓库根目录构建：

```powershell
.\firmware\build.ps1 -Board cyd -CliPath '.\.tools\arduino-cli\arduino-cli.exe' -ConfigFile '.\.tools\arduino-cli.yaml'
```

镜像输出到 `firmware/dist/cyd/panel0/`，目标为 ESP32 Dev Module 默认 4 MB Flash 分区。手机 App 通过 BLE 向屏幕发送地图栅格和路线数据；CYD 导航页支持地图样式/日夜模式、行驶轨迹、弯道着色、路线与原路线、拥堵、红绿灯、电子眼和位置预判。设备还包含行程、天气预报/预警、音乐、来电和触摸设置页面。地图内容由手机取图并传输，固件本身不联网。

用 USB-C 连接时，可从仓库根目录运行以下命令刷写当前 CYD 应用镜像；脚本会先检查 manifest 和 SHA-256，再以 ESP32 芯片参数写入 `0x10000`，保留 NVS 设置：

```powershell
.\firmware\boards\cyd\flash.ps1 -Port COM4
```

使用 `-Verify` 可将设备 Flash 中的应用与当前镜像回读比对。构建清单仍标记 `deviceTested: false`：当前镜像已在 COM4 刷入、回读校验并确认串口启动，用户确认显示正常；触摸校准和手机 BLE 功能仍需逐项实机验收。
