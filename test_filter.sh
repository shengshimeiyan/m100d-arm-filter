#!/bin/bash
# test_filter.sh — Generate a test CUPS raster file and run the filter
# This creates a simple 600dpi A4 grayscale test page

FILTER="/home/user/桌面/m100d-arm-filter/rastertolhplh"
OUTPUT="/home/user/桌面/m100d-arm-filter/test_output.raw"

echo "=== Testing rastertolhplh filter ==="

# Create a test CUPS raster file using Python
python3 -c "
import struct, sys

# A4 at 600dpi: 4960 x 7016 pixels
width = 4960
height = 7016  # we'll use a smaller height for speed
bpp = 8  # bits per pixel (grayscale)
bps = 8  # bits per sample
cspace = 0  # CUPS_CSPACE_W (grayscale)

# CUPS Raster header (cups_page_header2_t, 400 bytes)
header = bytearray(400)

# MediaClass
header[0:64] = b'\\x00' * 64
# MediaColor  
header[64:128] = b'\\x00' * 64
# MediaType
header[128:192] = b'Plain\\x00' + b'\\x00' * 59
# OutputType
header[192:256] = b'\\x00' * 64

# AdvanceDistance (uint32 @ offset 256)
struct.pack_into('<I', header, 256, 0)
# AdvanceMedia (uint32 @ 260)
struct.pack_into('<I', header, 260, 0)
# Collate (uint32 @ 264)
struct.pack_into('<I', header, 264, 0)
# CutMedia (uint32 @ 268)
struct.pack_into('<I', header, 268, 0)
# Duplex (uint32 @ 272)
struct.pack_into('<I', header, 272, 0)
# HWResolution[2] (uint32 @ 276, 280) - 600x600 dpi
struct.pack_into('<I', header, 276, 600)
struct.pack_into('<I', header, 280, 600)
# ImagingBoundingBox[4] (uint32 @ 284-299)
struct.pack_into('<I', header, 284, 0)
struct.pack_into('<I', header, 288, 0)
struct.pack_into('<I', header, 292, 4960)
struct.pack_into('<I', header, 296, height)
# InsertSheet (uint32 @ 300)
struct.pack_into('<I', header, 300, 0)
# Jog (uint32 @ 304)
struct.pack_into('<I', header, 304, 0)
# LeadingEdge (uint32 @ 308)
struct.pack_into('<I', header, 308, 0)
# Margins[2] (uint32 @ 312, 316)
struct.pack_into('<I', header, 312, 0)
struct.pack_into('<I', header, 316, 0)
# ManualFeed (uint32 @ 320)
struct.pack_into('<I', header, 320, 0)
# MediaPosition (uint32 @ 324)
struct.pack_into('<I', header, 324, 0)
# MediaWeight (uint32 @ 328)
struct.pack_into('<I', header, 328, 0)
# MirrorPrint (uint32 @ 332)
struct.pack_into('<I', header, 332, 0)
# NegativePrint (uint32 @ 336) - 1 for M100D
struct.pack_into('<I', header, 336, 1)
# NumCopies (uint32 @ 340)
struct.pack_into('<I', header, 340, 1)
# Orientation (uint32 @ 344)
struct.pack_into('<I', header, 344, 0)
# OutputFaceUp (uint32 @ 348)
struct.pack_into('<I', header, 348, 0)
# PageSize[2] (uint32 @ 352, 356) - A4 in points
struct.pack_into('<I', header, 352, 595)
struct.pack_into('<I', header, 356, 842)
# Separations (uint32 @ 360)
struct.pack_into('<I', header, 360, 0)
# TraySwitch (uint32 @ 364)
struct.pack_into('<I', header, 364, 0)

# CUPS-specific fields (offset 368+)
# cupsMediaClass (64 bytes @ 368)
header[368:432] = b'\\x00' * 64
# cupsMediaType (64 bytes @ 432)
header[432:496] = b'Plain\\x00' + b'\\x00' * 59
# cupsOutputType (64 bytes @ 496)
header[496:560] = b'\\x00' * 64

