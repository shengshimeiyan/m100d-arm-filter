# rastertolhplh — Lenovo M100D ARM-native CUPS Filter

Reverse-engineered CUPS filter for Lenovo M100D / L100D / M1520D / M1688DW series GDI printers, enabling printing on ARM (aarch64) devices like Snapdragon 410.

The official Lenovo driver only provides x86-64 binaries. This filter replaces the proprietary `lnthr8zfilter.app` with a standalone, statically-linked ARM-native binary — **zero runtime dependencies**.

## How It Works

```
CUPS Raster → 8×8 Bayer Dither → JBIG T.85 Compress → PJL/LHPLH Output → USB → M100D
```

- **Input**: CUPS Raster v2 (grayscale, 8-bit)
- **Halftone**: 8×8 Bayer ordered dither (memory-efficient, good for text)
- **Compression**: JBIG T.85 (ITU-T T.85, same as original driver)
- **Output**: PJL job wrapper + LHPLH GDI protocol (Lenovo's proprietary page language)

## Quick Start

### 1. Transfer to ARM device

```bash
# From your PC (same network)
scp -r m100d-arm-filter/ user@ARM_DEVICE_IP:/tmp/
```

### 2. Install filter + PPD

```bash
cd /tmp/m100d-arm-filter
sudo bash install.sh
```

### 3. Set up as print server (optional)

```bash
sudo bash setup-print-server.sh
```

This configures CUPS network sharing + AirPrint, so all devices on your LAN can print.

### 4. Test

```bash
echo "Hello M100D!" | lp -d M100D
```

## Build from Source

### Native (on ARM device)

```bash
# Install build deps
sudo apt-get install gcc make libcups2-dev libcupsimage2-dev

# Download JBIG-KIT 2.1
wget https://www.cl.cam.ac.uk/~mgk25/jbigkit/download/jbigkit-2.1.tar.gz
tar xzf jbigkit-2.1.tar.gz

# Build
make

# Install
sudo make install
```

### Cross-compile (from x86-64)

```bash
# Install cross-compiler
sudo apt-get install gcc-aarch64-linux-gnu

# Cross-compile
make CROSS=aarch64-linux-gnu-
```

## Supported Printers

| Model | USB PID | Status |
|-------|---------|--------|
| Lenovo M100D | 5444 | Primary target |
| Lenovo M100DNA | — | Same protocol |
| Lenovo L100D | 5442 | Same protocol |
| Lenovo L100DW | 5443 | Same protocol |
| Lenovo M1520D | — | Same protocol |
| Lenovo M1688DW | — | Same protocol |

## PJL/LHPLH Output Format

```
@PJL JOB\r\n
@PJL SET DUPLEX=OFF\r\n
@PJL SET RENDERMODE=GRAY\r\n
@PJL SET RESOLUTION=600\r\n
@PJL SET BITSPERPIXEL=1\r\n
@PJL SET COPIES=1\r\n
@PJL SET TONERMODE=0\r\n
@PJL ENTER LANGUAGE=LHPLH\r\n
[512 bytes 0x00 separator]
[JBIG T.85 compressed page data]
@PJL EOJ\r\n
```

## Resource Usage

- **Binary size**: 660KB (statically linked)
- **RAM per page**: ~2MB (well within 512MB device limit)
- **Storage**: ~1MB total (filter + PPD)

## Print Server Mode

The `setup-print-server.sh` script configures your ARM device as a CUPS print server:

| Client | Connection |
|--------|-----------|
| iPhone/iPad | AirPrint auto-discovery |
| macOS | AirPrint auto-discovery |
| Windows | IPP / WSD network printer |
| Android | IPP app (e.g. "CUPS Print") |
| Linux | `lpadmin -p M100D -E -v ipp://IP:631/printers/M100D` |

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| Printer no response | USB permissions | Check `lsusb`, udev rules |
| Garbled output | JBIG parameters | Adjust L0/MX in source |
| Print stops halfway | ABORT marker | May need proper SDNORM termination |
| CUPS error | Check logs | `cat /var/log/cups/error_log \| tail -50` |

## Technical Details

- **Protocol**: GDI (CMD:GDI in IEEE 1284 Device ID)
- **Language**: LHPLH (`@PJL ENTER LANGUAGE=LHPLH`)
- **Compression**: JBIG-KIT 2.1 T.85 (GPLv2+)
- **PJL separator**: 512 bytes of 0x00
- **PJL line endings**: `\r\n` (0x0D 0x0A)
- **CUPS raster reader**: Built-in (no libcups dependency for cross-compilation)

## License

GPLv2+ — JBIG-KIT 2.1 is GPL, and the filter links against it.

## Disclaimer

This is a reverse-engineered implementation. The PJL/LHPLH protocol was derived from analysis of the official `lnthr8zfilter.app` binary. Compatibility with actual printers should be verified on real hardware. Use at your own risk.
