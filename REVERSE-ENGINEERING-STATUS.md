# Lenovo M100D reverse-engineering status

Updated: 2026-08-23 (major correction)

## TL;DR — current status (encoder verified correct; local round-trip perfect)

**The filter's complete JBIG stream is now verified internally correct.**
The earlier "stripe 11→12 loss-of-sync" was a **decode-side artifact of
mis-reading the BIE byte layout** (treating `byte16=0x40` as `JBG_LRLTWO`
two-line template when it is actually `MX=64`). With the correct layout the
stream round-trips **perfectly** through the stock decoder.

The two "phantom" defects that dominated previous rounds are both disproven:

1. **The project `jbigkit-2.1/libjbig/jbig85.c` had been accidentally
   overwritten with a copy of `rastertolhplh.c`.** Every earlier "encoder
   modification" (SDRST / reuse_st / ring-buffer resets) was edited in that
   copy and **never entered the build** — the real encoder was always the
   unmodified stock `jbig85.c`. The file has been restored to the pristine
   stock source and re-verified.

2. **The "halftone input discrepancy" (64712 B / ~375-column shift) was caused
   by reading the CUPS raster at the wrong content offset.** The CUPS raster
   header is a fixed **1796 bytes** (`CUPS_HEADER_SIZE`, including the 4-byte
   sync). Earlier Python analysis read content at offset **1821** (25 bytes
   too late), producing a phantom horizontal shift. With the correct
   offset 1796 my Python Bayer halftone matches the filter's encoder input
   **byte-for-byte (0 diff over all 6820 rows)**.

## Verified facts (this round)

### A. The genuine jbig85 BIH/LHPLH BIE layout (from stock `jbig85.c`)

The 20-byte BIH written by `jbg85_new_bih` and mirrored by the filter's
LHPLH `@sp` sub-header is:

| byte | meaning            | test-page value |
|------|--------------------|-----------------|
| 0    | DL = 0             | 00              |
| 1    | D  = 0             | 00              |
| 2    | P  = 1             | 01              |
| 3    | reserved           | 00              |
| 4–7  | width  (BE)        | 00 00 14 00 = 5120 |
| 8–11 | height (BE)        | 00 00 1a a4 = 6820 |
| 12–15| l0/stripe (BE)     | 00 00 00 80 = 128 |
| 16   | **MX**             | **40 = 64**      |
| 17   | MY                 | 00              |
| 18   | order              | 00              |
| 19   | **options & (LRLTWO\|VLENGTH\|TPBON)** | **00 = noTPBON** |

So **`byte16 = MX`** and **`byte19 = options`** — the opposite of what
earlier rounds assumed (`byte16=0x40` was misread as `JBG_LRLTWO`). The
filter's BIE `00000100 00001400 00001aa4 00000080 40000000` is **byte-identical
to the stock encoder's BIH**.

### B. Filter output ≡ stock output (modulo last-byte SDNORM→SDRST)

For the same real input:
- Filter JBIG data (3957 B after BIE) == stock `pbmtojbg85 -p 0 -m 64 -s 128`
  data (3957 B), **except the final byte**: filter `0x03` (SDRST), stock
  `0x02` (SDNORM). The filter rewrites only the **last** SDNORM → SDRST.
- The filter's LHPLH BIE == stock's standard BIH (see A).

### C. Stock local round-trip is lossless

