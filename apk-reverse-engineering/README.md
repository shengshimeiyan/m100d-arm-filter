# PrinterShare APK reverse-engineering notes

This directory contains the reproducible, M100D-relevant results from the PrinterShare APK analysis:

- `reports/PRINTERSHARE-APK-ANALYSIS.md` — application, protocol, asset, and integration inventory.
- `assets/drv_m100d.dat` — extracted M100D driver asset.
- `assets/lenovo-*-arm.ppd` — extracted Lenovo ARM PPDs.
- `java/` and `smali/` — M100D extractor/runtime integration excerpts.
- `scripts/gen_ppds.py` — PPD generation helper used during analysis.

The original APKs, signed/repacked APK variants, generated apktool trees, build caches, firmware images, and third-party binaries are intentionally not copied: they are large generated/proprietary inputs, not required to reproduce the open driver, and may contain signing or third-party material. Keep those in the local analysis workspace and document hashes when redistribution is authorized.
