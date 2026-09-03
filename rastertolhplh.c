#define _GNU_SOURCE
/*
 * rastertolhplh - Standalone ARM-native CUPS filter for Lenovo M100D (LHPLH)
 *
 * NO dependency on libcups/libcupsimage — includes a minimal CUPS raster
 * reader so the binary can be fully cross-compiled and runs standalone.
 *
 * Only external dependency: JBIG-KIT 2.1 T.85 (compiled from source)
 *
 * Output format matches the original proprietary driver (lnthr8zfilter.app):
 *   PJL header → LHPLH command frames (@sj, @sp, @ep) → PJL EOJ
 *
 * Build (native):
 *   gcc -O2 -o rastertolhplh rastertolhplh.c jbig85.c jbig_ar.c -lm
 *
 * Build (cross-compile aarch64):
 *   aarch64-linux-gnu-gcc -O2 -o rastertolhplh rastertolhplh.c jbig85.c jbig_ar.c -lm
 *
 * Copyright 2026 — GPLv2+ (JBIG-KIT 2.1 is GPL)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <stdarg.h>
#include <jbig85.h>

/* ── Constants ─────────────────────────────────────────────────────── */

#define FILTER_VERSION   "3.0.1-arm-standalone"
#define MAX_PAGE_WIDTH   10000
#define MAX_PAGE_HEIGHT  15000

/* LHPLH command frame sizes */
#define LHPLH_CMD_SIZE   64    /* @sj and @ep are fixed 64 bytes */
#define LHPLH_HDR_SIZE   64    /* @sp header portion (includes XOR checksum) */
#define LHPLH_BIE_HDR    20    /* JBIG parameters header inside @sp */

/* Original driver printable area width at 600 DPI (for reference only) */
#define PRINTABLE_WIDTH_600  4768   /* 4768 pixels = 201.8mm ≈ A4 printable width */
#define PRINTABLE_WIDTH_1200 9536   /* 2× for 1200 DPI */
/* NOTE: We now use cupsWidth from the CUPS raster header instead of
 * hardcoding these values, so the filter adapts to the PPD configuration. */

/* CUPS raster sync words */
#define CUPS_RASTER_SYNC    0x52615333   /* "RaS3" v2 */
#define CUPS_RASTER_SYNCv1  0x52615374   /* "RaSt" v1 */
#define CUPS_RASTER_REVSYNC 0x33536152   /* "3SaR" reversed v2 */
#define CUPS_RASTER_PWG     0x52615332   /* "RaS2" PWG Raster */
#define CUPS_RASTER_REVPWG  0x32536152   /* "2SaR" reversed PWG */

/* CUPS color spaces (subset we care about) */
#define CUPS_CSPACE_W       0
#define CUPS_CSPACE_RGB     1
#define CUPS_CSPACE_K       3
#define CUPS_CSPACE_GRAY   17
#define CUPS_CSPACE_CMY     4
#define CUPS_CSPACE_CMYK    5
/* PWG Raster color spaces (offset by 18 from CUPS values) */
#define CUPS_CSPACE_W2      18   /* PWG gray (sgray) */
#define CUPS_CSPACE_RGB2    19   /* PWG RGB (srgb) */

/* ── CUPS raster page header ────────────────────────────────────────── */
/*
 * We read the full on-disk header (1796 bytes) but only extract the
 * fields we actually need. This avoids struct padding/ABI mismatches.
 *
 * Layout (all unsigned fields are 4 bytes, char arrays are 64 bytes):
 *   Offset   Field                Type
 *   0        MediaClass           char[64]
 *   64       MediaColor           char[64]
 *   128      MediaType            char[64]
 *   192      OutputType           char[64]
 *   256      AdvanceDistance      unsigned
 *   260      AdvanceMedia         unsigned
 *   264      Collate              unsigned
 *   268      CutMedia             unsigned
 *   272      Duplex               unsigned     ← WE NEED THIS
 *   276      HWResolution[0]      unsigned     ← WE NEED THIS
 *   280      HWResolution[1]      unsigned
 *   284-299  ImagingBoundingBox   unsigned[4]
 *   300      InsertSheet          unsigned
 *   304      Jog                  unsigned
 *   308      LeadingEdge          unsigned
 *   312      Margins[0]           unsigned
 *   316      Margins[1]           unsigned
 *   320      ManualFeed           unsigned
 *   324      MediaPosition        unsigned
 *   328      MediaWeight          unsigned
 *   332      MirrorPrint          unsigned
 *   336      NegativePrint        unsigned
 *   340      NumCopies            unsigned     ← WE NEED THIS
 *   344      Orientation          unsigned
 *   348      OutputFaceUp         unsigned
 *   352      PageSize[0]         unsigned
 *   356      PageSize[1]         unsigned
 *   360      Separations         unsigned
 *   364      TraySwitch          unsigned
 *   368      Tumble              unsigned
 *   372      cupsWidth            unsigned     ← WE NEED THIS
 *   376      cupsHeight           unsigned     ← WE NEED THIS
 *   380      cupsMediaType        unsigned     ← WE NEED THIS
 *   384      cupsBitsPerColor     unsigned
 *   388      cupsBitsPerPixel     unsigned     ← WE NEED THIS
 *   392      cupsBytesPerLine     unsigned     ← WE NEED THIS
 *   396      cupsColorOrder       unsigned
 *   400      cupsColorSpace       unsigned     ← WE NEED THIS
 *   404      cupsCompression      unsigned
 *   408      cupsRowCount         unsigned
 *   412      cupsRowFeed          unsigned
 *   416      cupsRowStep          unsigned
 *   ...      (v2 extended fields, 1796 total)
 */

#define CUPS_HEADER_SIZE 1796

/* Swap byte order of a 32-bit value */
static unsigned swap32(unsigned v)
{
    return ((v >> 24) & 0xff)        |
           ((v >> 8)  & 0xff00)      |
           ((v << 8)  & 0xff0000)    |
           ((v << 24) & 0xff000000);
}

typedef struct {
    unsigned Duplex;
    unsigned HWResolution[2];
    unsigned NumCopies;
    unsigned NegativePrint;
    unsigned cupsWidth;
    unsigned cupsHeight;
    unsigned cupsMediaType;
    unsigned cupsBitsPerPixel;
    unsigned cupsBytesPerLine;
    unsigned cupsColorSpace;
} cups_header_subset_t;

/* Read 4-byte unsigned from a byte buffer at offset (handle byte order) */
static unsigned get_u32(const unsigned char *buf, unsigned offset, int swapped)
{
    unsigned v;
    memcpy(&v, buf + offset, 4);
    if (swapped) v = swap32(v);
    return v;
}

