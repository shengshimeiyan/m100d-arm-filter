<div align="center">

# 🖨️ rastertolhplh

**Lenovo M100D / L100D 系列 GDI 打印机 ARM 原生 CUPS 过滤器**

[![License](https://img.shields.io/badge/License-MIT%20%2B%20JBIG%20GPLv2-blue.svg)](LICENSE)
[![Architecture](https://img.shields.io/badge/Arch-aarch64%20%7C%20arm%20%7C%20x86__64-green.svg)]()
[![Static](https://img.shields.io/badge/Linking-Static%20%2B%20Standalone-orange.svg)]()

[English](#english) · [中文](#中文)

</div>

---

<a id="中文"></a>

## 背景

联想 M100D / L100D 等系列打印机使用 **GDI 协议**（主机端渲染），官方仅提供 x86-64 的闭源 CUPS 过滤器 `lnthr8zfilter.app`，在 ARM 设备（如骁龙 410 随身 WiFi、树莓派等）上完全无法使用。

本项目通过**逆向工程**分析官方 Windows 驱动（`LNTHR9Zfm.dll`）与 Debian 驱动抓包，编写了完全独立的 ARM 原生 CUPS 过滤器，**静态链接、零运行时依赖**。

**2026-09 v3.0.1**：修复 JBIG `SDRST` 条带状态处理（下部黑块根因），整页打印实物验证通过；新增 32 位 ARM（armel）支持，deb 包同时提供 arm64/armel。

**2026-08 已完整打通 CUPS 打印路径**：`lp` 命令直接打印文本/PDF 正确出纸，中文/英文渲染正常，居中无裁剪。

## 工作原理

```
  CUPS Raster          8×8 Bayer         JBIG T.85          PJL/LHPLH
  (灰度/1bpp/32bpp) ──→ 有序抖动半色调 ──→ 压缩编码 ──→   打印机数据
```

| 步骤 | 说明 |
|------|------|
| **输入** | CUPS Raster v2（支持 8bpp 灰度 / 1bpp W/K / 32bpp RGBA 灰度） |
| **半色调** | 8×8 Bayer 有序抖动 |
| **固件 workaround** | 顶部 128 行空白 padding + JBIG SDRST 状态修复 |
| **压缩** | JBIG T.85（jbigkit-2.1 修改版），**LRLTWO + MX=8 + SDRST** |
| **输出** | PJL 作业控制 + LHPLH 命令帧（@sj/@sp/@ep） |

## ⚠️ 固件 workaround（关键）

物理打印机实测发现的 M100D 固件特性：

1. **页面顶部必须 ≥1 个空白 stripe**（128 行 @600dpi）：否则打印机"轰鸣不出纸"。过滤器自动添加 128 行空白。
2. **SDRST 条带状态必须兼容 M100D 固件**：保留 arithmetic probability state，只重置条带本地参考状态。
3. **可打印区**：实测 M100D 的有效高度约为 **6755px**（600dpi）。输入过长时过滤器裁剪到该边界。

## 快速开始

### 在 ARM 设备上（3 条命令）

```bash
git clone https://github.com/shengshimeiyan/m100d-arm-filter.git
cd m100d-arm-filter
sudo bash install.sh
```

### 方式 2：deb 包安装（推荐）

从 [GitHub Releases](https://github.com/shengshimeiyan/m100d-arm-filter/releases) 下载
`m100d-rastertolhplh_3.0.1_arm64.deb`，在 ARM Debian/Ubuntu 上：

```bash
sudo dpkg -i m100d-rastertolhplh_3.0.1_arm64.deb
# 若提示依赖问题：
sudo apt-get -f install
```

32 位 ARM（armel/armv7）设备使用 `m100d-rastertolhplh_3.0.1_armel.deb`：

```bash
sudo dpkg -i m100d-rastertolhplh_3.0.1_armel.deb
```

Release 同时提供独立静态二进制：`rastertolhplh-aarch64-v3.0.1`、`rastertolhplh-arm32-v3.0.1`（直接放到 CUPS filter 目录即可）。

自动完成：安装 filter + PPD + USB udev 规则 + 重启 CUPS。

添加打印机并测试：

```bash
sudo lpadmin -p M100D -E -v usb://Lenovo/M100D -m lenovo-M100D-arm.ppd
echo "Hello M100D!" | lp -d M100D
```

### 配置为打印服务器（AirPrint）

```bash
sudo bash setup-print-server.sh
```

### 测试打印

```bash
echo "Hello M100D!" | lp -d M100D
```

## 从源码编译

### 原生编译（ARM 设备上）

修改版 jbigkit 源码已内置在 `jbig/`（基于 jbigkit-2.1，含 M100D SDRST 修复，GPLv2），无需下载：

```bash
sudo apt-get install gcc make
make
sudo make install
```

### 交叉编译（x86-64 → aarch64）

```bash
sudo apt-get install gcc-aarch64-linux-gnu
aarch64-linux-gnu-gcc -O2 -Wall -Wextra -std=c11 -Ijbig \
    -c jbig/jbig85.c -o jbig85.o
aarch64-linux-gnu-gcc -O2 -Wall -Wextra -std=c11 -Ijbig \
    -c jbig/jbig_ar.c -o jbig_ar.o
aarch64-linux-gnu-gcc -O2 -Wall -Wextra -std=c11 -Ijbig \
    -o rastertolhplh rastertolhplh.c jbig85.o jbig_ar.o -lm -static
```

32 位 ARM（armel，v3.0.1 deb 同款）：

```bash
sudo apt-get install gcc-arm-linux-gnueabi
arm-linux-gnueabi-gcc -O2 -Wall -Wextra -std=c11 -Ijbig \
    -c jbig/jbig85.c -o jbig85.o
arm-linux-gnueabi-gcc -O2 -Wall -Wextra -std=c11 -Ijbig \
    -c jbig/jbig_ar.c -o jbig_ar.o
arm-linux-gnueabi-gcc -O2 -Wall -Wextra -std=c11 -Ijbig \
    -o rastertolhplh rastertolhplh.c jbig85.o jbig_ar.o -lm -static
```

## 支持的打印机

| 型号 | USB PID | 状态 |
|------|---------|------|
| Lenovo M100D | 17ef:5444 | ✅ 实物验证（USB 直写 + CUPS lp 路径） |
| Lenovo M100DNA | — | 同协议 |
| Lenovo L100D | 17ef:5442 | 同协议 |
| Lenovo L100DW | 17ef:5443 | 同协议 |
| Lenovo M1520D | — | 同协议 |
| Lenovo M1688DW | — | 同协议 |

## PJL/LHPLH 协议格式（最终确认版）

### 完整输出结构

```
┌──────────────────────────────────────────────────┐
│  PJL 作业控制头                                   │
│  ├─ UEL + @PJL JOB NAME=PRINTER                  │
│  ├─ @PJL SET JOBATTR=HST:<hostname>              │
│  ├─ @PJL SET JOBATTR=USR:<username>              │
│  ├─ @PJL SET JOBATTR=DOC:<title>                 │
│  ├─ @PJL SET JOBATTR=DATE/TIME                   │
│  ├─ @PJL SET MEDIASOURCE=0                       │
│  ├─ @PJL SET DUPLEX=OFF                          │
│  ├─ @PJL SET MDPXS=0                             │
│  ├─ @PJL SET BITSPERPIXEL=1                      │
│  ├─ @PJL SET COPIES=<n>                          │
│  ├─ @PJL SET RESOLUTION=<600|1200>               │
│  ├─ @PJL SET RENDERMODE=GRAYSCALE                │
│  └─ @PJL ENTER LANGUAGE=LHPL                     │
├──────────────────────────────────────────────────┤
│  LHPLH @sj 命令帧 (64 bytes, 全零+XOR 校验)       │
├──────────────────────────────────────────────────┤
│  LHPLH @sp 命令帧                                 │
│  ├─ @sp 头部 (64 bytes)                          │
│  │   ├─ 前缀: 1b 4c 48 40 73 70 (ESC LH @sp)     │
│  │   ├─ SHORT[6]  = 0x0102 (page type, Windows)  │
│  │   ├─ DWORD[8]  = page_width (LE)              │
│  │   ├─ DWORD[12] = page_height (LE)             │
│  │   ├─ DWORD[16] = uncompressed_size (LE)       │
│  │   ├─ DWORD[20] = compressed_size = JBIG 总长 (LE) │
│  │   ├─ SHORT[42] = resolution                   │
│  │   ├─ SHORT[44] = 0x0834 (210.0mm, Windows)    │
│  │   ├─ SHORT[46] = 0x0b9a (297.0mm)             │
│  │   └─ byte[63]  = XOR 校验                     │
│  ├─ 标准 JBIG BIH (20 bytes, 不剥离！)            │
│  │   ├─ byte[4-7]  = width = 5120 (BE)           │
│  │   ├─ byte[8-11] = height (BE)                 │
│  │   ├─ byte[12-15] = L0 = 128                   │
│  │   ├─ byte[16] = MX = 8                        │
│  │   └─ byte[19] = options = 0x40 (LRLTWO)       │
│  └─ JBIG T.85 压缩数据 (每 stripe SDRST)          │
├──────────────────────────────────────────────────┤
│  LHPLH @ep 命令帧 (64 bytes, 全零+XOR 校验)       │
├──────────────────────────────────────────────────┤
│  \x1b%-12345X@PJL EOJ\r\n                         │
└──────────────────────────────────────────────────┘
```

### 关键协议细节（与 Windows 驱动逐字节对齐）

| 项目 | 值 |
|------|----|
| PJL 语言标识 | `LHPL` |
| @sj 帧 | `ESC LH@sj` + 全零 + XOR 校验（与 Windows 完全一致） |
| @sp hdr[6-7] | `0x0102`（Windows 实测，非 Debian 的 0x0100） |
| @sp hdr[44-45] | `0x0834` = 210.0mm（Windows，非 Debian 的 0x0833） |
| @sp 后 | **标准 JBIG BIH**（不剥离、非自定义头） |
| BIH byte16 | `0x08` = MX=8（与 Windows prn 一致） |
| BIH byte19 | `0x40` = LRLTWO（与 Windows prn 一致） |
| compressed_size | JBIG 总长（**含 BIH**，非 +20） |
| JBIG 终止 | SDRST (0xFF 0x03)，每 stripe 一个 |
| @ep 帧 | `ESC LH@ep` + **全零** + XOR 校验（移除 Debian 遗留 0x06/0x80） |

## 逆向工程依据

- **Windows 官方驱动** `LNTHR9Zfm.dll`（LenovoPrint Z26 系列安装包）完整反汇编，见 [`analysis/windows-driver/README.md`](analysis/windows-driver/README.md)：
  - JBIG 编码器：硬编码 MX=8、options=0x340（LRLTWO）
  - 每 stripe SDRST
  - 页面顶部 4 个空白 stripe（512 行）
  - LHPL 帧结构（ESC LH@sp、XOR 校验）
- **Windows prn 抓包**（`captures/windows-original-driver.prn`）：字节级对比基准
- **实物打印测试**：定位页测量打印机物理映射、网格页验证居中/无裁剪

## 资源占用

| 指标 | 数值 |
|------|------|
| 二进制大小 | ~830KB（aarch64 静态链接，v3.0.1） |
| 每页内存 | ~2MB |
| 运行时依赖 | **无**（完全静态链接） |

## 测试验证（实物 M100D）

| 测试 | 结果 |
|------|------|
| Windows prn 原始文件直写 | ✅ 出纸 |
| 空白页 | ✅ 出纸 |
| 网格定位页 | ✅ 居中、无裁剪、均匀 |
| 中文/英文文字页 | ✅ 正常 |
| 顶部 padding 128 vs 512 | ✅ 128 足够 |
| CUPS `lp` 打印文本 | ✅ 正常（32bpp 提取 + 极性修复后） |
| 整页 JBIG（SDRST 修复编码器，v3.0.1） | ✅ 整页正常，无下部黑块 |
| OpenStack aarch64 VM：IJS 客户端 → 过滤器 → fd 100 | ✅ BIH、SDRST、@sj/@sp/@ep 与 Windows 字节对齐 |

> 2026-09 在 OpenStack Debian 11 aarch64 VM（Tailscale SSH）上验证：源码从 v3.0.1 tag 解压后单 `make` 即出包，IJS 服务器模式接收 4780×1 RGB 合成光栅后输出 627 字节 PJL/LHPLH/JBIG，BIH 字段 `width=5120 height=129 L0=128 MX=8 options=0x40` 与 Windows prn 基准（除 Y0）逐字节一致。

## 故障排查

| 现象 | 可能原因 | 解决方法 |
|------|---------|---------|
| `dpkg: unknown compression for member 'control.tar.zst'` | Release 上的 .deb 是 zstd 压缩，Debian 11 `dpkg-deb` 不支持 | 已重新打包为 xz 压缩并重新上传；如仍有缓存请清 `~/.cache/` 后重下 |
| 轰鸣不出纸 | 页面顶部无空白 stripe | 过滤器已内置 128 行顶部空白，使用 ≥ v3.0.1 |
| 页面下部大块黑域/乱码 | JBIG SDRST 条带状态与固件不符 | v3.0.1 已修复（保留算术状态，仅重置参考行） |
| 黑底白字 | CUPS NegativePrint 极性 | filter 强制标准极性（已修复） |
| 内容压缩到左侧 | 32bpp 输入未提取 | filter 自动检测提取（已修复） |
| 右边/底部被裁 | 画布超可打印区 | 过滤器已内置 4768px 可打印宽度处理 |

可选环境变量：`PAPER_CENTER_PX`（500–5000，默认 2392）覆盖内容居中的水平像素偏移。

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| v3.0.1 | 2026-09-03 | JBIG `SDRST` 修复：保留 arithmetic probability state，仅重置条带参考行；新增 ARM32（armel）deb；`jbig/` 内置修改版 jbigkit 源码，`make` 即可构建；Release .deb 重新打包为 xz 压缩以兼容 Debian 11 `dpkg-deb` |
| v3.0.0 | 2026-08 | 最终协议参数（MX=8、LRLTWO、标准 BIH）；CUPS `lp` 全路径实物验证 |
| v2.x | — | 早期不完整逆向（MX=64/TPBON 等），已被 v3.0.0 取代 |

---

<a id="english"></a>

## English

**rastertolhplh** — a standalone ARM-native CUPS filter for Lenovo M100D/L100D GDI printers.

Fully reverse-engineered from the official Windows driver (`LNTHR9Zfm.dll`) and verified on physical hardware:

- **Protocol**: PJL + LHPLH frames (`ESC LH@sj/@sp/@ep`), byte-aligned with the Windows driver
- **JBIG**: LRLTWO + MX=8, standard BIH kept, SDRST per stripe
- **Firmware workarounds** (discovered via physical testing):
  - 128 blank rows at page top (built-in)
  - JBIG SDRST workaround: preserve arithmetic state and reset only stripe-local reference state
  - Printable height 6755px (600dpi)
- **CUPS input**: 8bpp gray, 1bpp W/K, or 32bpp RGBA-gray (auto-detected)
- **Static binary**: zero runtime dependencies, works on 512MB RAM devices

```bash
echo "Hello M100D!" | lp -d M100D
```

See [`REVERSE-ENGINEERING-STATUS.md`](REVERSE-ENGINEERING-STATUS.md) and [`analysis/windows-driver/README.md`](analysis/windows-driver/README.md) for details.
