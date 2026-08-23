# Windows 官方驱动 LNTHR9Zfm.dll 反汇编分析

> 闭源二进制，仅供互操作性研究使用。

## 文件来源

- **SHA256**: `c07990d0278de2e15e3b783db17aa0b2bd85cf22d4b2c52cbb9a4f9a8cae334a`
- **PE32+ DLL (x86-64)**, 333 336 字节
- 来源: https://lenovo-upload.oss-cn-beijing.aliyuncs.com/drivers/LenovoPrint_Z26_Series_20250805173007.exe
- 安装包持久化位置: `/root/.cache/m100d-re/LenovoPrint_Z26_Series_20250805173007.exe`
- 7z 提取路径: `Lenovo_M100_M1520_M1688_M7360_series/Drivers/HB/Win_87VistaXP/x64/English/LNTHR9Zcmmn/LNTHR9Zfm.dll`
- **本仓库目录下的 DLL 只用于反汇编参考，不应提交到 git**

## 关键导出函数

| 函数 | 地址 | 角色 |
|---|---|---|
| `ZStartJob` | – | 任务初始化 |
| `ZStartDoc201707` | 0x180006880 | 文档开始（含 LHPL preamble 写入） |
| `ZStartDocEx` | – | 文档开始（扩展） |
| `ZStartDocSecureEx` | – | 安全模式文档开始 |
| `ZStartPage` | 0x180004a00 | 页面初始化（计算 stripe_height） |
| `ZOutputStrip` | 0x180005bd0 | stripe 数据输出 |
| `ZEndPage` | 0x180005e10 | 页面结束（触发 JBIG 编码） |

## LHPL preamble（ZStartDoc201707 写入）

`ESC LH@sj` 6 字节序列：

| 偏移 | 字节 | ASCII |
|---|---|---|
| +0 | 0x1b | ESC |
| +1 | 0x4c | L |
| +2 | 0x48 | H |
| +3 | 0x40 | @ |
| +4 | 0x73 | s |
| +5 | 0x6a | j |

完整 LHPL 头还包括：
- `@PJL JOB NAME=PRINTER`
- `@PJL ENTER LANGUAGE=LHPL`
- `@PJL EOJ`
- 加密/校验混合（见 0x180006d54–0x180006d97）

## JBIG 编码器内部结构（不是标准 jbig-kit！）

字段偏移（不同于 jbig85.h）：

| 偏移 | 含义 | 备注 |
|---|---|---|
| 0x00 | planes / data ptr | |
| 0x04 | x0 / L0 rows | |
| 0x08 | y0 | |
| 0x0c | y0 高位 | |
| 0x10 | plane count | |
| 0x14 | options byte 0 (BIH byte 0) | |
| 0x18 | mx_or_other (BIH byte 1) | |
| 0x1c | AT length / MX | jbg85 标准结构中此位置是 mx，但 Windows DLL 用作 AT 长度 |
| 0x3c | L0 part / `and 0xf` mask | jbg85 标准里此位置是 l0 |
| 0x40 | **options + flags**（BIH byte 19 source） | **硬编码 0x340** |
| 0x44 | **MX = 8**（硬编码） | |
| 0x48 | **MY**（外部传入，0..255） | |
| 0x54 | constant table 1 | jbig85 标准 TP/AT 表 |
| 0x5c | constant table 2 | jbig85 标准 AT 表 |
| 0x64 | plane 指针数组 | 多维 plane 数组 |
| 0x6c | output buffer | 0x1024-byte per-plane 输出缓冲 |
| 0x74 | are encoder state | |
| 0x7c | **data_out callback** | |
| 0x84 | callback user data | |
| 0x94 | pending BIH buffer ptr | 首次非 0 时写完即清零 |
| 0x9c-0x9f | BIH 余字节（xd/yd 高字节） | |

## 关键函数签名

### fcn.180001060 — CFreeJbigCodec::Open (codec 初始化)

```c
void fcn_180001060(
    int x0,               // rcx (实际是 stripe_height × 8)
    int y0,               // edx
    void *planes_array,   // r8
    int mx_or_plane_cnt,  // r9
    int arg_120h,         // [rsp+0x20]
    int arg_128h,         // [rsp+0x28]
    int arg_130h          // [rsp+0x30]
);
```

调用 `fcn_180001ce0` 初始化 state，调 `fcn_180001e80` 设置 options，调 **`fcn_180001ea0` 设置 stripe 参数**（含硬编码 r8d=0x340）。

