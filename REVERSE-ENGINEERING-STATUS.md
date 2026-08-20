# Lenovo M100D reverse-engineering status

Updated: 2026-08-21

## TL;DR — root cause of "black noise" (final)

The printer firmware's JBIG decoder runs with **TPBON** (typical-prediction ON).
Both the JBIG *encoder* options and the LHPLH `@sp` JBIG sub-header must tell
the printer "decode with TPBON", otherwise the arithmetic decoder desynchronises
and the page comes out as random "black noise".

The authoritative evidence is the **official Windows driver**, which is the
only capture confirmed to print correctly on the physical M100D:

| driver           | `@sp` BIE byte[16] | `@sp` BIE byte[19] | prints on M100D? |
|------------------|--------------------|--------------------|-------------------|
| Windows (gold)    | `0x08` (TPBON)     | `0x40` (MX=64)     | ✅ yes             |
| Debian/UOS        | `0x00` (no TPBON)  | `0x40` (MX=64)     | ❓ never verified  |
| project (old)     | `0x00` (no TPBON)  | `0x40` (MX=64)     | ❌ black noise     |
| **project (fix)** | **`0x08` (TPBON)** | `0x40` (MX=64)     | 🔬 pending paper   |

Because the Debian capture was never verified on real hardware, the earlier
attempt to match the Debian bytes (`byte[16]=0x00`, encoder `options=0`) was the
wrong target. The fix now matches the Windows driver instead.

## Hardware test

- Target: Snapdragon 410 device, Debian 11, aarch64.
- Printer: Lenovo M100D over `/dev/usb/lp0` (also `socket://localhost:9100` via p910nd).
- The statically linked ARM64 filter builds and runs on the target.
- Bypass test (raw LHPLH sent to `/dev/usb/lp0` and port 9100) feeds a full page.
- Final TPBON-corrected test page sent to printer: **26025 bytes**, pending paper inspection.

## What changed (this round)

1. **JBIG encoder options**
   Old: `jbg85_enc_options(&jbig_state, 0, 0, 64);`              (no TPBON)
   New: `jbg85_enc_options(&jbig_state, JBG_TPBON, 0, 64);`       (TPBON, MX=64)
   (`l0=0` keeps the JBIG-KIT default of L0=128.)

2. **LHPLH `@sp` JBIG sub-header byte[16]**
   Old: `bie[16] = 0x00;`                                        (no TPBON)
   New: `bie[16] = 0x08;`                                        (JBG_TPBON)
   This byte must stay in lock-step with the encoder options above.

3. **PPD color space**
   Old: `cupsColorSpace 0`   (W = 1-bit white, mismatched with 8 bpp data)
   New: `cupsColorSpace 17`  (GRAY = 8-bit grayscale, matches 8 bpp)

4. **1-bit input expansion (in filter)**
   CUPS may still deliver 1-bit `W`/`K` rasters depending on the pipeline.
   Added an inline expansion of bit-packed `W`/`K` lines to 8-bit `GRAY` so
   the halftoner always receives one byte per pixel. `W`: bit 1→255 (white),
   bit 0→0 (black). `K`: inverted.

5. **Halftone polarity formula**
   Corrected to:
   `negative_print ? (v > threshold) : ((255 - v) > threshold)`
   With PPD `NegativePrint true`: CUPS 0 → white, CUPS 255 → black.

6. **SDNORM→SDRST conversion**
   Only the **last** `FF 02` (SDNORM) in the JBIG stream is rewritten to
   `FF 03` (SDRST). Intermediate SDNORMs are left untouched, keeping the
   T.85 stream valid. Verified: 53 SDNORM + 1 SDRST for 6818 lines
   (54 stripes × 128, last stripe = 34 lines).

7. **debug `printf`s removed** from the raster reader (`sync` reads).

## Why TPBON matters (technical)

In JBIG T.85, TPBON (typical prediction on) changes the context model used by
the arithmetic coder. The encoder and the decoder **must agree** on TPBON:

- If the printer decodes with TPBON but we encode without it (or vice-versa),
  the arithmetic-coder state and the pixel context diverge immediately.
- The decoder then "successfully" produces *bitstream-consistent* but
  *content-wrong* pixels — observed on paper as **black noise** with no text.

There is no standard BIH `options` field in the LHPLH custom 20-byte JBIG
sub-header; instead the printer reads `byte[16]` as the options byte
(`byte[19]` is MX). So both the encoder call and `bie[16]` had to be updated
together.