static void put_u32(unsigned char *buf, unsigned offset, unsigned value)
{
    memcpy(buf + offset, &value, 4);
}

/* Parse the CUPS raster header, extracting only the fields we need */
static int parse_header(const unsigned char *raw, int swapped, cups_header_subset_t *h)
{
    /* Off offsets include the 4-byte sync word that precedes the struct */
    /* struct fields start at raw[4], so add 4 to each struct-relative offset */
    h->Duplex           = get_u32(raw, 272 + 4, swapped);
    h->HWResolution[0]  = get_u32(raw, 276 + 4, swapped);
    h->HWResolution[1]  = get_u32(raw, 280 + 4, swapped);
    h->NumCopies        = get_u32(raw, 340 + 4, swapped);
    h->NegativePrint    = get_u32(raw, 336 + 4, swapped);
    h->cupsWidth        = get_u32(raw, 372 + 4, swapped);
    h->cupsHeight       = get_u32(raw, 376 + 4, swapped);
    h->cupsMediaType    = get_u32(raw, 380 + 4, swapped);
    h->cupsBitsPerPixel = get_u32(raw, 388 + 4, swapped);
    h->cupsBytesPerLine = get_u32(raw, 392 + 4, swapped);
    h->cupsColorSpace   = get_u32(raw, 400 + 4, swapped);
    return 1;
}

/* ── Minimal CUPS raster reader ────────────────────────────────────── */

typedef struct {
    int              fd;
    int              swapped;     /* byte order swapped? */
    unsigned char   *buffer;     /* read buffer         */
    size_t           buflen;     /* buffer size         */
    size_t           bufpos;     /* current position    */
    size_t           bufend;     /* end of valid data   */
} cups_raster_t;

static cups_raster_t *ras_open(int fd)
{
    cups_raster_t *ras = calloc(1, sizeof(cups_raster_t));
    if (!ras) return NULL;

    ras->fd      = fd;
    ras->buffer  = malloc(65536);
    ras->buflen  = 65536;

    if (!ras->buffer) { free(ras); return NULL; }

    return ras;
}

static void ras_close(cups_raster_t *ras)
{
    if (ras) {
        free(ras->buffer);
        free(ras);
    }
}

/* Read exactly 'len' bytes from the raster stream */
static int ras_read(cups_raster_t *ras, void *buf, size_t len)
{
    size_t total = 0;
    while (total < len) {
        ssize_t n = read(ras->fd, (unsigned char *)buf + total, len - total);
        if (n <= 0) return 0;   /* EOF or error */
        total += n;
    }
    return 1;
}

/* Read the CUPS raster header */
static int ras_read_header(cups_raster_t *ras, cups_header_subset_t *h)
{
    unsigned sync;
    unsigned char raw[CUPS_HEADER_SIZE];

    if (!ras_read(ras, &sync, 4)) return 0;

    {
        static int dumped = 0;
        if (!dumped) {
            unsigned char sb[4];
            memcpy(sb, &sync, 4);
            fprintf(stderr, "SYNC bytes: %02x %02x %02x %02x\n",
                    sb[0], sb[1], sb[2], sb[3]);
            fflush(stderr);
            dumped = 1;
        }
    }

    int swapped = 0;
    int is_pwg = 0;
    if (sync == CUPS_RASTER_SYNC) {
        swapped = 0;
    } else if (sync == CUPS_RASTER_REVSYNC) {
        swapped = 1;
    } else if (sync == CUPS_RASTER_PWG) {
        /* PWG Raster (RaS2) on big-endian machines.
         * Header fields are big-endian (PWG spec), but on a big-endian
         * machine they are already in native order, so no swap needed. */
        swapped = 0;
        is_pwg = 1;
    } else if (sync == CUPS_RASTER_REVPWG) {
        /* PWG Raster (RaS2) on little-endian machines.
         * The sync word bytes 52 61 53 32 read as 0x32536152 on LE.
         * PWG Raster header fields are big-endian (network byte order),
         * so we need swapped=1 to convert them to native (LE) order. */
        swapped = 1;
        is_pwg = 1;
    } else if (sync == CUPS_RASTER_SYNCv1) {
        /* v1 header is only 296 bytes, but we read 1796 — would misalign
         * all subsequent page data. Reject rather than produce garbage. */
        fprintf(stderr, "ERROR: CUPS Raster v1 not supported (v2 required)\n");
        return 0;
    } else {
        fprintf(stderr, "ERROR: not a CUPS raster file (sync=0x%08x)\n", sync);
        return 0;
    }

    /* Copy sync word into raw buffer, then read the rest of the header */
    memcpy(raw, &sync, 4);
    if (!ras_read(ras, raw + 4, CUPS_HEADER_SIZE - 4)) return 0;

    /* For PWG Raster, the PwgRaster\0 marker at bytes 4-13 overwrites
     * the MediaClass field — but we don't use MediaClass, so no fixup
     * needed. The other header fields are at the same offsets. */
    if (is_pwg) {
        fprintf(stderr, "DEBUG: PWG Raster input detected (RaS2)\n");
    }

    /* Parse the header fields we need */
    parse_header(raw, swapped, h);

    return 1;
}

/* Read pixel data for one line */
static int ras_read_pixels(cups_raster_t *ras, unsigned char *buf, unsigned len)
{
    return ras_read(ras, buf, len);
}

/* ── Halftone matrices ─────────────────────────────────────────────── */

static const unsigned char bayer8x8[8][8] = {
  {  0, 128,  32, 160,   8, 136,  40, 168 },
  { 192,  64, 224,  96, 200,  72, 232, 104 },
  { 48, 176,  16, 144,  56, 184,  24, 152 },
  { 240, 112, 208,  80, 248, 120, 216,  88 },
  { 12, 140,  44, 172,   4, 132,  36, 164 },
  { 204,  76, 236, 108, 196,  68, 228, 100 },
  { 60, 188,  28, 156,  52, 180,  20, 148 },
  { 252, 124, 220,  92, 244, 116, 212,  84 },
};

/* ── JBIG output context ───────────────────────────────────────────── */

typedef struct {
    unsigned char *buf;
    size_t         alloc;
    size_t         len;
} jbig_out_t;

static void jbig_data_out(unsigned char *start, size_t len, void *file)
{
    jbig_out_t *ctx = (jbig_out_t *)file;
    if (ctx->len + len > ctx->alloc) {
        size_t new_alloc = (ctx->alloc == 0) ? 65536 : ctx->alloc * 2;
        while (new_alloc < ctx->len + len) new_alloc *= 2;
        unsigned char *new_buf = realloc(ctx->buf, new_alloc);
        if (!new_buf) return;
        ctx->buf   = new_buf;
        ctx->alloc = new_alloc;
    }
    memcpy(ctx->buf + ctx->len, start, len);
    ctx->len += len;
}