### fcn.180001ce0 — jbg85_init core

```c
void fcn_180001ce0(jbig_state *s, uint32_t x0, uint32_t y0,
                   int plane_count, int l0_unused, void *planes_array);
```

**硬编码字段**:
- `[s+0x44] = 8` ← **MX = 8** ✓
- `[s+0x48] = 0` ← MY
- `[s+0x3c] = 3`
- `[s+0x40] = 0x1c` (28 = 0b00011100)
- `[s+0x4] = x0`
- `[s+0x8] = y0`
- `[s+0xc] = y0`
- `[s+0x10] = plane_count`
- 分配 4×plane_count 字节 → [s+0x34]
- 分配 8×plane_count 字节 → [s+0x2c]
- 分配 plane 数据 → [s+0x2c][i]
- 分配 0x1024 × plane_count 字节 → [s+0x6c]
- 分配 4 × plane_count 字节 → [s+0x4c]
- 分配 output line buffer（长度 1 字节 × x0 单元） → [s+0x8c]

### fcn.180001e80 — jbg85_enc_options

```c
void fcn_180001e80(jbig_state *s, uint32_t options);
```

- 限制 options ≤ 0x1f
- `[s+0x00] = options`
- `[s+0x14] = 0` (BIH byte 0)
- `[s+0x18] = options` (BIH byte 1)
- 调 c40 算 AT 长度 → [s+0x1c]

### fcn.180001ea0 — jbg85_enc_init (finalize)

```c
void fcn_180001ea0(jbig_state *s, uint32_t edx, uint32_t r8d, uint32_t r9d,
                   uint32_t arg_28h, uint32_t arg_30h);
```

写入字段:
- `[s+0x3c] = edx` if edx ≤ 0xf
- `[s+0x40] = r8d` if r8d ≥ 0     ← **r8d 硬编码 0x340**
- `[s+0x1c] = r9d` if r9d ≠ 0
- `[s+0x44] = arg_28h` if 0..0x7f
- `[s+0x48] = arg_30h` if 0..0xff

**关键**: `0x340 = 832 = 0b1101000000`，低 7 位 `0x40 = JBG_LRLTWO`。
BIH byte 19 = `[s+0x40] & 0x7f = 0x40` ✓

### fcn.180001c40 — AT length 计算

```c
void fcn_180001c40(jbig_state *s);
```

计算 adaptive template 长度（基于 [s+0] 和 [s+8]），结果存 [s+0x1c]。最少 2，最多到 (128 >> [s+8])。

### fcn.1800038e0 — jbg85_enc_encode (BIE 输出主函数)

调用流程:
1. `[s+0x3c] &= 0xf`（mask L0 part）
2. 检查 `[s+0x40]` flags
3. 限制 `[s+0x44]` (MX) ≤ 127，若 `[s+0x44] == 0` 清零
4. 分配 plane 二级数组 [s+0x64]
5. **输出 BIH**（20 字节到 [rsp+0x58]）：
   - byte 0  = [s+0x14]（options/DL）
   - byte 1  = [s+0x18]（mx/D）
   - byte 2  = [s+0x10]（planes）
   - byte 3  = 0
   - byte 4-7  = [s+0xc] BE （L0）
   - byte 8-11 = xd BE
   - byte 12-15 = [s+0x1c] BE（AT 长度）
   - byte 16 = [s+0x44]（MX）
   - byte 17 = [s+0x48]（MY）
   - byte 18 = [s+0x3c]（L0 part / ATMOVE）
   - byte 19 = [s+0x40] & 0x7f（options）
6. 输出 ABI header（如果 options & 0x7 == 6 = LRLTWO|TPDON|TPBON）
7. 主循环 stripe 编码，每 stripe 输出 SDE/SDNORM markers
8. 末 stripe 输出 NEWLEN marker

## stripe_height 计算路径（ZStartPage 0x180005090–0xe7）

```c
cx = stripe_height
eax = r10d
cdq
edx &= 0x3f
eax += edx
eax &= 0x3f
eax -= edx
// if eax != 0:
sub cx, ax
add cx, 0x40
mov word [0x1801410b8], cx
```

**stripe_height 强制舍入到 64 的倍数**。

## ZEndPage 调用 jbg 编码（ZEndPage@0x1800060c2）

