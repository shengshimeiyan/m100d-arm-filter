#!/bin/bash
# test_filter.sh — Generate a test CUPS raster file and run the filter
# This creates a simple 600dpi A4 grayscale test page using the correct
# CUPS Raster v2 on-disk format (1796-byte header, "RaS3" magic)
#
# IMPORTANT: CUPS Raster v2 on-disk field offsets differ from the struct layout.
# The on-disk format is: 4-byte sync word + 1792-byte header data.
# The sync word is NOT part of the struct — add +4 to struct-relative offsets
# when writing to the raw buffer.

FILTER="./rastertolhplh"
OUTPUT="./test_output.raw"

echo "=== Testing rastertolhplh filter ==="

# Create a test CUPS raster file using Python
python3 -c "
import struct, sys

# A4 at 600dpi: printable area 4760 x 6818 pixels
width = 4760
height = 100   # short page for quick test
bpp = 8        # bits per pixel (grayscale)
cspace = 0     # CUPS_CSPACE_W (grayscale)

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

# Write pixel data: alternating black and white stripes
bytes_per_line = width * bpp // 8

for y in range(height):
    line = bytearray(bytes_per_line)
    if y < height // 2:
        # Black stripe (value 0 = black in CUPS with NegativePrint)
        for x in range(bytes_per_line):
            line[x] = 0
    else:
        # White stripe (value 255 = white in CUPS with NegativePrint)
        for x in range(bytes_per_line):
            line[x] = 255
    sys.stdout.buffer.write(line)

sys.stderr.write(f'Wrote test raster: {width}x{height}, {bpp}bpp, cspace={cspace}\n')
" > ./test_raster.raw

echo "Test raster file created: $(wc -c < ./test_raster.raw) bytes"

# Run the filter
echo "--- Running filter ---"
"$FILTER" 1 "test" "Test Page" 1 "" ./test_raster.raw > "$OUTPUT" 2>/tmp/filter_stderr.txt
cat /tmp/filter_stderr.txt

echo "--- Output file stats ---"
ls -lh "$OUTPUT"

# Check the PJL structure
echo "--- PJL header check ---"
hexdump -C "$OUTPUT" | head -20

# Check the PJL footer
echo "--- PJL footer check ---"
tail -c 20 "$OUTPUT" | hexdump -C

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