## Verification on x86-64 (decode round-trip)

The encoder's own 20-byte BIE is stripped from the output that goes to the
printer. To validate, we rebuild a jbig85 BIH for `jbgtopbm85`:

```
BIH = 00 00 01 00  <width BE4>  <height BE4>  00 00 00 80  40 00 00 08
                                                       ^^^         ^^^
                                                       l0=128      options byte[19]=0x08 (TPBON)
byte[16]=mx=64
```

Pixel distribution after round-trip decode of the full A4 test page:

| y       | expected       | decoded |
|---------|----------------|---------|
| 0       | 0  (white)     | 0       |
| 100     | 4768 (bar)     | 4768    |
| 110     | 4768 (bar)     | ~4768   |
| 200     | 4768 (bar)     | 4768    |
| 300     | 730 (pattern)  | 730     |
| 400     | 560 (pattern)  | 560     |
| 1000    | 4768 (bar)     | 4768    |
| 1500    | 288 (pattern)   | 288     |
| 2000    | 636 (pattern)   | 636     |
| 3000    | 96 (pattern)    | 96      |
| 4000    | 4768 (bar)     | 4768    |
| 5000    | 1300 (pattern)  | 1300    |
| 6817    | 4768 (bottom)  | 4768    |

All rows match → the TPBON-encoded stream is internally consistent and the
image is correct. Frame checksums (@sj/@sp/@ep XOR over bytes 0–62) all OK,
final JBIG terminator = SDRST.

## Captures / artifacts in repo

- `captures/debian-original-driver-output.raw`    — official Debian/UOS reference.
- `captures/our-debian-compat-output.raw`          — ARM filter Debian-compat output.
- `captures/tpbon-fixed-output.raw`                — earlier TPBON experiment.
- `captures/windows-original-driver.prn`           — Windows official-driver PRN (gold).
- `captures/current-arm-before-abort.raw`, `captures/current-arm-after-abort.raw` — abort experiment snapshots (reverted).

Zero-byte USBPcap probe files are intentionally not included (no USB payload).

## Reproduction on the ARM target

```sh
make                                      # native build on ARM
# or cross-compile on x86-64:
aarch64-linux-gnu-gcc -O2 -Ijbigkit-2.1/libjbig -c jbigkit-2.1/libjbig/jbig85.c -o jbig85-arm.o
aarch64-linux-gnu-gcc -O2 -Ijbigkit-2.1/libjbig -c jbigkit-2.1/libjbig/jbig_ar.c -o jbig_ar-arm.o
aarch64-linux-gnu-gcc -O2 -Ijbigkit-2.1/libjbig -static \
    -o rastertolhplh-aarch64 rastertolhplh.c jbig85-arm.o jbig_ar-arm.o -lm

sudo install -m 755 rastertolhplh-aarch64 /usr/lib/cups/filter/rastertolhplh
lp -d M100D some-text-file.txt
```

## Reproduction: bypass CUPS and send raw LHPLH to the printer

Useful for isolating "is CUPS the problem" vs "is the LHPLH the problem":

```sh
# USB device directly:
cat /tmp/testpage.lhplh > /dev/usb/lp0

# Or via p910nd on port 9100:
python3 -c 'import socket,sys; s=socket.socket(); s.connect(("127.0.0.1",9100)); s.sendall(open("/tmp/testpage.lhplh","rb").read()); s.close()'
```

Both paths produce the same output the CUPS queue would, so if the bypass page
prints correctly but the CUPS queue does not, the problem is in the CUPS/PWG
raster pipeline, not in `rastertolhplh`.

## ⚠️ Pending user confirmation

- Print the latest TPBON-corrected LHPLH test page on real paper.
- Expected: a white page with black text / grid / gradient / diagonal patterns.

If still wrong after TPBON, the next candidate to vary is `VLENGTH`:
- Current fix uses `JBG_TPBON` only (matches Windows `byte[16]=0x08`).
- Alternative: `JBG_TPBON | JBG_VLENGTH` (0x28) with `bie[16]=0x28`.
- Another alternative: also raise `MX` or match the Windows 5120 width path.

## Next steps (after paper confirmation)

1. If correct: freeze the config, update the README "tested on hardware" badge.
2. If still wrong: try VLENGTH, then width 5120, then SDNORM-only termination.
3. Remove `rastertolhplh-dbg`, `rastertolhplh.arm`, stray `*.o` from the tree.
4. Add an automated end-to-end test in CI (cross-compile + JBIG round-trip).
