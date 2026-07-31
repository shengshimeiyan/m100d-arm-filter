# M100D 打印机抓包数据

## 文件说明

| 文件 | 大小 | 说明 |
|------|------|------|
| `debian-original-driver-output.raw` | 4.1KB | Debian Linux 原始驱动输出（100行测试页，4768×100） |
| `tpbon-fixed-output.raw` | 60KB | TPBON 修复后的 ARM 过滤器输出（完整A4页，4760×6818） |

## 数据格式

所有文件都是完整的打印机输出数据，包含：
1. PJL 头部（`@PJL JOB` + `@PJL SET` 参数）
2. LHPLH 命令帧（`ESC LH @sj` + `ESC LH @sp` + `ESC LH @ep`）
3. PJL 结尾（`@PJL EOJ`）

## LHPLH 命令帧结构

```
@sj: 64 bytes (作业设置)
@sp: 64 bytes (页头) + 20 bytes (BIE子头) + JBIG压缩数据
@ep: 64 bytes (页结束)
```

## BIE 子头格式（LHPLH 自定义，非标准 JBIG）

```
DWORD[0] = 0x00000100 (标志)
DWORD[1] = page_width (大端)
DWORD[2] = page_height (大端)
DWORD[3] = stripe_height = 128 (大端)
DWORD[4]:
  byte[16] = options (0x08=TPBON, 0x00=无TPBON)
  byte[17] = MY (0)
  byte[18] = 0 (保留)
  byte[19] = MX (0x40=64)
```

⚠️ 注意：LHPLH BIE 子头格式与标准 JBIG BIE header 不同！
- 标准 JBIG: byte[16]=MX, byte[18]=options
- LHPLH: byte[16]=options, byte[19]=MX

## Debian 驱动 vs Windows 驱动对比

| 参数 | Debian 驱动 | Windows 驱动 |
|------|-----------|-------------|
| @sp byte[6] | 0x00 | 0x02 |
| page_width | 4768 | 5120 |
| BIE byte[16] | 0x00 (无TPBON) | 0x08 (TPBON) |
| BIE byte[19] | 0x40 (MX=64) | 0x40 (MX=64) |
| @ep byte[8] | 0x06 | 0x00 |
| @ep byte[15] | 0x80 | 0x00 |
| PJL EOJ | `@PJL EOJ` | `\x1b%-12345X@PJL EOJ` |
| PJL SET 顺序 | 不同 | MEDIASOURCE→DUPLEX→MDPXS→BITSPERPIXEL→COPIES→RESOLUTION→RENDERMODE→PCNT |
| offset 44-45 | 0x0833=2099 | 0x0834=2100 |

## 分析工具

```bash
# 查找 @sp 命令
xxd file.raw | grep "1b4c 4840 7370"

# 提取 JBIG 数据 (从 @sp offset 84 到 @ep)
python3 -c "
import struct
data = open('file.raw','rb').read()
sp = data.find(b'\x1bLH@sp')
ep = data.find(b'\x1bLH@ep', sp)
bie = data[sp+64:sp+84]
jbig = data[sp+84:ep]
print(f'BIE header: {bie.hex()}')
print(f'JBIG data: {len(jbig)} bytes')
print(f'DWORD[4] = 0x{struct.unpack(\">I\", bie[16:20])[0]:08X}')
print(f'byte[16]=0x{bie[16]:02X} byte[19]=0x{bie[19]:02X}')
"

# 用 jbig85 解码 (需要 jbigkit)
# 注意：需要用标准 JBIG BIE header 替换 LHPLH BIE header
```
