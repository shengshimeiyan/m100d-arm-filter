# Lenovo M100D reverse-engineering status

Updated: 2026-08-09

## Hardware test

- Target: Snapdragon 410 device, Debian 11.2, aarch64.
- Printer: Lenovo M100D over `/dev/usb/lp0`.
- CUPS queue: `M100D`.
- The statically linked ARM64 filter builds and runs on the target.
- CUPS accepts a normal text job and the printer feeds one page, but the page contains black noise and no readable text.
- The synthetic polarity test produced three white horizontal bars on an otherwise black page. This confirms that the current raster polarity/halftone path is still wrong; it is not a successful print test.

## Current Debian/UOS protocol hypothesis

The Debian/UOS capture is the current protocol target, not the Windows capture:

- printable width: 4768 pixels at 600 dpi;
- `@sp` page type/flags: `0x0100`;
- paper width field: `0x0833` (209.9 mm);
- custom JBIG header: `flags=0x00000100`, `L0=128`, `MY=0`, `MX=64`;
- JBIG encoder options: no TPBON and no VLENGTH;
- JBIG stream termination: convert `SDNORM` (`ff 02`) to `SDRST` (`ff 03`);
- `@ep` fields observed in the Debian capture: byte 8=`06`, byte 15=`80`;
- all command-frame checksums are XOR over bytes 0–62.

The Windows capture is retained for comparison only. It uses a different 5120-pixel width and TPBON-enabled JBIG parameters, so those values must not be copied into the Debian ARM path.

## Captures

- `captures/debian-original-driver-output.raw`: official Debian/UOS reference capture already in the repository.
- `captures/our-debian-compat-output.raw`: ARM filter protocol comparison capture already in the repository.
- `captures/tpbon-fixed-output.raw`: earlier TPBON experiment already in the repository.
- `captures/windows-original-driver.prn`: Windows official-driver output captured through a local file port.
- `captures/current-arm-before-abort.raw`: ARM output before the temporary abort experiment.
- `captures/current-arm-after-abort.raw`: retained for comparison; the abort experiment was reverted.

Zero-byte USBPcap probe files are intentionally not included because no USB payload was captured from them.

## Reproduction on the ARM target

```sh
make
sudo install -m 755 rastertolhplh /usr/lib/cups/filter/rastertolhplh
lp -d M100D some-text-file.txt
```

The current blocker is JBIG/raster content reconstruction: framing, dimensions, checksums, and job submission are functional, but the printer does not reproduce the intended bitmap.