```c
fcn.180001060(
    [0x1801410b8] * 8,         // stripe_height × 8 → ecx
    [0x1801410f4],             // edx
    [r13 + r8*8 + 0x141220],   // r8 = encoder state struct
    [0x180141114],             // r9 (实际 = 8 from ZStartDoc201707 写入)
    [0x180140ea4],             // [rsp+0x20] = 0x80 (128)
    [0x180141100],             // [rsp+0x28] = ebx = 0
    [r13 + r8*8 + 0x141220]    // [rsp+0x30]
);
```

**关键确认**:
- `[0x180141100]`（options）由 `ZStartDoc201707@0x180006e5a` 的 ebx 写入，ebx 在 `0x180006ace` 处 `xor ebx, ebx` 清零，**options = 0**
- **但 `[s+0x40] = 0x340` 在 fcn.180001ea0 中硬编码**（不依赖 caller options！）
- BIH byte 19 始终 = `0x340 & 0x7f = 0x40` = LRLTWO

## 字段对应表（与 jbig-kit BIH 字节序）

| BIH byte | 字节来源 | jbig 标准字段 |
|---|---|---|
| 0 | [s+0x14] | DL \| D |
| 1 | [s+0x18] | P \| 0 |
| 2 | [s+0x10] | 0 |
| 3 | 0 | 0 |
| 4-7 | [s+0xc] BE | L0 |
| 8-11 | xd BE | Xd |
| 12-15 | [s+0x1c] BE | Yd (实际是 AT length!) |
| 16 | [s+0x44] | MX |
| 17 | [s+0x48] | MY |
| 18 | [s+0x3c] | options high |
| 19 | [s+0x40] & 0x7f | options low |

**重要差异**: Windows DLL 的 BIH 字段顺序与 jbig-kit 标准不同！
- 标准 L0 → Windows byte 4-7（[s+0xc]，但 [s+0xc] = y0！）
- 标准 Yd → Windows byte 12-15（[s+0x1c]，但 [s+0x1c] = AT length！）

所以 Windows DLL 的 BIH 是 **私有格式**，但 jbgtopbm85 仍能解码（因为按 BIE 解码不需要 Yd 字段正确）。

## fix6 BIH 比对

我们 fix6 输出:
```
00000100 00001400 00001aa2 00000080 08000040
```

Windows 驱动输出（windows-original-driver.prn）:
```
00000100 00001400 00001aa4 00000080 08000040
```

**唯一差异**: y0 byte 8-11 = `00001aa2` (6818) vs `00001aa4` (6820)
多 2 行的 stripe —— Windows 在最后一 stripe 加了 2 行 padding。

## 关键全局变量（LX globals）

| 地址 | 含义 | 来源 |
|---|---|---|
| 0x180141100 | options mask | ZStartDoc201707@0x180006e5a, ebx (=0) |
| 0x1801410fc | options mask2 | ZStartDoc201707@0x180006e60, ebx (=0) |
| 0x180140ea0 | options mask3 | ZStartDoc201707@0x180006e66, ebx (=0) |
| 0x1801415c4 | something | ZStartDoc201707@0x180006ade, ebx (=0) |
| 0x1801410f0 | something | ZStartDoc201707@0x180006ae4, ebx (=0) |
| 0x180141114 | stripe_count = 8 | ZStartDoc201707@0x180006e46, hardcoded 8 |
| 0x180140ea4 | Mx_param = 128 | ZStartDoc201707@0x180006e50, hardcoded 0x80 |
| 0x1801410b8 | stripe_height | ZStartPage@0x1800050e7, rounded to 64 |
| 0x180140e8e | some flag byte | read at 0x180006a30 |
| 0x180140a6a | state byte | checked 0x4 at 0x180006ece |
| 0x180140e78 | dpi/scaling | ZStartDoc201707@0x180006c67 |
| 0x180140e7a | another DPI | ZStartDoc201707@0x180006c90 |
| 0x1801410b4 | error code | ZStartDoc201707@0x180006a93 |
| 0x180140a70 | device info | ZStartDoc201707@0x180006b05 (0x41e byte copy) |
| 0x180140fb0 | user name string | ZStartDoc201707@0x180006e1a |
| 0x180141120 | formatted string | ZStartDoc201707@0x180006da2 |
| 0x180140c74 | string table | ZStartDoc201707@0x180006b54 |

## 结论

1. **Windows 驱动用 jbig85 的 JBIG 编码**，但字段偏移与标准 jbig85.h 不同
2. **硬编码 MX = 8**（与 fix6 一致）
3. **硬编码 options = 0x340**（含 LRLTWO bit）
4. **BIH 是私有布局**，但解码端按 jbig85 标准解析仍能解码（因 byte 16, 17, 19 位置正确）
5. **stripe_height 强制 64 倍数**
6. **y0 + 2 行 padding** 是 Windows 驱动的特征