`enc_input.bin` (the filter's real encoder input) → `pbmtojbg85` →
`jbgtopbm85` → **0 diff bytes** (5120×6820). Both the SDNORM and SDRST
last-byte variants decode to **0 diff**. So the encoder, BIE, halftone input
and data are internally correct and self-consistent.

### D. jbg85_enc_options: correct call

`jbg85_enc_options(&s, options, l0, mx)` with `(0, 0, 64)`:
- `options = 0` (no TPBON — matches Debian/Lenovo driver)
- `l0 = 0` (keep stock default 128)
- `mx = 64` (MX=64)

This is the call currently in the source.

## What is (and is not) established

**Established**
- The CUPS raster content offset is 1796 (fixed header).
- The Bayer 8×8 halftone + `negative_print? (v>th) : ((255-v)>th)` formula,
  `left_pad=176` reproduce the filter's encoder input exactly (0 diff).
- The BIE layout (byte16=MX, byte19=options) and the filter's values
  (MX=64, options=0) are correct.
- The JBIG data round-trips perfectly through stock — the stream itself is
  valid and self-consistent.

**Not yet established**
- Whether the **printer firmware** (a custom decoder that does not read the
  LHPLH BIE the same way stock does) actually agrees. This can only be
  settled by a paper test on the ARM device, which is currently unreachable
  (SSH timeout). The known-good Windows driver emits **SDRST at each stripe
  boundary**; our stream emits SDNORM for all but the last stripe. If the
  firmware's stripe-boundary handling differs from stock, this is the most
  plausible remaining interop difference.

## Historical context / red herrings (kept for record)

1. `jbg85_enc_options` argument-order bug: `(0, 64, 0)` put 64 into `l0`.
   Fixed to `(0, 0, 64)`.
2. `mx=64` vs `mx=0` with `tx=0` produce byte-identical arithmetic streams;
   the earlier size difference came from `l0=64` vs `l0=128`.
3. The `(128,192)` "stripe-12 desync" decode result that drove the whole
   "jbig85.c has a bug" investigation was obtained with the BIE mis-read
   (byte16=0x40 treated as LRLTWO). It is not a real encoder defect.

## Pending (blocked on ARM connectivity)

1. Regenerate / deploy the freshly-built binary to the ARM device.
2. Paper test of the full A4 page (text / grid / gradient / diagonal).
3. If the firmware still loses sync at a stripe boundary, the next candidate
   is emitting **SDRST at every stripe boundary** (matching the Windows
   driver) instead of only the last one.

## Hardware test

- Target: Snapdragon 410 device, Debian 11, aarch64.
- Printer: Lenovo M100D over `/dev/usb/lp0` (also `socket://localhost:9100` via p910nd).
- The statically linked ARM64 filter builds and runs on the target.
- Bypass test (raw LHPLH to `/dev/usb/lp0` / port 9100) feeds a full page.

## Build / reproduce (local validation on x86-64, no printer needed)

```sh
make                                  # native build
./rastertolhplh 1 test title 1 "" /tmp/tp2.ras > /tmp/rdbg.lhplh
# stock self-check (input must be the 1bpp halftone, offset 1796):
/root/jbigkit-2.1/pbmtools/pbmtojbg85 -p 0 -m 64 -s 128 /tmp/filter_input.pbm /tmp/stock_e1.jbg
/root/jbigkit-2.1/pbmtools/jbgtopbm85 /tmp/stock_e1.jbg /tmp/stock_d1.pbm
# 0-diff vs enc_input.bin
```

## Build artefacts (current)

- `rastertolhplh` (x86_64) — rebuilt 2026-08-23, TEMP DEBUG dump removed.
- `rastertolhplh-aarch64` — stale; must be rebuilt from current source and
  re-deployed (old aarch64 predates the BIE/offset verification).

## Stale sections below this point

The prior version of this document claimed a real arithmetic encoder/decoder
desync in `jbig85.c` at the stripe 11→12 boundary and proposed patching
`jbig85.c` (ring-buffer resets, `reuse_st`, blank-stripe `4b c6` handling).
All of those were investigated against an **overwritten copy** of `jbig85.c`
and/or a **mis-read BIE**, and are superseded by the verified facts above.
`jbig85.c` is pristine stock and must not be patched.

## Firmware-side workaround (2026-08-23 round)

### Problem
The complete-page test (`tp2`) prints only the top section (~y0-1280) on
the physical M100D. The bottom (black band paragraphs, diagonal lines,
gradient, text) is missing even though the firmware decoder claims
successful completion. Extensive bisection (diagnostic pages A through K)
narrowed the firmware bug to: **the decoder desyncs when a JBIG stripe
starts with an all-white first line that is followed by non-white content
in the same stripe**. This affects stripe 11 (`y1408-1535`) in `tp2`:
`y1408-1499` are white, `y1500+` is a black band row.

### Encoder is correct
- Width: `5120` bits (`@sp` width + jbig BIH `x0`), matching the
  Windows-prn capture.
- Height: `6818` (matches `cupsHeight`).
- BIE options: `JBG_LRLTWO` (`byte19 = 0x40`) + `MX = 8` (`byte16 = 0x08`).
- `SDRST` at every stripe boundary (matching Windows driver).
- Local round-trip (`jbgtopbm85` of our encoded `.jbg`) reproduces the
  half-toned input byte-for-byte over the bulk of the page; the few
  remaining diff rows are purely from our intentional workaround patch.

### Workaround (currently deployed)
The filter now does **two passes** over the raster:

1. **Pass 1**: halftone every row from the raster into a malloc'd
   `page_height × lhplh_bpl` bit buffer (centred in the 5120-bit width).
2. **Pass 1.5**: for every stripe whose first row is all-white **AND**
   whose stripe has some non-white content later, copy the stripe's
   first non-white row onto the first row. Empty stripes are left as
   empty stripes (this also removes the empty-stripe post-processing
   that collapsed `4b c6 00 ff 03` to `4b c6 ff 03`).
3. **Pass 2**: feed the modified buffer into `jbg85_enc_lineout()`.

This forces the printer's decoder to never see a `SDRST`-followed-by-
SDRST stripe reset with a 128-row top blank stripe; full arithmetic reset is required by the validated M100D path.

### Build artefacts (this round)
- `rastertolhplh-aarch64` md5 `b82de42d0242620ea1132f3cd10590a1`
  — deployed on `100.94.110.126:/usr/lib/cups/filter/rastertolhplh`.
- `/tmp/fix6_tp2.lhplh` on the printer — the complete tp2 page after
  the workaround patch (waiting on user feedback).

### Still open
- The patch is **content-altering**: stripes that had a white first row
  followed by black rows now have an extra black row at the top of the
  stripe (the first non-white row is duplicated). Visually this should
  look correct because the duplicate row's content is the same black
  pixel pattern that already appears in the stripe. We will only know
  once the user confirms the printout.
- If this workaround also fails, the next experiments to try are
  (a) SDNORM (already tested, failed), (b) tweak the per-stripe first
  line differently (e.g. white reference only, no black duplicate),
  (c) patch the firmware decoder (out of scope).

## 2026-08 后续：LNTHR9Zfm.dll 完整反汇编

完整反汇编参见 [`analysis/windows-driver/README.md`](analysis/windows-driver/README.md)。

### 关键发现

1. **jboss options 硬编码 = 0x340** (`fcn.180001ea0` 调用现场 `r8d = 0x340`)
   - `0x340 & 0x7f = 0x40 = JBG_LRLTWO` ← BIH byte 19
   - 与 fix6 输出 byte19 = 0x40 完全一致
2. **MX = 8 硬编码** (`[s+0x44] = 8` 在 `fcn.180001ce0` 内)
3. **MY 默认 0**（由调用方传入）
4. **BIH 是私有布局**，但 BIH byte 4-7 (L0) = `[s+0xc]` (y0 重复)；byte 12-15 = AT length
5. **stripe_height 强制 64 倍数**（ZStartPage 0x1800050d3）
6. **windows-original-driver.prn y0 = 6820**，fix6 y0 = 6818：差异是 Windows 在最后一 stripe 加了 2 行 padding

### 下载方式

`https://www.lenovoimage.com/index.php/services/servers_drivers?cat_id=2&key_words=M100D&...`
中的 `data-href="https://lenovo-upload.oss-cn-beijing.aliyuncs.com/drivers/LenovoPrint_Z26_Series_20250805173007.exe"`

## 2026-08 完整 ZEndPage → JBIG 编码链路

### fcn.180001020 = JBIG data_out callback
```c
int data_out_callback(uint8_t* data, uint32_t len) {
    HANDLE hFile = [0x180040a30];        // LHPL 文件句柄
    WriteFile(hFile, data, len, &written, NULL);
    return 0;
}
```

### fcn.1800075f0 = Stripe 64 倍数 padding（最关键发现）
```c
// 在 halftone 输出缓冲层做 padding：
if (row % 64 != 0) {
    write_zero_rows(obj, 64 - (row % 64));
}
```
**与 fix6 workaround 思路一致** —— Windows driver 也要求 stripe_height 是 64 倍数。

### 4 plane CMYK JBIG encoder
ZEndPage 主循环处理 4 plane，但 M100D 实际只写 1 plane (灰度)。
plane_buffer[] = [3, 0, 0, 0] 表示 K-plane active，其他 CMY 被跳过。

### LHPL frame 64 字节结构（已在 README.md 详述）

### 字段对应修正
**之前混淆**：BIH byte 4-7 (L0) 实际对应 Windows state 的 y0 (duplicate)；BIH byte 12-15 (AT length) 用 state 的 [s+0x1c]。

完整分析见 [`analysis/windows-driver/README.md`](analysis/windows-driver/README.md)。

## 2026-08 最终成果：CUPS 完整打印路径打通 ✅

### 实测打印机物理特性（定位页测量）
- **画布原点**：纸张 (6.55mm, 11.08mm)，600dpi 精确映射
- **可打印区**：4651×6755px（左右边距 6.55mm，顶部 11.08mm）
- **画布 BIH 5120×6946 超出纸张**（右侧 13mm、底部 8mm 被裁）→ 必须缩放到可打印区

### 最终修复清单
1. **可打印区适配**：`left_pad` 基于可打印宽 4651（`PRINTABLE_WIDTH` env 可调）
2. **顶部 padding**：128 行空白（`TOP_PAD` env 可调），固件要求开头 ≥1 空白 stripe
3. **白首行 workaround 修复**：原来检查"整 stripe 全白"（无效），改为检查"stripe 首行"——首行白+非空时复制第一非白行到首行
4. **32bpp 灰度提取**：CUPS cupsfilter/texttops 输出 32bpp RGBA（4B/px），filter 原来按 1B/px 处理导致内容压缩/丢失；用临时缓冲正确提取
5. **极性**：CUPS 实际输出标准灰度（0=黑 255=白），与 PPD NegativePrint 标志无关 → 强制标准极性
6. **@ep 帧**：移除 Debian 驱动遗留字段（cmd[8]=0x06, cmd[15]=0x80），对齐 Windows 全 0
7. **BIH/compressed_size**：保留标准 BIH（byte16=MX=8, byte19=LRLTWO=0x40），compressed_size = JBIG 总长（含 BIH）
8. **PPD**：A4 ImageableArea 更新为实测可打印区，NegativePrint=false

### 验证结果
- ✅ 网格页：外框完整、中心居中、无截断
- ✅ 文字页（手动路径）：中文+英文正常、居中、无被裁
- ✅ CUPS 完整路径（`lp` 打印）：texttops → raster → filter → 打印机，文字正常
- ✅ 解码验证：5120×6755，内容完整（标题不丢失）

### 最终二进制
- aarch64：`rastertolhplh-aarch64`（静态链接，零 libcups 依赖）
- 已部署：`root@100.94.110.126:/usr/lib/cups/filter/rastertolhplh`
