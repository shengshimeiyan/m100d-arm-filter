<div align="center">

# 🖨️ rastertolhplh

**Lenovo M100D / L100D 系列 GDI 打印机 ARM 原生 CUPS 过滤器**

[![License](https://img.shields.io/badge/License-GPLv2%2B-blue.svg)](LICENSE)
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
| **压缩** | JBIG T.85（ITU-T T.85，与原厂驱动相同） |
| **输出** | PJL 作业控制 + LHPLH GDI 页面数据 |

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

## PJL/LHPLH 输出格式

```
@PJL JOB\r\n
@PJL SET JOBATTR=DATE:...\r\n
@PJL SET JOBATTR=TIME:...\r\n
@PJL SET DUPLEX=OFF\r\n
@PJL SET RENDERMODE=GRAY\r\n
@PJL SET RESOLUTION=600\r\n
@PJL SET BITSPERPIXEL=1\r\n
@PJL SET COPIES=1\r\n
@PJL SET TONERMODE=0\r\n
@PJL ENTER LANGUAGE=LHPLH\r\n
[512 bytes 0x00 分隔符]
[JBIG T.85 压缩页面数据]
@PJL EOJ\r\n
```

## 资源占用

| 指标 | 数值 |
|------|------|
| 二进制大小 | 660KB（静态链接） |
| 每页内存 | ~2MB |
| 总安装大小 | ~1MB（过滤器 + PPD） |
| 编译依赖 | 仅 `gcc` + `make`（无需 libcups-dev！） |
| 运行时依赖 | **无**（完全静态链接） |

## 故障排查

| 现象 | 可能原因 | 解决方法 |
|------|---------|---------|
| 打印机无反应 | USB 权限问题 | `lsusb` 检查，确认 udev 规则 |
| 输出乱码/全黑 | JBIG 参数不对 | 调整源码中 L0、MX 值 |
| 打印一半停止 | ABORT 标记问题 | 可能需要改用 SDNORM 终止 |
| CUPS 报错 | 查看日志 | `cat /var/log/cups/error_log \| tail -50` |
| 找不到打印机 | CUPS 未识别 | `sudo lpinfo -l -v` 检查 USB URI |

## 技术细节

| 项 | 值 |
|----|----|
| 协议 | GDI（IEEE 1284 Device ID: CMD:GDI） |
| 页面语言 | LHPLH（`@PJL ENTER LANGUAGE=LHPLH`） |
| 压缩 | JBIG-KIT 2.1 T.85（GPLv2+） |
| PJL 分隔符 | 512 字节 0x00 |
| PJL 行尾 | `\r\n`（0x0D 0x0A） |
| CUPS Raster 读取器 | 内嵌（无需 libcups 依赖） |
| 链接方式 | `-lm -static`，零运行时 `.so` 依赖 |

## 逆向工程过程

1. 下载官方驱动 `L100_Series_drivers_Lin_20210511095611.7z`
2. 提取 `lnthr8zfilter.app`（已剥离符号）和 `liblnthr8zcl.so`（**未剥离**，655+ 导出符号）
3. 反汇编分析 `CreateNTDCMS()` 14 步流水线
4. 识别 PJL/LHPLH 协议格式、JBIG T.85 压缩参数、512 字节分隔符
5. 编写独立 CUPS 过滤器，内嵌 CUPS Raster 读取器
6. 测试验证：PJL 输出格式 ✓ / JBIG 压缩解压往返 ✓ / 半色调输出 ✓

## 许可证

GPLv2+ — JBIG-KIT 2.1 为 GPL 许可，本过滤器链接使用它。

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

## License

GPLv2+ — JBIG-KIT 2.1 is GPL licensed.