/* ── PJL output helpers ────────────────────────────────────────────── */

/* Write the whole buffer, returning 0 on failure (downstream closed/EPIPE). */
static int write_all(FILE *fp, const void *buf, size_t len)
{
    const unsigned char *p = (const unsigned char *)buf;
    while (len > 0) {
        size_t n = fwrite(p, 1, len, fp);
        if (n == 0) {
            if (ferror(fp)) {
                fprintf(stderr, "ERROR: output write failed (downstream gone)\n");
                perror("fwrite");
            }
            return 0;
        }
        p += n;
        len -= n;
    }
    return 1;
}

static int pjl_printf(FILE *fp, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    if ((size_t)n >= sizeof(buf)) n = sizeof(buf) - 1;
    buf[n] = '\0';
    if (!write_all(fp, buf, (size_t)n)) return 0;
    return write_all(fp, "\r\n", 2);
}

/* ── LHPLH command frame helpers ───────────────────────────────────── */

/*
 * Compute XOR checksum of all bytes in buf[0..len-2],
 * store result in buf[len-1].
 */
static void lhplh_xor_checksum(unsigned char *buf, size_t len)
{
    unsigned char xor_val = 0;
    size_t i;
    for (i = 0; i < len - 1; i++)
        xor_val ^= buf[i];
    buf[len - 1] = xor_val;
}

/*
 * Write an LHPLH @sj (Job Setup) command frame.
 * Format: 64 bytes, XOR checksum at byte 63.
 *
 * Structure (from original driver capture):
 *   Bytes 0-5:   1b 4c 48 40 73 6a  (ESC LH @sj)
 *   Byte  6:     0x01 (version?)
 *   Byte  7:     0x00 (flags?)
 *   Byte  8:     copies (low byte)
 *   Byte  9:     0x00
 *   Bytes 10-62: padding (zeros)
 *   Byte  63:    XOR checksum
 */
static int write_lhplh_sj(FILE *fp, int copies)
{
    unsigned char cmd[LHPLH_CMD_SIZE];
    memset(cmd, 0, sizeof(cmd));

    /* Prefix: ESC LH @sj */
    cmd[0] = 0x1b; cmd[1] = 'L'; cmd[2] = 'H'; cmd[3] = '@';
    cmd[4] = 's'; cmd[5] = 'j';

    /* Version / flags */
    cmd[6] = 0x01;
    cmd[7] = 0x00;

    /* Copies */
    cmd[8] = (unsigned char)(copies & 0xFF);
    cmd[9] = 0x00;

    /* XOR checksum over bytes 0-62 → byte 63 */
    lhplh_xor_checksum(cmd, sizeof(cmd));

    return write_all(fp, cmd, sizeof(cmd));
}

/*
 * Write an LHPLH @sp (Page Data) command frame.
 *
 * Structure (from original driver capture):
 *   Bytes 0-5:    1b 4c 48 40 73 70  (ESC LH @sp)
 *   Bytes 6-63:   @sp header (mixed 16-bit + 32-bit little-endian):
 *     SHORT at offset 6  = 0x0100 (page type/flags)
 *     DWORD at offset 8  = page_width (32-bit LE, from CUPS raster header)
 *     DWORD at offset 12 = page_height (32-bit LE, e.g. 6818)
 *     DWORD at offset 16 = uncompressed_size (32-bit LE, e.g. 4063528)
 *     DWORD at offset 20 = compressed_size (32-bit LE)
 *     DWORD at offset 24 = compressed_size2 (32-bit LE, same as above)
 *     DWORD at offset 28-41 = reserved (zeros)
 *     SHORT at offset 42 = resolution (16-bit LE, e.g. 600)
 *     SHORT at offset 44 = 0x0833 (printer-specific constant)
 *     SHORT at offset 46 = 0x0b9a (printer-specific constant)
 *   Byte 63: XOR checksum of bytes 0-62
 *
 *   After the 64-byte header:
 *   Bytes 64-83:  JBIG parameters header (big-endian, 20 bytes):
 *     DWORD[0] = 0x00000100 (flags)
 *     DWORD[1] = page_width (BE)
 *     DWORD[2] = page_height (BE)
 *     DWORD[3] = stripe_height (BE, e.g. 128)
 *     DWORD[4] = 0x00000040 (MX=64)
 *   Bytes 84+:   JBIG T.85 compressed data (no BIE header wrapper)
 */
static int write_lhplh_sp(FILE *fp,
                            unsigned page_width, unsigned page_height,
                            unsigned resolution,
                            const unsigned char *jbig_data, size_t jbig_len)
{
    unsigned char hdr[LHPLH_HDR_SIZE];
    unsigned uncompressed_size = ((page_width + 7) / 8) * page_height;

    memset(hdr, 0, sizeof(hdr));

    /* Prefix: ESC LH @sp */
    hdr[0] = 0x1b; hdr[1] = 'L'; hdr[2] = 'H'; hdr[3] = '@';
    hdr[4] = 's'; hdr[5] = 'p';

    /* @sp header fields (mixed 16-bit + 32-bit little-endian) */
    hdr[6] = 0x02; hdr[7] = 0x01;   /* page type/flags (SHORT, LE: 0x0102) — Windows driver */
    /* page_width (32-bit LE) */
    hdr[8]  = (page_width >> 0) & 0xFF;
    hdr[9]  = (page_width >> 8) & 0xFF;
    hdr[10] = (page_width >> 16) & 0xFF;
    hdr[11] = (page_width >> 24) & 0xFF;
    /* page_height (32-bit LE) */
    hdr[12] = (page_height >> 0) & 0xFF;
    hdr[13] = (page_height >> 8) & 0xFF;
    hdr[14] = (page_height >> 16) & 0xFF;
    hdr[15] = (page_height >> 24) & 0xFF;
    /* uncompressed_size (32-bit LE) */
    hdr[16] = (uncompressed_size >> 0) & 0xFF;
    hdr[17] = (uncompressed_size >> 8) & 0xFF;
    hdr[18] = (uncompressed_size >> 16) & 0xFF;
    hdr[19] = (uncompressed_size >> 24) & 0xFF;
    /* compressed_size (32-bit LE) — equals the JBIG stream length
     * INCLUDING the 20-byte BIH header (matches Windows driver). */
    unsigned compressed_size = jbig_len;
    hdr[20] = (compressed_size >> 0) & 0xFF;
    hdr[21] = (compressed_size >> 8) & 0xFF;
    hdr[22] = (compressed_size >> 16) & 0xFF;
    hdr[23] = (compressed_size >> 24) & 0xFF;
    /* compressed_size repeated (32-bit LE) */
    hdr[24] = (compressed_size >> 0) & 0xFF;
    hdr[25] = (compressed_size >> 8) & 0xFF;
    hdr[26] = (compressed_size >> 16) & 0xFF;
    hdr[27] = (compressed_size >> 24) & 0xFF;
    /* DWORD[5-7] = 0 (reserved) */
    /* resolution (16-bit LE, at offset 42) */
    hdr[42] = (resolution >> 0) & 0xFF;
    hdr[43] = (resolution >> 8) & 0xFF;
    /* printer-specific constants (16-bit LE, in 0.1mm units) */
    hdr[44] = 0x34; hdr[45] = 0x08;   /* SHORT: 0x0834 = 2100 (Windows driver) */
    hdr[46] = 0x9a; hdr[47] = 0x0b;   /* SHORT: 0x0b9a = 2970 (297.0mm in 0.1mm units) */

    /* XOR checksum over bytes 0-62 → byte 63 */
    lhplh_xor_checksum(hdr, sizeof(hdr));

    /* Write @sp header */
    if (!write_all(fp, hdr, sizeof(hdr))) return 0;

    /* Write JBIG compressed data (with BIH header, matching Windows driver).
     * The Windows driver emits the standard 20-byte JBIG BIH immediately
     * after the @sp frame, then the arithmetic-coded data. The BIH layout is
     * the standard one: byte16=MX, byte19=options (LRLTWO). jbig85 outputs
     * this header at the start of its stream, so we must NOT strip it. */
    if (jbig_len > 0 && !write_all(fp, jbig_data, jbig_len)) return 0;
    return 1;
}

