# M100D Capture Data Analysis

## Files

- `debian-original-driver-output.raw` - Debian x64 driver capture (4.1KB, 4768x100)
- `tpbon-fixed-output.raw` - Our TPBON-enabled output (Windows parameters, 60KB)
- `our-debian-compat-output.raw` - Our Debian-compatible output (7.5KB, 4768x100)

## Comparison Results

### Debian-compatible output vs Debian original driver

| Component | Result |
|-----------|--------|
| @sp header (page_width, page_height, resolution, etc.) | Match |
| BIE sub-header | Match |
| @ep | Match |
| JBIG decoded pixel data | Match (100% identical) |
| JBIG compression size | Different (our=7001B vs Debian=3613B) |
| PJL SET parameters | Match (except JOBATTR) |

### Key findings

1. JBIG pixel data is 100% identical - our encoder produces the same decoded output as the Debian driver
2. JBIG compression efficiency differs - our jbigkit-2.1 encoder produces ~2x larger output than the Debian driver closed-source encoder, but both are T.85 standard compliant
3. All protocol parameters match - page_width=4768, no TPBON, MX=64, @ep byte[8]=0x06/byte[15]=0x80, offset 44-45=0x0833
4. NegativePrint=1 is correctly handled in the PPD, CUPS inverts pixel data before sending to the filter

### BIE sub-header format (LHPLH custom)

| Byte | Standard JBIG | LHPLH |
|------|--------------|-------|
| 16 | MX | options (0x00=no TPBON, 0x08=TPBON) |
| 17 | MY | MY (0) |
| 18 | options | reserved (0) |
| 19 | - | MX (64) |

### JBIG parameters (Debian driver)

- Width: 4768 pixels
- Height: variable (from page)
- L0 (stripe height): 128
- MX: 64
- TPBON: disabled (Debian), enabled (Windows)
- Termination: SDRST (0xFF 0x03)
