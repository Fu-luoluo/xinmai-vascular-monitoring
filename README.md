# 心脉血管监测系统 · xinmai-vascular-monitoring

基于 **WCH CH585（RISC-V）** 的分布式脉搏波/血管监测演示系统：主控大屏节点 + 指夹式与腕带式两路 BLE 测量外设 + ESP32-C3 WiFi/MQTT 云网关，并带有三级 OTA 升级链路。

## 系统组成

| 节点 | 角色 |
|---|---|
| `Central_LCD/` | 主控（BLE Central 双链路）：LCD 显示（LVGL 8.x）、MAX30102 数据接收、脉搏波速度（PWV）分析与机器学习门控、报警/语音、W25Q 历史存储、OTA（APP 侧） |
| `FingerPeriph/` | 指夹式外设：MAX30102 采集 + FIR 滤波 + SpO₂/脉率测量，BLE 外设上报，支持 OTA |
| `WristPeriph/` | 腕带式外设：同上架构，佩戴于手腕 |
| `Central_LCD_IAP/` | 12 KB IAP 引导程序（@0x6D000，从 W25Q 搬运 OTA 镜像到 App 区），独立 `make` 工程 |
| `Central_LCD_JumpIAP/` | 4 KB 启动选择器（@0x0000，按 EEPROM 标志跳转 App/IAP），独立 `make` 工程 |
| `C3_SuperMini_CH585_MQTT/` | ESP32-C3-SuperMini Arduino 工程：WiFi/MQTT 云网关，与主控 UART1(57600) 通信，支持屏上配网与 OTA 命令转发 |
| `LinkFiles/` | 共享的 WCH CH58x SDK 子集（StdPeriphDriver / HAL / RVMSIS / LIB 预编译 BLE 库 / Ld / Startup），**必须与工程目录同级** |
| `Shared_OTA_BLE/` | FingerPeriph / WristPeriph 共用的 BLE OTA 源文件，**必须与工程目录同级** |

## 目录关系（务必保持）

各 MounRiver 工程的 `.project`/`.wvproj` 通过相对路径链接平级目录：

```
repo-root/
├── LinkFiles/          ← 链接 ../LinkFiles/...（PARENT-1-PROJECT_LOC）
├── Shared_OTA_BLE/     ← Finger/Wrist 编译需要（#include "ota_iap.h"）
├── Central_LCD/  FingerPeriph/  WristPeriph/  Central_LCD_IAP/  Central_LCD_JumpIAP/
└── C3_SuperMini_CH585_MQTT/
```

克隆后请保持该平级布局，不要单独移动任一工程目录。

## 构建环境

- MounRiver Studio 2.x（内置 WCH RISC-V GCC 工具链），芯片 CH585M，调试器 WCH-Link
- 打开方式：MounRiver → 导入 `*.wvproj` 工程（Central_LCD / FingerPeriph / WristPeriph）
- `Central_LCD_IAP/`、`Central_LCD_JumpIAP/`：命令行 `make`（工具链需在 PATH）
- `C3_SuperMini_CH585_MQTT/`：Arduino IDE + ESP32-C3 支持包；**须开启 “USB CDC On Boot = Enabled”**（GPIO20/21 被 UART0 占用）

## OTA 升级链路

```
JumpIAP(@0x0000, 4KB) ──EEPROM 标志──> IAP(@0x6D000, 12KB) ──W25Q 镜像──> App 区
外设侧：FingerPeriph / WristPeriph 经 Shared_OTA_BLE 走 BLE OTA
主控侧：云平台 → ESP32-C3(MQTT) → UART → Central_LCD(APP ota_*) → W25Q → IAP 搬移
```

## 说明与许可

- 示例代码中的 WiFi/MQTT 口令均为占位符，请按需替换；仓库不含任何个人/内网敏感配置（本机路径、调试器路径已剔除）。
- LVGL 为 MIT 许可；WCH 库/预编译 BLE 库的使用范围见各头文件版权声明（仅用于沁恒 MCU）。
- 编译产物（obj/、.elf/.hex/.map 等）、IDE 本机状态（.mrs/、.settings/）与字体源文件不入库，克隆后请自行 `make` 或重新编译生成。