/*
 * Write an LHPLH @ep (End Page) command frame.
 * Format: 64 bytes, XOR checksum at byte 63.
 *
 * Structure (from original driver capture):
 *   Bytes 0-5:   1b 4c 48 40 65 70  (ESC LH @ep)
 *   Byte  6:     0x00
 *   Byte  7:     0x00
 *   Byte  8:     0x06 (end-of-page marker)
 *   Byte  9:     0x00
 *   Byte  14:    0x00
 *   Byte  15:    0x80 (end flag)
 *   Bytes 16-62: padding (zeros)
 *   Byte  63:    XOR checksum
 */
static int write_lhplh_ep(FILE *fp)
{
    unsigned char cmd[LHPLH_CMD_SIZE];
    memset(cmd, 0, sizeof(cmd));

    /* Prefix: ESC LH @ep */
    cmd[0] = 0x1b; cmd[1] = 'L'; cmd[2] = 'H'; cmd[3] = '@';
    cmd[4] = 'e'; cmd[5] = 'p';

    /* All other fields zeroed (matches Windows driver: full-zero @ep) */

    /* XOR checksum over bytes 0-62 → byte 63 */
    lhplh_xor_checksum(cmd, sizeof(cmd));

    return write_all(fp, cmd, sizeof(cmd));
}

/* ── Halftone a single line ────────────────────────────────────────── */

static void halftone_line(const unsigned char *gray_in,
                          unsigned char       *bit_out,
                          unsigned             width,
                          unsigned             y,
                          int                  negative_print,
                          unsigned             left_pad_bits)
{
    unsigned col;
    unsigned byte_idx = left_pad_bits / 8;
    /* padding bits must be WHITE (0 in the bit buffer: 1=black).
     * The old code set them to 0xFF<<n which made them black when
     * left_pad_bits was not a multiple of 8. */
    unsigned char byte_val = 0;
    unsigned bit_pos  = (left_pad_bits % 8) ? (7 - (left_pad_bits % 8)) : 7;

    for (col = 0; col < width; col++) {
        unsigned char v = gray_in[col];
        /*
         * NegativePrint=false (0): CUPS raster value 0=black, 255=white.
         *   Black if: (255 - v) > threshold
         * NegativePrint=true  (1): CUPS raster value 0=white, 255=black.
         *   Black if: v > threshold
         */
        unsigned char threshold = bayer8x8[y % 8][col % 8];
        int is_black = negative_print ? (v > threshold) : ((255 - v) > threshold);
        if (is_black) {
            byte_val |= (1 << bit_pos);
        }
        if (bit_pos == 0) {
            bit_out[byte_idx++] = byte_val;
            byte_val = 0;
            bit_pos  = 7;
        } else {
            bit_pos--;
        }
    }
    if (bit_pos != 7) {
        bit_out[byte_idx] = byte_val;
    }
}

/* ── Write a complete page ─────────────────────────────────────────── */