这与我们的 fix6 输出一致——除了最后 +2 行 stripe padding。

## ZEndPage 完整 JBIG 编码流程

ZEndPage 在每页输出时执行：
1. `EndNTDCMS(halftone_obj)`（`fcn.1800014b0`）— 完成 halftone 处理
2. **4 次循环** (CMYK planes)：每次
   - `fcn.180001000(state_ptr, &out_count)` — 清零输出计数
   - `fcn.180001060(...)` — 触发 JBIG 编码
3. 写 64-byte LHPL frame 到输出（含 JBIG 数据）

```c
for (int plane_idx = 0; plane_idx < 4; plane_idx++) {
    state_index = plane_buffer[plane_idx];   // [rsp+0x40 + plane_idx]
    if (state_index >= 4) break;
    
    fcn_180001000(state_ptr[state_index], &out_count);  // reset
    jbig_state_t* state = state_array[state_index];
    
    fcn_180001060(
        state,
        stripe_height * 8,           // ecx (x0 in JBIG = stripe_height × 8)
        page_width,                  // edx (y0 in JBIG = page_width)
        state,                       // r8
        plane_count,                 // r9d = 8 (always)
        [rsp+0x20] = 0x80,          // MY
        [rsp+0x28] = options,       // 0x340
        [rsp+0x30] = another_mask   // 0
    );
}
```

**注意**：`fcn.180001060` 的参数语义与标准 jbig85_init 不同！

| 参数 | 标准 jbig85 | Windows 私有 |
|---|---|---|
| x0 | image width | **stripe_height × 8** |
| y0 | image height | **page_width** |
| planes | plane count | **always 1** |

**所以 `[s+0x4] = stripe_height*8, [s+0x8] = page_width` — 与标准相反！**

## 完整 JBIG state 布局（最终确认）

| 偏移 | 内容 | 来源 |
|---|---|---|
| 0x00 | x0 = stripe_height × 8 | fcn.180001060 ecx |
| 0x04 | x0 (dup) | 同上 |
| 0x08 | y0 = page_width | fcn.180001060 edx |
| 0x0c | y0 (dup) | 同上 |
| 0x10 | plane_count = 1 | fcn.180001ce0 硬编码 |
| 0x14 | options byte0 | fcn.180001e80 |
| 0x18 | options byte1 | 同上 |
| 0x1c | AT length | fcn.180001c40 |
| 0x24 | **data_out callback** = 0x180001020 | fcn.180001ce0 |
| 0x3c | L0 part | fcn.180001ea0 (r8d=0x340) |
| 0x40 | **options + flags = 0x340** | 同上 |
| 0x44 | **MX = 8** (硬编码) | fcn.180001ce0 |
| 0x48 | MY = 0 (默认) | 同上 |

## fcn.180001020 — data_out callback 完整分析

```c
int data_out_callback(uint8_t* data, uint32_t len) {
    HANDLE hFile = [0x180040a30];        // LHPL output handle (set by ZStartDoc201707)
    uint32_t* pCount = [0x180040a28];    // bytes written counter
    
    DWORD written;
    BOOL ok = WriteFile(hFile, data, len, &written, NULL);
    *pCount += len;
    return 0;
}
```

**关键发现**：`[0x180040a30]` 是 Windows LHPL 文件句柄，由 `ZStartDoc201707` 设置。

## fcn.1800075f0 — Stripe padding 函数

```c
void fcn_1800075f0(halftone_obj* obj) {
    uint32_t* pending = &obj->field_100c;
    uint32_t row = obj->field_8;          // current row count
    
    if (*pending > 0) {
        // 把 pending 行 flush 到 plane data
        flush_rows(obj, *pending);
        *pending = 0;
    }
    
    uint32_t mod = row & 0x3f;           // row % 64
    if (mod != 0) {
        // **添加 (64 - mod) 行 padding**
        uint8_t zero_buf[64] = {0};
        write_rows(obj, zero_buf, 64 - mod);
        row += (64 - mod);
        obj->field_8 = row;
    }
    
    delete obj;  // fcn.180008414
}
```

**这是 Windows 驱动实现 "stripe 64 倍数 padding" 的位置！** 与 fix6 workaround **思路完全相同** —— 但 Windows 是在 halftone 输出缓冲里做，不是 JBIG stripe level。

