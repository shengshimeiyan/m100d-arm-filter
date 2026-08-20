#!/bin/bash
# test_filter.sh — Generate a test CUPS raster file and run the filter
# This creates a simple 600dpi A4 grayscale test page using the correct
# CUPS Raster v2 on-disk format (1796-byte header, "RaS3" magic)
#
# IMPORTANT: CUPS Raster v2 on-disk field offsets differ from the struct layout.
# The on-disk format is: 4-byte sync word + 1792-byte header data.
# The sync word is NOT part of the struct — add +4 to struct-relative offsets
# when writing to the raw buffer.

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
FILTER="$SCRIPT_DIR/rastertolhplh"
OUTPUT="$SCRIPT_DIR/test_output.raw"
RASTER="${TMPDIR:-/tmp}/m100d-filter-raster.$$"
DIAG="${TMPDIR:-/tmp}/m100d-filter-stderr.$$"
cleanup() {
    rm -f "$DIAG" "$RASTER"
    if [ "${KEEP_OUTPUT:-0}" != 1 ]; then
        rm -f "$OUTPUT"
    fi
}
trap cleanup EXIT

echo "=== Testing rastertolhplh filter ==="

# Create a test CUPS raster file using Python
python3 -c "
import struct, sys

# A4 at 600dpi: Debian driver's printable area 4768 x 6818 pixels
width = 4768
height = 6818
bpp = 8        # bits per pixel (grayscale)
cspace = 17    # CUPS_CSPACE_GRAY (8-bit grayscale, matches PPD)

# CUPS Raster v2 on-disk format: 4-byte sync + 1792-byte header = 1796 bytes
# (struct-relative offsets from the checkpoint, +4 for sync word)
header = bytearray(1796)

# Sync word (4 bytes): CUPS Raster v2 = 0x52615333 ("RaS3")
# Written in native (little-endian) byte order, matching the CUPS raster format.
# On LE systems this becomes bytes 33 53 61 52 ("3SaR").
struct.pack_into('<I', header, 0, 0x52615333)

# --- struct-relative offsets (add +4 for raw buffer position) ---
# Duplex (uint32 @ struct 272)
struct.pack_into('<I', header, 272 + 4, 0)
# HWResolution[2] (uint32 @ struct 276, 280) - 600x600 dpi
struct.pack_into('<I', header, 276 + 4, 600)
struct.pack_into('<I', header, 280 + 4, 600)
# NegativePrint (uint32 @ struct 336)
struct.pack_into('<I', header, 336 + 4, 1)
# NumCopies (uint32 @ struct 340)
struct.pack_into('<I', header, 340 + 4, 1)
# cupsWidth (uint32 @ struct 372)
struct.pack_into('<I', header, 372 + 4, width)
# cupsHeight (uint32 @ struct 376)
struct.pack_into('<I', header, 376 + 4, height)
# cupsMediaType (uint32 @ struct 380)
struct.pack_into('<I', header, 380 + 4, 0)
# cupsBitsPerPixel (uint32 @ struct 388)
struct.pack_into('<I', header, 388 + 4, bpp)
# cupsBytesPerLine (uint32 @ struct 392)
struct.pack_into('<I', header, 392 + 4, width * bpp // 8)
# cupsColorSpace (uint32 @ struct 400) - CUPS_CSPACE_W = 0
struct.pack_into('<I', header, 400 + 4, cspace)

# Write header
sys.stdout.buffer.write(header)

# Write a deterministic full-page pattern. NegativePrint=1 means the
# CUPS raster carries inverted gray values: 255 is intended black and
# 0 is intended white for this filter.
bytes_per_line = width * bpp // 8

for y in range(height):
    line = bytearray(bytes_per_line)  # intended white
    if 100 <= y < 220 or 3000 <= y < 3120 or 6500 <= y < 6620:
        line[:] = b'\xff' * bytes_per_line
    else:
        for x in range(100, 4768, 400):
            if x + 20 < width:
                line[x:x + 20] = b'\xff' * 20
    sys.stdout.buffer.write(line)

sys.stderr.write(f'Wrote test raster: {width}x{height}, {bpp}bpp, cspace={cspace}\n')
" > "$RASTER"

echo "Test raster file created: $(wc -c < "$RASTER") bytes"

# Run the filter
echo "--- Running filter ---"
"$FILTER" 1 "test" "Test Page" 1 "" "$RASTER" > "$OUTPUT" 2>"$DIAG"
cat "$DIAG"

echo "--- Output file stats ---"
ls -lh "$OUTPUT"

# Check the PJL structure
echo "--- PJL header check ---"
if command -v hexdump >/dev/null 2>&1; then
    hexdump -C "$OUTPUT" | head -20
else
    od -Ax -tx1z -N320 "$OUTPUT"
fi

# Check the PJL footer
echo "--- PJL footer check ---"
if command -v hexdump >/dev/null 2>&1; then
    tail -c 20 "$OUTPUT" | hexdump -C
else
    tail -c 20 "$OUTPUT" | od -Ax -tx1z
fi

echo "--- Validation ---"
python3 -c "
import struct, sys
data = open('$OUTPUT', 'rb').read()
print(f'Output size: {len(data)} bytes')

# Check PJL
if b'@PJL JOB NAME=PRINTER' in data:
    print('✅ PJL JOB header found')
if b'@PJL ENTER LANGUAGE=LHPL' in data:
    print('✅ PJL ENTER LANGUAGE found')
if b'@PJL EOJ' in data:
    print('✅ PJL EOJ found')

# Check LHPLH commands
for cmd in [b'\x1bLH@sj', b'\x1bLH@sp', b'\x1bLH@ep']:
    if cmd in data:
        print(f'✅ {cmd[3:].decode()} found')

# Check @sp fields
sp_pos = data.find(b'\x1bLH@sp')
ep_pos = data.find(b'\x1bLH@ep')
if sp_pos >= 0 and ep_pos >= 0:
    sp = data[sp_pos:ep_pos]
    w = struct.unpack('<I', sp[8:12])[0]
    h = struct.unpack('<I', sp[12:16])[0]
    print(f'✅ @sp width={w} height={h}')
    # Check JBIG termination
    jbig = sp[84:]
    term = jbig[-2:]
    names = {2: 'SDNORM', 3: 'SDRST', 4: 'ABORT'}
    print(f'✅ JBIG termination: {names.get(term[1], \"?\")} (0xFF 0x{term[1]:02x})')

# Check DOC attribute
if b'JOBATTR=DOC:Test Page' in data:
    print('✅ DOC=Test Page (uses argv[3] title)')
if b'JOBATTR=USR:test' in data:
    print('✅ USR=test (uses argv[2] username)')
"