static int write_page(FILE *fp, cups_raster_t *ras,
                      cups_header_subset_t *header,
                      int page_num, int copies,
                      const char *doc_title, const char *username)
{
    /*
     * Use the CUPS raster width from the page header as the printable area.
     * The original driver uses 4768 pixels at 600 DPI, but our PPD defines
     * cupsWidth=4760. Using the CUPS raster width ensures consistency between
     * the @sp header fields and the actual pixel data sent to the JBIG encoder.
     *
     * The Debian driver uses a fixed 4768-pixel printable width, while the
     * CUPS raster may be slightly narrower because of imageable margins.
     */
    unsigned cups_width = header->cupsWidth;
    unsigned width      = cups_width;
    unsigned height     = header->cupsHeight;
    /* Keep one complete blank L0 stripe before raster data and cap the
     * encoded page at the observed 6755-row firmware boundary. */
    unsigned top_pad = 128;
    if (height + top_pad > 6755) {
        unsigned input_height = height;
        height = 6755 - top_pad;
        fprintf(stderr, "INFO: clipping input from %u to %u rows plus %u top rows\n",
                input_height, height, top_pad);
    }
    unsigned total_height = height + top_pad;
    unsigned lhplh_page_width = 5120;  /* match Windows prn BIE width */
    /* M100D 打印机实测映射（定位页 + 方框页手工测量）:
     * 画布 x=0 → 纸张 4.08mm, 600dpi 精确 (1px = 0.04219mm)。
     * 纸张中心 105mm = 画布 (105-4.08)/0.04219 = 2392px。
     * 内容中心对齐纸张中心，避免内容在纸张上偏左。
     * （可打印区本身不对称: 左 4.08mm / 右 ~0, 所以不能只用可打印区居中） */
    unsigned paper_center_px = 2392;
    const char *pc_env = getenv("PAPER_CENTER_PX");
    if (pc_env) {
        unsigned v = (unsigned)atoi(pc_env);
        if (v >= 500 && v <= 5000) paper_center_px = v;
    }
    unsigned left_pad = (paper_center_px > width/2) ?
                        paper_center_px - width/2 : 0;
    unsigned g_stripe_height = 128;  /* L0 */
    int      duplex     = header->Duplex;
    int      resolution = (header->HWResolution[0] >= 1200) ? 1200 : 600;

    /* ── PJL header (matches original driver format) ── */
    /* Each page is a complete PJL job. Since CUPS raster is sequential,
     * we can't peek ahead to determine if this is the last page.
     * For single-page jobs (the common case), this is identical to
     * the original driver. For multi-page jobs, each page is a
     * separate PJL job — the printer prints each page in sequence.
     */
    {
        /* UEL + JOB with NAME=PRINTER */
        pjl_printf(fp, "\x1b%%-12345X@PJL JOB NAME=PRINTER");

        /* JOBATTR: HST/USR/DOC/DATE/TIME (matches original driver) */
        {
            const char *host = getenv("HOSTNAME");
            if (!host) host = "localhost";
            pjl_printf(fp, "@PJL SET JOBATTR=HST:%s", host);
            pjl_printf(fp, "@PJL SET JOBATTR=USR:%s", username ? username : "unknown");
            pjl_printf(fp, "@PJL SET JOBATTR=DOC:%s", doc_title ? doc_title : "unknown");
            {
                time_t now = time(NULL);
                struct tm *t = localtime(&now);
                pjl_printf(fp, "@PJL SET JOBATTR=DATE:%02d/%02d/%04d",
                           t->tm_mon + 1, t->tm_mday, t->tm_year + 1900);
                pjl_printf(fp, "@PJL SET JOBATTR=TIME:%02d:%02d:%02d",
                           t->tm_hour, t->tm_min, t->tm_sec);
            }
        }

        pjl_printf(fp, "@PJL SET DUPLEX=%s", duplex ? "ON" : "OFF");
        pjl_printf(fp, "@PJL SET MEDIASOURCE=%d", 0);
        pjl_printf(fp, "@PJL SET MDPXS=0");
        pjl_printf(fp, "@PJL SET BITSPERPIXEL=1");
        pjl_printf(fp, "@PJL SET COPIES=%d", copies);
        pjl_printf(fp, "@PJL SET RESOLUTION=%d", resolution);
        pjl_printf(fp, "@PJL SET RENDERMODE=GRAYSCALE");
        pjl_printf(fp, "@PJL ENTER LANGUAGE=LHPL");

        /* Abort if the downstream pipe died during the PJL header. */
        if (ferror(fp)) {
            fprintf(stderr, "ERROR: output pipe closed during PJL header\n");
            return 1;
        }

        /* ── LHPLH @sj (Job Setup) ── */
        if (!write_lhplh_sj(fp, copies)) {
            fprintf(stderr, "ERROR: failed to write @sj\n");
            return 1;
        }
    }

    /* ── Halftone + JBIG compress ── */
    {
        unsigned lhplh_bpl = (lhplh_page_width + 7) / 8;
        unsigned char *gray_line  = malloc(header->cupsBytesPerLine > cups_width + 8
                                            ? header->cupsBytesPerLine : cups_width + 8);
        unsigned char *prev_line  = calloc(lhplh_bpl, 1);
        unsigned char *prev2_line = calloc(lhplh_bpl, 1);
        unsigned char *cur_line   = calloc(lhplh_bpl, 1);
        jbig_out_t     jbig_out   = { NULL, 0, 0 };
        struct jbg85_enc_state jbig_state;
        unsigned y;

        if (!gray_line || !prev_line || !prev2_line || !cur_line) {
            fprintf(stderr, "ERROR: line buffer alloc failed\n");
            free(gray_line); free(prev_line); free(prev2_line); free(cur_line);
            return 1;
        }

        /*
         * JBIG T.85 encoder parameters (matching original driver):
         *   width = 4768 (Debian driver's printable width)
         *   MX = 64 (NOT 127)
         *   options = 0 (no TPBON, no VLENGTH)
         */
        jbg85_enc_init(&jbig_state, lhplh_page_width, total_height, jbig_data_out, &jbig_out);
        /*
         * TPBON options for the M100D printer firmware.
         *
         * The WM/Windows official driver (known-good on the physical M100D)
         * sets the @jp 5th DWORD high byte to 0x08 (= JBG_TPBON, typ-pred
         * ON). The printer firmware therefore decodes JBIG with TPBON.
         * The Debian capture uses byte16=0x00 (no TPBON) but is NOT
         * validated on real hardware, so it must NOT be trusted as the
         * decode target.
         *
         * To match: encoder must use TPBON AND the @sp 5th-DWORD high byte
         * must also be 0x08 so the printer decoder enables TPBON.
         */
        jbg85_enc_options(&jbig_state, JBG_LRLTWO, g_stripe_height, 8);  /* LRLTWO MX=8 (Windows match) */

        /* M100D firmware workaround: the printer's JBIG decoder desyncs
         * (everything below prints blank) when a stripe starts with an
         * all-white first line that is followed by non-white content.
         *
         * Solution: pre-halftone the entire page into a buffer, then for
         * every stripe whose first row is all-white AND the stripe has
         * some non-white content later, copy the stripe's first non-white
         * row onto the first row. Empty stripes (all rows white) and
         * stripes that already start with a non-white row are untouched.
         */
        {
            unsigned long buf_bytes = (unsigned long)total_height * lhplh_bpl;
            unsigned char *halftone_buf = (unsigned char *)malloc(buf_bytes);
            if (!halftone_buf) {
                fprintf(stderr, "ERROR: cannot allocate %lu bytes\n", buf_bytes);
                return 1;
            }
            memset(halftone_buf, 0, buf_bytes);
            /* Pass 1: halftone every row into buffer (after top padding) */
            for (y = 0; y < height; y++) {
                if (!ras_read_pixels(ras, gray_line, header->cupsBytesPerLine)) {
                    fprintf(stderr, "WARNING: EOF at line %u\n", y);
                    break;
                }
                /* Convert RGB -> gray */
                if (header->cupsColorSpace == CUPS_CSPACE_RGB ||
                    header->cupsColorSpace == CUPS_CSPACE_RGB2) {
                    unsigned i;
                    for (i = 0; i < cups_width; i++) {
                        unsigned char r = gray_line[i * 3];
                        unsigned char g = gray_line[i * 3 + 1];
                        unsigned char b = gray_line[i * 3 + 2];
                        gray_line[i] = (unsigned char)((r * 77 + g * 150 + b * 29) >> 8);
                    }
                } else if (header->cupsColorSpace == CUPS_CSPACE_W ||
                           header->cupsColorSpace == CUPS_CSPACE_K) {
                    unsigned max_bits = cups_width;
                    if (max_bits > header->cupsBytesPerLine * 8)
                        max_bits = header->cupsBytesPerLine * 8;
                    for (unsigned i = max_bits; i > 0; i--) {
                        unsigned byte_idx = (i - 1) >> 3;
                        unsigned bit_idx = 7 - ((i - 1) & 7);
                        unsigned char b = (gray_line[byte_idx] >> bit_idx) & 1;
                        gray_line[i - 1] = (b ^ (header->cupsColorSpace == CUPS_CSPACE_K)) ? 255 : 0;
                    }
                } else if (header->cupsBytesPerLine >= cups_width * 2) {
                    /* GRAY may be delivered as 32bpp (4 bytes/pixel) by
                     * some CUPS versions (cupsfilter/texttops): extract the
                     * first byte of each pixel group. */
                    unsigned bytes_pp = header->cupsBytesPerLine / cups_width;
                    if (bytes_pp > 1) {
                        unsigned char *tmp = (unsigned char *)malloc(header->cupsBytesPerLine);
                        if (tmp) {
                            memcpy(tmp, gray_line, header->cupsBytesPerLine);
                            for (unsigned i = 0; i < cups_width; i++)
                                gray_line[i] = tmp[i * bytes_pp];
                            free(tmp);
                        }
                    }
                }
                memset(cur_line, 0, lhplh_bpl);
                /* CUPS delivers standard grayscale (0=black, 255=white)
                 * regardless of the PPD NegativePrint flag (verified on the
                 * M100D via cupsfilter/texttops). Force standard polarity. */
                halftone_line(gray_line, cur_line, width, y, 0, left_pad);
                memcpy(halftone_buf + ((unsigned long)top_pad + y) * lhplh_bpl,
                       cur_line, lhplh_bpl);
            }
            /* Log black-pixel distribution before compression. This proves
             * whether a black lower page came from the input or LHPLH decode. */
            {
                unsigned long black[3] = { 0, 0, 0 };
                unsigned row, off;
                for (row = 0; row < total_height; row++) {
                    const unsigned char *p = halftone_buf + (unsigned long)row * lhplh_bpl;
                    unsigned band = ((unsigned long)row * 3) / total_height;
                    if (band > 2) band = 2;
                    for (off = 0; off < lhplh_bpl; off++)
                        black[band] += (unsigned long)__builtin_popcount((unsigned)p[off]);
                }
                fprintf(stderr, "INFO: black pixels thirds=%lu,%lu,%lu\n",
                        black[0], black[1], black[2]);
            }
            /* Pass 2: encode from buffer */
            for (y = 0; y < total_height; y++) {
                memcpy(cur_line, halftone_buf + (unsigned long)y * lhplh_bpl,
                       lhplh_bpl);
                jbg85_enc_lineout(&jbig_state, cur_line, prev_line, prev2_line);
                unsigned char *tmp = prev2_line;
                prev2_line = prev_line;
                prev_line  = cur_line;
                cur_line   = tmp;
            }
            free(halftone_buf);
        }
        /* original loop body below was kept for reference but is dead code */
        #if 0
        for (y = 0; y < height; y++) {
            if (!ras_read_pixels(ras, gray_line, header->cupsBytesPerLine)) {
                fprintf(stderr, "WARNING: EOF at line %u\n", y);
                break;
            }

            /* Convert RGB (3 bytes/pixel) to grayscale if needed */
            if (header->cupsColorSpace == CUPS_CSPACE_RGB ||
                header->cupsColorSpace == CUPS_CSPACE_RGB2) {
                unsigned i;
                for (i = 0; i < cups_width; i++) {
                    unsigned char r = gray_line[i * 3];
                    unsigned char g = gray_line[i * 3 + 1];
                    unsigned char b = gray_line[i * 3 + 2];
                    gray_line[i] = (unsigned char)((r * 77 + g * 150 + b * 29) >> 8);
                }
            }

            /*
             * Expand 1-bit-per-pixel raster data to 8-bit grayscale.
             * CUPS may send White (cspace=0) or Black (cspace=3) for
             * 1bpp content, depending on the PPD configuration.
             *
             * White (0): 1bpp, bit-packed, 0=black, 1=white
             * Black (3): 1bpp, bit-packed, 0=white, 1=black
             * Gray (17): 8bpp, 1 byte/pixel, 0=black, 255=white
             */
            if (header->cupsColorSpace == CUPS_CSPACE_W ||
                header->cupsColorSpace == CUPS_CSPACE_K) {
                /* Bit-packed: cupsBytesPerLine = (width + 7) / 8 */
                /* Expand bits to full bytes in reverse order */
                unsigned max_bits = cups_width;
                if (max_bits > header->cupsBytesPerLine * 8)
                    max_bits = header->cupsBytesPerLine * 8;
                for (unsigned i = max_bits; i > 0; i--) {
                    unsigned byte_idx = (i - 1) >> 3;
                    unsigned bit_idx = 7 - ((i - 1) & 7);
                    unsigned char b = (gray_line[byte_idx] >> bit_idx) & 1;
                    /* White cspace: 0=black, 1=white. Black cspace: inverted. */
                    gray_line[i - 1] = (b ^ (header->cupsColorSpace == CUPS_CSPACE_K)) ? 255 : 0;
                }
            }

            /*
             * Halftone the CUPS raster line to 1-bit, then pad to
             * lhplh_page_width (4768 pixels). The CUPS content area is
             * left-aligned and the remaining pixels are white.
             */
            memset(cur_line, 0, lhplh_bpl);  /* Zero-fill entire line (white) */
            halftone_line(gray_line, cur_line, width, y, header->NegativePrint, left_pad);
            jbg85_enc_lineout(&jbig_state, cur_line, prev_line, prev2_line);
            unsigned char *tmp = prev2_line;
            prev2_line = prev_line;
            prev_line  = cur_line;
            cur_line   = tmp;
        }
        #endif
        /* jbig85 emits SDRST at each stripe boundary. Count the actual
         * markers rather than rewriting compressed bytes after encoding. */
        {
            size_t j;
            unsigned markers = 0;
            for (j = 1; j < jbig_out.len; j++)
                if (jbig_out.buf[j - 1] == 0xff && jbig_out.buf[j] == 0x03)
                    markers++;
            fprintf(stderr,
                    "INFO: JBIG BIH width=%u height=%u L0=%u MX=%u options=0x%02x SDRST=%u\\n",
                    ((unsigned)jbig_out.buf[4] << 24) | ((unsigned)jbig_out.buf[5] << 16) |
                    ((unsigned)jbig_out.buf[6] << 8) | jbig_out.buf[7],
                    ((unsigned)jbig_out.buf[8] << 24) | ((unsigned)jbig_out.buf[9] << 16) |
                    ((unsigned)jbig_out.buf[10] << 8) | jbig_out.buf[11],
                    ((unsigned)jbig_out.buf[12] << 24) | ((unsigned)jbig_out.buf[13] << 16) |
                    ((unsigned)jbig_out.buf[14] << 8) | jbig_out.buf[15],
                    jbig_out.buf[16], jbig_out.buf[19], markers);
        }

        /* ── LHPLH @sp (Page Data) ── */
        /*
         * The jbig85 encoder outputs a 20-byte BIH header at the start of
         * the stream. The Windows driver KEEPS this header (standard JBIG
         * BIH, byte16=MX, byte19=options) right after the @sp frame, so we
         * pass the complete stream including the BIH.
         */
        {
            size_t jbig_start = 0;  /* keep BIH header (Windows driver match) */
            size_t jbig_len   = jbig_out.len > jbig_start ? jbig_out.len - jbig_start : 0;
            if (!write_lhplh_sp(fp, lhplh_page_width, total_height, resolution,
                               jbig_out.buf + jbig_start, jbig_len)) {
                fprintf(stderr, "ERROR: failed to write @sp page %u\n", page_num);
            }
        }

        fprintf(stderr, "INFO: page %d %ux%u @ %ddpi, JBIG %zu bytes\n",
                page_num, width, total_height, resolution, jbig_out.len);

        free(gray_line); free(prev_line); free(prev2_line); free(cur_line);
        free(jbig_out.buf);
    }

    /* ── LHPLH @ep (End Page) ── */
    if (!write_lhplh_ep(fp)) {
        fprintf(stderr, "ERROR: failed to write @ep\n");
    }

    /* ── PJL footer ── */
    pjl_printf(fp, "\x1b%%-12345X@PJL EOJ");

    return 0;
}

