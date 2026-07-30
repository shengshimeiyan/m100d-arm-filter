<div align="center">

# 🖨️ rastertolhplh

**Lenovo M100D / L100D 系列 GDI 打印机 ARM 原生 CUPS 过滤器**

[![License](https://img.shields.io/badge/License-MIT%20%2B%20JBIG%20GPLv2-blue.svg)](LICENSE)
[![Architecture](https://img.shields.io/badge/Arch-aarch64%20%7C%20x86__64-green.svg)]()
[![Static](https://img.shields.io/badge/Linking-Static%20%2B%20Standalone-orange.svg)]()

[English](#english) · [中文](#中文)

</div>

---

<a id="中文"></a>

## 背景

联想 M100D / L100D 等系列打印机使用 **GDI 协议**（主机端渲染），官方仅提供 x86-64 的闭源 CUPS 过滤器 `lnthr8zfilter.app`，在 ARM 设备（如骁龙 410 随身 WiFi、树莓派等）上完全无法使用。

本项目通过**逆向工程**分析官方驱动的 PJL/LHPLH 协议，编写了完全独立的 ARM 原生 CUPS 过滤器，**静态链接、零运行时依赖**，在 512MB 内存 + 4GB 存储的设备上也能流畅运行。

## 工作原理

```
  CUPS Raster          8×8 Bayer         JBIG T.85          PJL/LHPLH
  (灰度光栅)    ──→   有序抖动半色调  ──→   压缩编码    ──→   打印机数据
  8-bit/pixel         1-bit/pixel        T.85 标准压缩      GDI 协议输出
```

| 步骤 | 说明 |
|------|------|
| **输入** | CUPS Raster v2（灰度，8-bit） |
| **半色调** | 8×8 Bayer 有序抖动（内存高效，文本效果好） |
| **压缩** | JBIG T.85（ITU-T T.85，MX=64，与原厂驱动相同） |
| **输出** | PJL 作业控制 + LHPLH 命令帧（@sj/@sp/@ep） |

## 快速开始

### 在 ARM 设备上（3 条命令）

```bash
git clone https://github.com/shengshimeiyan/m100d-arm-filter.git
cd m100d-arm-filter
sudo bash install.sh
```

`install.sh` 自动完成：
1. ✅ 检查/安装 `gcc` + `make`
2. ✅ 下载 JBIG-KIT 2.1 源码
3. ✅ 编译静态二进制（~660KB）
4. ✅ 安装过滤器 + PPD + USB 规则
5. ✅ 重启 CUPS

### 配置为打印服务器（AirPrint）

```bash
sudo bash setup-print-server.sh
```

一键配置 CUPS 网络共享 + AirPrint，局域网内所有设备可直接打印：

```
                    ┌──────────────────────┐
  📱 iPhone ──────→│                      │
  (AirPrint)       │   骁龙410 / 树莓派   │    USB     ┌─────────┐
                   │   CUPS 打印服务器     │─────────→│  M100D  │
  💻 Windows ─────→│   (rastertolhplh)    │           │  打印机  │
  (IPP)            │                      │           └─────────┘
                   └──────────────────────┘
```

| 客户端 | 连接方式 |
|--------|---------|
| iPhone / iPad | AirPrint 自动发现 |
| macOS | AirPrint 自动发现 |
| Windows | IPP / WSD 网络打印机 |
| Android | IPP APP（如 "CUPS Print"） |
| Linux | `lpadmin -p M100D -E -v ipp://IP:631/printers/M100D` |

### 测试打印

```bash
echo "Hello M100D!" | lp -d M100D
```

## 从源码编译

### 原生编译（ARM 设备上）

```bash
sudo apt-get install gcc make

wget https://www.cl.cam.ac.uk/~mgk25/jbigkit/download/jbigkit-2.1.tar.gz
tar xzf jbigkit-2.1.tar.gz

make          # 编译静态二进制
sudo make install   # 安装
```

### 交叉编译（x86-64 → aarch64）

```bash
sudo apt-get install gcc-aarch64-linux-gnu

# 编译 JBIG 对象
aarch64-linux-gnu-gcc -O2 -Ijbigkit-2.1/libjbig -c jbigkit-2.1/libjbig/jbig85.c -o jbig85.o
aarch64-linux-gnu-gcc -O2 -Ijbigkit-2.1/libjbig -c jbigkit-2.1/libjbig/jbig_ar.c -o jbig_ar.o

# 编译过滤器
aarch64-linux-gnu-gcc -O2 -Ijbigkit-2.1/libjbig -o rastertolhplh \
    rastertolhplh.c jbig85.o jbig_ar.o -lm -static
```

## 支持的打印机

| 型号 | USB PID | 状态 |
|------|---------|------|
| Lenovo M100D | 5444 | 主要目标 |
| Lenovo M100DNA | — | 同协议 |
| Lenovo L100D | 5442 | 同协议 |
| Lenovo L100DW | 5443 | 同协议 |
| Lenovo M1520D | — | 同协议 |
| Lenovo M1688DW | — | 同协议 |

## PJL/LHPLH 协议格式

### 完整输出结构

```
┌──────────────────────────────────────────────────┐
│  PJL 作业控制头                                   │
│  ├─ UEL + @PJL JOB NAME=PRINTER                  │
│  ├─ @PJL SET JOBATTR=HST:<hostname>              │
│  ├─ @PJL SET JOBATTR=USR:<username>              │
│  ├─ @PJL SET JOBATTR=DOC:<title>                 │
│  ├─ @PJL SET JOBATTR=DATE:<MM/DD/YYYY>           │
│  ├─ @PJL SET JOBATTR=TIME:<HH:MM:SS>             │
│  ├─ @PJL SET DUPLEX=<ON|OFF>                     │
│  ├─ @PJL SET MEDIASOURCE=0                       │
│  ├─ @PJL SET RENDERMODE=GRAYSCALE                │
│  ├─ @PJL SET RESOLUTION=<600|1200>               │
│  ├─ @PJL SET BITSPERPIXEL=1                      │
│  ├─ @PJL SET COPIES=<n>                          │
│  └─ @PJL ENTER LANGUAGE=LHPL                     │
├──────────────────────────────────────────────────┤
│  LHPLH @sj 命令帧 (64 bytes)                      │
│  ├─ 前缀: 1b 4c 48 40 73 6a (ESC LH @sj)         │
│  ├─ byte[6]=0x01, byte[8]=copies                  │
│  └─ byte[63]=XOR校验 (bytes 0-62)                 │
├──────────────────────────────────────────────────┤
│  LHPLH @sp 命令帧 (变长)                           │
│  ├─ @sp 头部 (64 bytes)                           │
│  │   ├─ 前缀: 1b 4c 48 40 73 70 (ESC LH @sp)     │
│  │   ├─ SHORT[6]  = 0x0102 (page type, byte6=0x02=JBIG) │
│  │   ├─ DWORD[8]  = 5120 (printer physical width, 32-bit LE) │
│  │   ├─ DWORD[12] = page_height (32-bit LE)       │
│  │   ├─ DWORD[16] = uncompressed_size (32-bit LE) │
│  │   ├─ DWORD[20] = compressed_size+20 (32-bit LE, 含 BIE 头) │
│  │   ├─ DWORD[24] = compressed_size+20 (32-bit LE, 重复) │
│  │   ├─ SHORT[42] = resolution (16-bit LE)        │
│  │   ├─ SHORT[44] = 0x0834=2100 (纸张宽度, 0.1mm) │
│  │   ├─ SHORT[46] = 0x0b9a=2970 (纸张高度, 0.1mm) │
│  │   └─ byte[63]=XOR校验 (bytes 0-62)             │
│  ├─ JBIG 参数头 (20 bytes, big-endian)             │
│  │   ├─ DWORD[0] = 0x00000100 (flags)             │
│  │   ├─ DWORD[1] = 5120 (printer physical width, BE) │
│  │   ├─ DWORD[2] = page_height (BE)               │
│  │   ├─ DWORD[3] = L0=128 (stripe height, BE)     │
│  │   └─ DWORD[4] = 0x08000040 (MY=2048, MX=64, BE) │
│  └─ JBIG T.85 压缩数据 (SDRST 终止, 无 BIE 头包装) │
├──────────────────────────────────────────────────┤
│  LHPLH @ep 命令帧 (64 bytes)                      │
│  ├─ 前缀: 1b 4c 48 40 65 70 (ESC LH @ep)         │
│  ├─ bytes 6-62 全部为零 (匹配 Windows 驱动)       │
│  └─ byte[63]=XOR校验 (bytes 0-62)                 │
├──────────────────────────────────────────────────┤
│  \x1b%-12345X@PJL EOJ\r\n                         │
└──────────────────────────────────────────────────┘
```

### 关键协议细节

| 项目 | 值 |
|------|----|
| PJL 行尾 | `\r\n`（0x0D 0x0A） |
| PJL 语言标识 | `LHPL`（不是 LHPLH） |
| PJL SET 顺序 | MEDIASOURCE→DUPLEX→MDPXS→BITSPERPIXEL→COPIES→RESOLUTION→RENDERMODE→PCNT |
| PJL EOJ | `\x1b%-12345X@PJL EOJ\r\n`（含 UEL 前缀） |
| LHPLH 命令帧大小 | @sj/@ep 固定 64 字节，@sp 变长 |
| @sp byte[6] | 0x02（JBIG 压缩标志），非 0x00 |
| @sp 头部字段 | 偏移 8+ 为 **32-bit LE DWORD**（非 16-bit） |
| @sp page_width | 5120（打印机物理宽度，非 CUPS Raster 宽度） |
| @sp compressed_size | JBIG 数据大小 + 20（含 BIE 头大小，即使不发送 BIE 头） |
| @sp 纸张尺寸 | SHORT[44]=0x0834=2100, SHORT[46]=0x0b9a=2970（0.1mm 单位） |
| @sp JBIG 参数头 | 自定义 LHPLH 格式，5 个 big-endian DWORD（非标准 BIE） |
| JBIG 压缩参数 | MX=64, MY=2048, L0=128, TPBON=0（DWORD[4]=0x08000040） |
| JBIG 终止标记 | SDRST (0xFF 0x03)，替换 jbig85 的 SDNORM (0xFF 0x02) |
| @ep 命令帧 | bytes 6-62 全部为零（非 0x06/0x80） |
| XOR 校验 | `byte[63] = bytes[0..62]` 逐字节异或 |
| 打印区域宽度 | 固定 5120 像素（600 DPI），内容左对齐，剩余填零 |

## 资源占用

| 指标 | 数值 |
|------|------|
| 二进制大小 | 660KB（aarch64 静态链接） |
| 每页内存 | ~2MB |
| 总安装大小 | ~1MB（过滤器 + PPD） |
| 编译依赖 | 仅 `gcc` + `make`（无需 libcups-dev！） |
| 运行时依赖 | **无**（完全静态链接） |

## 测试验证

本过滤器通过以下方法验证：

| 方法 | 说明 | 结果 |
|------|------|------|
| **直接运行** | `rastertolhplh` 处理 CUPS Raster → 分析输出结构 | ✅ PJL/LHPLH/XOR 全部正确 |
| **Windows 驱动对比** | GDI 抓包 Windows 驱动输出，逐字节对比 | ✅ PJL/@sj/@ep/@sp头部完全匹配（8项修正后） |
| **CUPS 完整流程** | `lp` 命令 → CUPS → 过滤器 → 打印机 | ✅ 打印机出纸（JBIG 编码内容待修复） |
| **USB 直送** | `cat output.bin > /dev/usb/lp0` | ✅ Windows 驱动输出可正常打印 |

验证项：PJL 命令格式 ✅ · @sj/@sp/@ep 命令帧 ✅ · XOR 校验 ✅ · @sp 32-bit LE 字段 ✅ · JBIG 参数头 (L0=128, MY:MX=0x08000040) ✅ · SDRST 终止 ✅ · @PJL EOJ (含 UEL) ✅ · compressed_size+20 ✅ · page_width=5120 ✅

⚠️ **已知问题**：JBIG 编码输出与 Windows 驱动存在差异（空白条纹 2B vs 4B、Stripe 0 大小 686B vs 129B），打印机出纸但内容不正确，正在调试中。

## 故障排查

| 现象 | 可能原因 | 解决方法 |
|------|---------|---------|
| 打印机无反应 | USB 权限问题 | `lsusb` 检查，确认 udev 规则 |
| 打印机无反应 | CUPS 用了 libusb 后端 | 改用 usblp 内核驱动 + `file:///dev/usb/lp0` 或 `socket://localhost:9100` + socat |
| 输出乱码/全黑 | JBIG 参数不对 | 调整源码中 L0、MX、MY 值 |
| 打印一半停止 | ABORT 标记问题 | 已修复为 SDRST (0xFF 0x03) 终止 |
| 内容不正确（灰色纹路） | JBIG 编码差异 | 已知问题，正在调试中 |
| CUPS 报错 | 查看日志 | `cat /var/log/cups/error_log \| tail -50` |
| 找不到打印机 | CUPS 未识别 | `sudo lpinfo -l -v` 检查 USB URI |
| 打印机红绿灯闪烁 | 发送了错误数据 | 断电重启打印机（拔电源线 10 秒） |
| PWG Raster 不识别 | cspace 18/19 未处理 | 已修复，支持 PWG gray/RGB |

## 技术细节

| 项目 | 值 |
|------|------|
| 协议 | GDI（IEEE 1284 Device ID: CMD:GDI） |
| 页面语言 | LHPLH（`@PJL ENTER LANGUAGE=LHPL`） |
| 压缩 | JBIG-KIT 2.1 T.85（GPLv2+），MX=64, MY=2048, L0=128, SDRST 终止 |
| 命令帧格式 | ESC LH @sj/@sp/@ep，64 字节固定头 + XOR 校验 |
| @sp 头部字段 | 32-bit LE DWORD（page_width=5120/height/sizes） |
| @sp JBIG 子头 | 自定义 LHPLH 格式，5 个 BE DWORD（非标准 BIE） |
| @sp byte[6] | 0x02（JBIG 压缩标志） |
| 页面宽度 | 5120（打印机物理宽度，600 DPI） |
| PJL 行尾 | `\r\n`（0x0D 0x0A） |
| CUPS Raster 读取器 | 内嵌（无需 libcups 依赖），支持 PWG Raster (RaS2/RaS3) |
| NegativePrint | PPD 中 `NegativePrint true`，半色调需适配反转像素值 |
| 链接方式 | `-lm -static`，零运行时 `.so` 依赖 |

## 逆向工程过程

1. 下载官方驱动 `L100_Series_drivers_Lin_20210511095611.7z`
2. 提取 `lnthr8zfilter.app`（已剥离符号）和 `liblnthr8zcl.so`（**未剥离**，655+ 导出符号）
3. 反汇编分析 `CreateNTDCMS()` 14 步流水线
4. 在 x86-64 机器上安装原厂驱动，通过 CUPS `file:/` 后端抓包获取完整协议输出
5. 识别 LHPLH 命令帧格式（@sj/@sp/@ep + XOR 校验）、JBIG T.85 压缩参数（MX=64, L0=128）
6. 确认 @sp 头部字段为 **32-bit LE**（非 16-bit）——原厂小测试页值恰好能存入 16-bit，全页输出才暴露
7. 确认 JBIG 终止标记为 **SDRST (0xFF 0x03)**——原厂使用 SDRST，ABORT (0xFF 0x04) 非有效终止
8. 页面宽度从 CUPS Raster 头部动态读取——不再硬编码 4768，适配 PPD 配置
9. 编写独立 CUPS 过滤器，内嵌 CUPS Raster 读取器
10. 三种方法测试验证：直接运行 ✅ / 与原厂对比 ✅ / CUPS 完整流程 ✅
11. **Windows 驱动 GDI 抓包**：通过 `gdi32.CreateDCW` + `lpszOutput` 参数捕获 Windows 驱动完整输出
12. **8 项协议修正**：对比 Windows 驱动输出发现 8 处差异（@sp byte6=0x02、纸张宽度 0x0834、MY:MX=0x08000040、@ep 全零、PJL EOJ 含 UEL、PJL SET 顺序、MDPXS/PCNT 参数、compressed_size+20）
13. **JBIG 编码调试**：打字机能出纸但内容不正确，JBIG 条纹对比发现与 Windows 驱动存在显著差异（待修复）

## 许可证

- **本项目代码**：MIT License
- **JBIG-KIT 2.1**（jbig85.c / jbig_ar.c）：GPLv2+

编译后的二进制包含 JBIG-KIT 代码，因此分发时整体适用 GPLv2。源码层面本项目采用 MIT 许可。

## 免责声明

本项目为逆向工程实现。PJL/LHPLH 协议通过分析官方 `lnthr8zfilter.app` 二进制推导得出，与实际打印机的兼容性需在真实硬件上验证。使用风险自负。

---

<a id="english"></a>

## Background

Lenovo M100D / L100D series printers use the **GDI protocol** (host-based rendering). The official driver only provides a closed-source x86-64 CUPS filter `lnthr8zfilter.app`, making it completely unusable on ARM devices (Snapdragon 410, Raspberry Pi, etc.).

This project reverse-engineers the official driver's PJL/LHPLH protocol and implements a fully standalone ARM-native CUPS filter — **statically linked, zero runtime dependencies**, running smoothly even on 512MB RAM + 4GB storage devices.

## Quick Start

```bash
git clone https://github.com/shengshimeiyan/m100d-arm-filter.git
cd m100d-arm-filter
sudo bash install.sh
```

## Build from Source

```bash
# Only gcc + make needed (no libcups-dev!)
sudo apt-get install gcc make
wget https://www.cl.cam.ac.uk/~mgk25/jbigkit/download/jbigkit-2.1.tar.gz
tar xzf jbigkit-2.1.tar.gz
make
sudo make install
```

## Print Server (AirPrint)

```bash
sudo bash setup-print-server.sh
```

Enables AirPrint/IPP sharing — all devices on your LAN can print directly.

## Supported Printers

Lenovo M100D, M100DNA, L100D, L100DW, M1520D, M1688DW (all share the same LHPLH GDI protocol).

## PJL/LHPLH Protocol Format

### Output Structure

```
┌──────────────────────────────────────────────────┐
│  PJL Job Control Header                           │
│  ├─ UEL + @PJL JOB NAME=PRINTER                  │
│  ├─ @PJL SET JOBATTR=HST/USR/DOC/DATE/TIME       │
│  ├─ @PJL SET DUPLEX/MEDIASOURCE/RENDERMODE/...   │
│  └─ @PJL ENTER LANGUAGE=LHPL                     │
├──────────────────────────────────────────────────┤
│  LHPLH @sj Command Frame (64 bytes)               │
│  ├─ Prefix: 1b 4c 48 40 73 6a (ESC LH @sj)       │
│  ├─ byte[6]=0x01, byte[8]=copies                  │
│  └─ byte[63]=XOR checksum (bytes 0-62)            │
├──────────────────────────────────────────────────┤
│  LHPLH @sp Command Frame (variable length)        │
│  ├─ @sp Header (64 bytes)                         │
│  │   ├─ SHORT[6]  = 0x0102 (page type, byte6=0x02=JBIG) │
│  │   ├─ DWORD[8]  = 5120 (printer physical width, 32-bit LE) │
│  │   ├─ DWORD[12] = page_height (32-bit LE)       │
│  │   ├─ DWORD[16] = uncompressed_size (32-bit LE) │
│  │   ├─ DWORD[20] = compressed_size+20 (32-bit LE, incl. BIE header) │
│  │   ├─ DWORD[24] = compressed_size+20 (32-bit LE, repeated) │
│  │   ├─ SHORT[42] = resolution (16-bit LE)        │
│  │   ├─ SHORT[44] = 0x0834=2100 (paper width, 0.1mm) │
│  │   ├─ SHORT[46] = 0x0b9a=2970 (paper height, 0.1mm) │
│  │   └─ byte[63]=XOR checksum (bytes 0-62)        │
│  ├─ JBIG Parameters Header (20 bytes, big-endian) │
│  │   ├─ DWORD[0] = 0x00000100 (flags)             │
│  │   ├─ DWORD[1] = 5120 (printer physical width, BE) │
│  │   ├─ DWORD[2] = page_height (BE)               │
│  │   ├─ DWORD[3] = L0=128 (stripe height, BE)     │
│  │   └─ DWORD[4] = 0x08000040 (MY=2048, MX=64, BE) │
│  └─ JBIG T.85 Compressed Data (SDRST termination, no BIE header) │
├──────────────────────────────────────────────────┤
│  LHPLH @ep Command Frame (64 bytes)               │
│  ├─ Prefix: 1b 4c 48 40 65 70 (ESC LH @ep)       │
│  ├─ bytes 6-62 all zeros (matching Windows driver) │
│  └─ byte[63]=XOR checksum (bytes 0-62)            │
├──────────────────────────────────────────────────┤
│  \x1b%-12345X@PJL EOJ\r\n                         │
└──────────────────────────────────────────────────┘
```

### Key Protocol Details

| Item | Value |
|------|-------|
| PJL line ending | `\r\n` (0x0D 0x0A) |
| PJL language identifier | `LHPL` (not LHPLH) |
| PJL SET order | MEDIASOURCE→DUPLEX→MDPXS→BITSPERPIXEL→COPIES→RESOLUTION→RENDERMODE→PCNT |
| PJL EOJ | `\x1b%-12345X@PJL EOJ\r\n` (with UEL prefix) |
| LHPLH command frame size | @sj/@ep fixed 64 bytes, @sp variable |
| @sp byte[6] | 0x02 (JBIG compression flag), not 0x00 |
| @sp header fields | 32-bit LE DWORDs at offset 8+ (not 16-bit) |
| @sp page_width | 5120 (printer physical width, not CUPS raster width) |
| @sp compressed_size | JBIG data size + 20 (includes BIE header size, even though BIE header is not sent) |
| @sp paper dimensions | SHORT[44]=0x0834=2100, SHORT[46]=0x0b9a=2970 (0.1mm units) |
| @sp JBIG sub-header | Custom LHPLH format, 5 BE DWORDs (not standard BIE) |
| JBIG compression params | MX=64, MY=2048, L0=128, TPBON=0 (DWORD[4]=0x08000040) |
| JBIG termination marker | SDRST (0xFF 0x03), replacing jbig85's SDNORM (0xFF 0x02) |
| @ep command frame | bytes 6-62 all zeros (not 0x06/0x80) |
| XOR checksum | `byte[63] = XOR of bytes[0..62]` |
| Printable area width | Fixed 5120 pixels (600 DPI), content left-aligned, remaining zero-filled |

## Testing

The filter has been verified through the following methods:

| Method | Description | Result |
|--------|-------------|--------|
| **Direct run** | Process CUPS Raster → analyze output structure | ✅ All checks pass |
| **Windows driver comparison** | GDI capture of Windows driver output, byte-by-byte comparison | ✅ PJL/@sj/@ep/@sp headers match exactly (after 8 fixes) |
| **CUPS full pipeline** | `lp` → CUPS → filter → printer | ✅ Printer outputs paper (JBIG encoding content needs fix) |
| **USB direct** | `cat output.bin > /dev/usb/lp0` | ✅ Windows driver output prints correctly |

Verified items: PJL format ✅ · @sj/@sp/@ep command frames ✅ · XOR checksums ✅ · @sp 32-bit LE fields ✅ · JBIG params (L0=128, MY:MX=0x08000040) ✅ · SDRST termination ✅ · @PJL EOJ (with UEL) ✅ · compressed_size+20 ✅ · page_width=5120 ✅

⚠️ **Known issue**: JBIG encoding output differs from Windows driver (blank stripes 2B vs 4B, Stripe 0 size 686B vs 129B). Printer outputs paper but content is incorrect — under investigation.

## License

- **This project's code**: MIT License
- **JBIG-KIT 2.1** (jbig85.c / jbig_ar.c): GPLv2+

Compiled binaries include JBIG-KIT code, so distribution of binaries is subject to GPLv2. The source code itself is MIT licensed.

## Disclaimer

This is a reverse-engineered implementation. The PJL/LHPLH protocol was derived by analyzing the official `lnthr8zfilter.app` binary. Compatibility with actual printers must be verified on real hardware. Use at your own risk.