# cupsBitsPerColor (uint32 @ 560)
struct.pack_into('<I', header, 560, bps)
# cupsBitsPerPixel (uint32 @ 564)
struct.pack_into('<I', header, 564, bpp)
# cupsBytesPerLine (uint32 @ 568)
struct.pack_into('<I', header, 568, width * bpp // 8)
# cupsColorOrder (uint32 @ 572)
struct.pack_into('<I', header, 572, 0)
# cupsColorSpace (uint32 @ 576) - CUPS_CSPACE_W = 0
struct.pack_into('<I', header, 576, cspace)
# cupsCompression (uint32 @ 580)
struct.pack_into('<I', header, 580, 0)
# cupsHeight (uint32 @ 584)
struct.pack_into('<I', header, 584, height)
# cupsMargins[4] (uint32 @ 588-603)
struct.pack_into('<I', header, 588, 0)
struct.pack_into('<I', header, 592, 0)
struct.pack_into('<I', header, 596, 0)
struct.pack_into('<I', header, 600, 0)
# cupsMatrix[6] (uint32 @ 604-627) - identity
for i in range(6):
    struct.pack_into('<I', header, 604 + i*4, 0)
# cupsNumColors (uint32 @ 628)
struct.pack_into('<I', header, 628, 1)
# cupsPageSize[2] (uint32 @ 632, 636)
struct.pack_into('<I', header, 632, 595)
struct.pack_into('<I', header, 636, 842)
# cupsPhysicalMargins[4] (uint32 @ 640-655)
struct.pack_into('<I', header, 640, 0)
struct.pack_into('<I', header, 644, 0)
struct.pack_into('<I', header, 648, 0)
struct.pack_into('<I', header, 652, 0)
# cupsWidth (uint32 @ 656)
struct.pack_into('<I', header, 656, width)
# cupsRowCount (uint32 @ 660)
struct.pack_into('<I', header, 660, height)
# cupsRowFeed (uint32 @ 664)
struct.pack_into('<I', header, 664, height)
# cupsRowStep (uint32 @ 668)
struct.pack_into('<I', header, 668, 1)

# Remaining fields - zero fill
for i in range(672, 400):
    header[i] = 0

# Write CUPS raster magic + header
sys.stdout.buffer.write(b'RAIST2')  # CUPS Raster v2 magic
sys.stdout.buffer.write(header)

# Write pixel data: alternating black and white stripes for testing
short_height = min(height, 100)  # just 100 lines for quick test
bytes_per_line = width * bpp // 8

for y in range(short_height):
    line = bytearray(bytes_per_line)
    # Create horizontal stripes: black on top half, white on bottom
    if y < short_height // 2:
        # Black stripe (value 0 = black in CUPS)
        for x in range(bytes_per_line):
            line[x] = 0
    else:
        # White stripe (value 255 = white in CUPS)
        for x in range(bytes_per_line):
            line[x] = 255
    sys.stdout.buffer.write(line)

sys.stderr.write(f'Wrote test raster: {width}x{short_height}, {bpp}bpp, {cspace} color space\\n')
" > /home/user/桌面/m100d-arm-filter/test_raster.raw 2>&1

echo "Test raster file created: $(wc -c < /home/user/桌面/m100d-arm-filter/test_raster.raw) bytes"

# Run the filter
echo "--- Running filter ---"
"$FILTER" 1 "test" "Test Page" 1 "" /home/user/桌面/m100d-arm-filter/test_raster.raw > "$OUTPUT" 2>&1 | head -20

# Actually, CUPS filters output to stdout and log to stderr
echo "--- Running filter (stdout->file, stderr->console) ---"
"$FILTER" 1 "test" "Test Page" 1 "" /home/user/桌面/m100d-arm-filter/test_raster.raw > "$OUTPUT" 2>/tmp/filter_stderr.txt
cat /tmp/filter_stderr.txt

echo "--- Output file stats ---"
ls -lh "$OUTPUT"
wc -c < "$OUTPUT"

# Check the PJL structure
echo "--- PJL header check ---"
hexdump -C "$OUTPUT" | head -30

# Check the PJL footer
echo "--- PJL footer check ---"
tail -c 20 "$OUTPUT" | hexdump -C
