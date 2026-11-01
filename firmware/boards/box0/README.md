# MotoNav BOX0 固件

目标板：正点原子 ESP32-S3 BOX0（ESP32-S3、16 MiB Flash、8 MiB Octal PSRAM）。

## 构建

在仓库根目录运行：

```powershell
.\firmware\build.ps1 -Board box0 -CliPath './.tools/arduino-cli/arduino-cli.exe' -ConfigFile './.tools/arduino-cli.yaml'
```

如果 Windows 沙箱内的 Espressif 编译器报“无效的系统 DLL 重定位”，应在正常宿主进程中运行构建；同一编译器在沙箱外已验证可用。

产物位于 `firmware/dist/box0/panel0/`：

- `*_app_0x10000.bin`：应用镜像，烧录偏移 `0x10000`。
- `*_full_0x0.bin`：包含 bootloader、分区表、boot data 和应用的完整镜像，烧录偏移 `0x0`。
- `*_manifest.json`：板卡、Flash、分区、偏移、文件长度和 SHA256。

FQBN：`esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,CDCOnBoot=cdc`。

## 硬件映射

| 功能 | GPIO / 参数 |
| --- | --- |
| ST7789 SCLK / MOSI / DC / CS | 39 / 40 / 38 / 41 |
| LCD RESET | -1（由硬件复位） |
| LCD 背光 | 42 |
| 左键 / 中键 / 右键 | 3（低有效）/ 4（高有效）/ 0（低有效） |
| 电源保持 | 2（高有效） |
| 电池采样 | 1 / ADC1_CH0（未标定，当前不启用） |

不得猜测或探测资料未确认的 GPIO，尤其不要输出或探测 GPIO47/48。

## 当前边界

- 原生 240×240 方屏画布；地图覆盖四角，车速、箭头、车道、底部信息及首页、配对、音乐、天气、来电、到达动画分别排版。行程及转弯进度沿圆角矩形边框显示。
- 中键长按 3 秒执行关机流程；左/右键长按调节亮度。
- 触摸、TF、音频、IMU、充电和电池 ADC 均没有已验证接线或标定，固件不启用这些外设，也不伪造电量。
- 构建清单默认 `deviceTested: false`。实机验证结果另存测试记录；编译成功本身不代表验收通过。

## v7.8.13 重启修复

v7.8.12 的实机堆栈定位到 `vignetteRows → parallelRows → drawVignette → drawMapFrame`。圆屏渐暗使用了 -91 到 332 的行，越过 240×240 framebuffer，导致 Cache error 和 PANIC 重启。方屏改为矩形边缘渐暗，同时统一裁剪行回调、地图遮罩、图标复制和文字图片的可用区域。