## LHPL Frame 64 字节结构

每个 LHPL frame = 64 字节，前 6 字节是 "ESC LH@sp" preamble：

```
Offset  Bytes              Meaning
+0x00   0x1b 0x4c 0x48    "ESC LH"
+0x03   0x40 0x73 0x70    "@sp"
+0x06   byte              [0x180140a6f] = page flag
+0x07   byte              [0x180140a6b] = page flag
+0x08   dword             page param (1 or 4 × bytes)
+0x0c   word              page_width
+0x10   dword             pixel_count = (1 or 4) × stripe_height × page_width
+0x14   dword             pixel_count (dup)
+0x18   dword             pixel_count (dup)
+0x1c   dword             pixel_count (dup) -- xor area
+0x20   word              0x258 (dpi? height) or 0x4b0 (if [0x180140e88]==1)
+0x22   word              [0x180140a6c] OR [0x1801410f8] (resolution)
+0x24   word              [0x1801410f8] OR [0x180140a6c] (resolution swap)
+0x26   56 bytes          zero padding
+0x3f   byte              XOR checksum of bytes [0..0x3e] (每 3 字节一组)
```

## LHPL Preamble 字节确认

> **重要更正**：之前总结说 `ESC LH@sj`，实际是 **`ESC LH@sp`**（最后字节 0x70='p'，不是 0x6a='j'）。

## Color Mode 表（LHPL 解码）

```c
// ZStartPage@0x180004c5c 处的查找表
// r8b = color mode (0..15)
const uint16_t dpi_height_table[] = {
    0, 2790, 2970, 2100, 1480, 2570, 1820, 2670,
    2600, 3560, 1480, 1280, 0, 0, 0, 0
};
const uint16_t dpi_width_table[] = {
    0, 2160, 2100, 1480, 1050, 1820, 1280, 1840,
    1850, 2160, 2100, 1820, 0, 0, 0, 0
};
```

**对照 fix6 用 5120×6818 @ 600dpi：LHPL 600dpi 在表中没有，但 #11=1280（接近）。**

## halftone 对象 (Halftoner.cpp)

- 结构大小 **0x1034 字节**（4148 bytes）
- `[obj+0x1024] = "GRAY"` 标识（LE = 0x59415247）
- `[obj+0x1010..0x1018]` = 颜色深度/格式
- `[obj+0x1004]` = bps (bits per sample)
- `[obj+0x100c]` = pending flush row count

### vtable 方法映射

| 方法 | fcn 包装器 | ZEndPage 调用顺序 |
|---|---|---|
| InitNTDCMS | fcn.180001240 | 1 (在 ZStartPage 之后) |
| CreateNTDCMS | fcn.180001280 | 6 (flush) |
| StartAdjustJob | fcn.180001300 | 2 (颜色调色) |
| UpdatePerryTonerSave | fcn.180001460 | 3 (色调节能) |
| StartFilterJob | fcn.1800012b0 | 4 (output buf) |
| UpdateScreenTonerTRC | fcn.1800013e0 | 5 (颜色 TRC) |
| NTDCMS | fcn.1800014e0 | ZOutputStrip 批量模式 |
| EndNTDCMS | fcn.1800014b0 | 7 (flush & close) |

## 结论（完整）

我们已掌握 Windows 官方驱动的全部关键路径：

1. ✅ **LHPL preamble**：`ESC LH@sp` 6 字节
2. ✅ **LHPL frame**：64 字节（含 XOR checksum + 分辨率 + pixel count）
3. ✅ **JBIG encoder**：自定义结构，硬编码 MX=8, options=0x340 (LRLTWO)
4. ✅ **stripe_height 强制 64 倍数**：fcn.1800075f0 在 halftone 输出缓冲层做 padding
5. ✅ **stripe padding 思路与 fix6 workaround 一致** —— Windows 也认为 64 倍数是必要的
6. ✅ **4 plane CMYK 但实际只写 1 plane 灰度**（printer 是单色激光）
7. ✅ **ZOutputStrip 两种路径**：单行 halftone (fcn.180004180) 和批量 (fcn.1800014e0)
8. ✅ **halftone 对象结构**：`.\Halftoner.cpp`，含 InitNTDCMS/CreateNTDCMS 等方法

**最有价值的发现**：Windows 驱动 **也做 stripe 64 倍数 padding**（fcn.1800075f0）—— 这确认了 fix6 workaround 的方向正确。