/* ── Main ──────────────────────────────────────────────────────────── */

/* ── IJS server: big-endian int (matches Java DataOutput/DataInput) ────── */
static int be_read_int(void)
{
    unsigned char b[4];
    ssize_t n;
    size_t got = 0;
    while (got < 4) {
        n = read(0, b + got, 4 - got);
        if (n <= 0) return -1;  /* EOF/error */
        got += (size_t)n;
    }
    return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
}

static int be_write_int(int v)
{
    unsigned char b[4];
    b[0] = (unsigned char)(v >> 24);
    b[1] = (unsigned char)(v >> 16);
    b[2] = (unsigned char)(v >> 8);
    b[3] = (unsigned char)(v & 0xff);
    return write_all(stdout, b, 4);
}

/* Read exactly len payload bytes; returns bytes read or -1 on EOF. */
static long be_read_payload(unsigned char *out, int len)
{
    long total = 0;
    while (total < len) {
        ssize_t n = read(0, out + total, (size_t)(len - total));
        if (n <= 0) return -1;
        total += n;
    }
    return total;
}

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);

    fprintf(stderr, "FILTER: IJS server v42 started, argc=%d\n", argc);
    fflush(stderr);

    /* ── Phase 1: handshake (mirror of k/a.e()) ──
     * client writes "IJS\n 0xFC v1 \n" on stdin; server replies on stdout:
     *   line "IJS" + byte 0xAB + line "v1"
     * then client sends frame(2, len=12, arg=30) and server replies
     *   frame(3, len=12, val>=30).
     */
    {
        /* consume exactly "IJS\n 0xFC v1 \n" from stdin (8 bytes). */
        unsigned char h[8];
        if (be_read_payload(h, 8) != 8) {
            fprintf(stderr, "IJS: handshake read failed (expected 8 bytes)\n");
            fflush(stderr);
            return 1;
        }
        fprintf(stderr, "IJS: got handshake bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                h[0],h[1],h[2],h[3],h[4],h[5],h[6],h[7]);
        fflush(stderr);
        /* reply "IJS\n" + 0xAB + "v1\n" */
        if (!write_all(stdout, "IJS\n", 4)) { fprintf(stderr,"IJS: reply1 failed\n"); return 1; }
        { unsigned char ab = 0xAB; if (!write_all(stdout, &ab, 1)) return 1; }
        if (!write_all(stdout, "v1\n", 3)) return 1;
        fflush(stdout);
    }

    /* ── Phase 2: frame loop ──
     * client sends length-prefixed frames: writeInt(opcode) writeInt(len) [len-8]
     * server replies: writeInt(reply) writeInt(replyLen) [payload].
     */
    /* Collect opcode-15 (s=send raster/data) payloads into a file for offline
     * analysis, and record job parameters (opcode 12) seen. */
    {
        const char *work_dir = "/data/user/0/com.dynamixsoftware.printershare/files/drv_m100d";
        const char *raster_path = "/data/user/0/com.dynamixsoftware.printershare/files/drv_m100d/ijsraster.bin";
        FILE *dataf = fopen("/data/user/0/com.dynamixsoftware.printershare/files/drv_m100d/ijsdata.bin", "wb");
        FILE *rasterf = fopen(raster_path, "w+b");
        (void)work_dir;
        long long op15_total = 0;
        unsigned raster_rows = 0;
        unsigned raster_width = 4780;
        unsigned raster_height = 6765;
        unsigned raster_bpl = raster_width * 3;
        if (!rasterf) {
            fprintf(stderr, "IJS: cannot create temporary raster file\n");
            return 1;
        }
        /* Reserve a synthetic CUPS Raster v2 header. The IJS client sends
         * one RGB scanline per opcode 15, so the existing, hardware-tested
         * CUPS-to-LHPLH writer can be reused unchanged at the end. */
        {
            unsigned sync = CUPS_RASTER_SYNC;
            unsigned char header[CUPS_HEADER_SIZE];
            memset(header, 0, sizeof(header));
            fwrite(&sync, 1, 4, rasterf);
            fwrite(header, 1, sizeof(header) - 4, rasterf);
        }

    for (;;) {
        int opcode = be_read_int();
        if (opcode < 0) {
            fprintf(stderr, "IJS: EOF reading opcode, server done (op15_total=%lld)\n", op15_total);
            fflush(stderr);
            break;
        }
        int flen = be_read_int();
        if (flen < 8) { fprintf(stderr, "IJS: bad frame len %d (op=%d)\n", flen, opcode); break; }
        int paylen = flen - 8;
        unsigned char pay[262144];
        int data_len = 0;
        /* opcode 15 is special: the frame declares only its two integer
         * fields (channel and byte count); the raster bytes follow outside
         * the declared frame length.  Java k.a.s() writes p(15, channel,
         * count), then writes the raster buffer directly. */
        if (opcode == 15) {
            if (paylen != 8) {
                fprintf(stderr, "IJS: bad opcode 15 header length %d\n", paylen);
                break;
            }
            if (be_read_payload(pay, 8) != 8) {
                fprintf(stderr, "IJS: short opcode 15 header\n");
                break;
            }
            data_len = ((int)pay[4] << 24) | ((int)pay[5] << 16) |
                       ((int)pay[6] << 8) | (int)pay[7];
            if (data_len < 0) {
                fprintf(stderr, "IJS: invalid opcode 15 byte count %d\n", data_len);
                break;
            }
        } else {
            if (paylen > (int)sizeof(pay)) paylen = (int)sizeof(pay);
            if (paylen > 0 && be_read_payload(pay, paylen) != paylen) {
                fprintf(stderr, "IJS: short payload read\n"); break;
            }
        }

        if (opcode == 15) {
            unsigned char chunk[8192];
            int remaining = data_len;
            while (remaining > 0) {
                int n = remaining > (int)sizeof(chunk) ? (int)sizeof(chunk) : remaining;
                if (be_read_payload(chunk, n) != n) {
                    fprintf(stderr, "IJS: short opcode 15 data (%d bytes remain)\n", remaining);
                    remaining = -1;
                    break;
                }
                if (dataf && fwrite(chunk, 1, (size_t)n, dataf) != (size_t)n) {
                    fprintf(stderr, "IJS: opcode 15 data file write failed\n");
                }
                if (rasterf && fwrite(chunk, 1, (size_t)n, rasterf) != (size_t)n) {
                    fprintf(stderr, "IJS: temporary raster write failed\n");
                    remaining = -1;
                    break;
                }
                remaining -= n;
            }
            if (remaining < 0) break;
            if (data_len == (int)raster_bpl) raster_rows++;
            op15_total += data_len;
        }

        fprintf(stderr, "IJS frame: opcode=%d len=%d payload[%d]: ", opcode, flen, paylen);
        { int i; for (i = 0; i < paylen && i < 32; i++) fprintf(stderr, "%02x ", pay[i]); }
        fprintf(stderr, "\n");
        if (opcode == 12 && paylen >= 9) {
            /* payload = int(arg) int(combined) key NUL value  */
            int klen = (pay[7] << 24) | (pay[6] << 16) | (pay[5] << 8) | pay[4];
            if (klen > 0 && klen < paylen) {
                fprintf(stderr, "IJS param: arg=0%.2X key=\"%.*s\" val=\"%s\"\n",
                        (unsigned)pay[8], klen, pay + 9,
                        (const char *)(pay + 9 + klen));
            }
        }
        fflush(stderr);

        if (opcode == 17) {
            /* opcode 17 is not an acknowledged IJS command. Java sends it
             * immediately before procWait(), so waiting for more input here
             * deadlocks the child and leaves the app stuck at job completion. */
            fprintf(stderr, "IJS: opcode 17 received; finishing filter\n");
            fflush(stderr);
            break;
        } else if (opcode == 2) {
            be_write_int(3); be_write_int(12); be_write_int(30);
            fflush(stdout);
            fprintf(stderr, "IJS: replied handshake confirm (op=3 val=30)\n");
        } else if (opcode == 15) {
            /* data chunk ack: success */
            be_write_int(0); be_write_int(8);
            fflush(stdout);
        } else {
            be_write_int(0); be_write_int(8);
            fflush(stdout);
        }
    }

    if (dataf) { fclose(dataf); }
    fprintf(stderr, "IJS: op15 total received = %lld bytes (%u rows)\n",
            op15_total, raster_rows);
    fflush(stderr);

    /* The IJS control stream is only a transport wrapper. Once opcode 5
     * (job end) arrives, turn the collected RGB scanlines into the same
     * CUPS Raster input consumed by write_page(), then emit real PJL/LHPLH
     * bytes on stdout for q's printer-output pump. */
    if (rasterf && raster_rows > 0) {
        unsigned char header[CUPS_HEADER_SIZE];
        unsigned sync = CUPS_RASTER_SYNC;
        memset(header, 0, sizeof(header));
        memcpy(header, &sync, 4);
        put_u32(header, 372 + 4, raster_width);
        put_u32(header, 376 + 4, raster_rows < raster_height ? raster_rows : raster_height);
        put_u32(header, 272 + 4, 0);       /* Duplex */
        put_u32(header, 276 + 4, 600);     /* HWResolution X */
        put_u32(header, 280 + 4, 600);     /* HWResolution Y */
        put_u32(header, 340 + 4, 1);       /* NumCopies */
        put_u32(header, 388 + 4, 24);      /* cupsBitsPerPixel */
        put_u32(header, 392 + 4, raster_bpl);
        put_u32(header, 400 + 4, CUPS_CSPACE_RGB);
        rewind(rasterf);
        if (fwrite(header, 1, sizeof(header), rasterf) != sizeof(header) ||
            fflush(rasterf) != 0 || fseek(rasterf, 0, SEEK_SET) != 0) {
            fprintf(stderr, "IJS: failed to prepare synthetic raster header\n");
        }
        {
            cups_raster_t ras;
            cups_header_subset_t rh;
            memset(&ras, 0, sizeof(ras));
            ras.fd = fileno(rasterf);
            if (ras_read_header(&ras, &rh)) {
                FILE *printer = fdopen(100, "wb");
                if (!printer) {
                    fprintf(stderr, "IJS: fdopen(100) failed\n");
                } else {
                    if (write_page(printer, &ras, &rh, 1, 1, "PrinterShare", "PrinterShare") != 0)
                        fprintf(stderr, "IJS: write_page failed\n");
                    fflush(printer);
                    fclose(printer);
                }
            } else {
                fprintf(stderr, "IJS: synthetic raster header rejected\n");
            }
        }
    } else {
        fprintf(stderr, "IJS: no complete RGB scanlines received\n");
    }
    if (rasterf) {
        fclose(rasterf);
        unlink(raster_path);
    }
    }

    fprintf(stderr, "IJS: server exiting\n");
    fflush(stderr);
    return 0;
}
