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
#include <fcntl.h>
#include <stdarg.h>
#include <jbig85.h>

/* ── Constants ─────────────────────────────────────────────────────── */

#define FILTER_VERSION   "2.0.0-arm64-standalone"
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

static void pjl_printf(FILE *fp, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fputs("\r\n", fp);
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
static void write_lhplh_sj(FILE *fp, int copies)
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

    fwrite(cmd, 1, sizeof(cmd), fp);
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
static void write_lhplh_sp(FILE *fp,
                            unsigned page_width, unsigned page_height,
                            unsigned resolution,
                            const unsigned char *jbig_data, size_t jbig_len)
{
    unsigned char hdr[LHPLH_HDR_SIZE];
    unsigned uncompressed_size = ((page_width + 7) / 8) * page_height;
    unsigned stripe_height = 128;  /* L0 = 128, matches original driver (fixed) */

    memset(hdr, 0, sizeof(hdr));

    /* Prefix: ESC LH @sp */
    hdr[0] = 0x1b; hdr[1] = 'L'; hdr[2] = 'H'; hdr[3] = '@';
    hdr[4] = 's'; hdr[5] = 'p';

    /* @sp header fields (mixed 16-bit + 32-bit little-endian) */
    hdr[6] = 0x00; hdr[7] = 0x01;   /* page type/flags (SHORT, LE: 0x0100) — Debian driver */
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
    /* compressed_size (32-bit LE) — includes 20-byte BIE header size
     * even though we don't send the BIE header in the data stream.
     * The Windows driver sets compressed_size = BIE_header(20) + compressed_data.
     */
    unsigned compressed_size = jbig_len + 20;
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
    hdr[44] = 0x33; hdr[45] = 0x08;   /* SHORT: 0x0833 = 2099 (Debian driver) */
    hdr[46] = 0x9a; hdr[47] = 0x0b;   /* SHORT: 0x0b9a = 2970 (297.0mm in 0.1mm units) */

    /* XOR checksum over bytes 0-62 → byte 63 */
    lhplh_xor_checksum(hdr, sizeof(hdr));

    /* Write @sp header */
    fwrite(hdr, 1, sizeof(hdr), fp);

    /* Write JBIG parameters header (20 bytes, custom LHPLH format) */
    /*
     * This is NOT a standard JBIG BIE header — it's the LHPLH protocol's
     * own page-descriptor that encodes the same parameters in a different
     * layout (all 5 fields are big-endian DWORDs):
     *   DWORD[0] = 0x00000100 (flags / version)
     *   DWORD[1] = page_width  (BE)
     *   DWORD[2] = page_height (BE)
     *   DWORD[3] = stripe_height (BE, typically 128)
     *   DWORD[4] = MY:MX (BE DWORD, high WORD=MY=0x0800=2048, low WORD=MX=0x0040=64)
     */
    {
        unsigned char bie[LHPLH_BIE_HDR];
        bie[0]  = 0x00; bie[1]  = 0x00; bie[2]  = 0x01; bie[3]  = 0x00;
        bie[4]  = (page_width >> 24) & 0xFF;
        bie[5]  = (page_width >> 16) & 0xFF;
        bie[6]  = (page_width >> 8) & 0xFF;
        bie[7]  = (page_width >> 0) & 0xFF;
        bie[8]  = (page_height >> 24) & 0xFF;
        bie[9]  = (page_height >> 16) & 0xFF;
        bie[10] = (page_height >> 8) & 0xFF;
        bie[11] = (page_height >> 0) & 0xFF;
        bie[12] = (stripe_height >> 24) & 0xFF;
        bie[13] = (stripe_height >> 16) & 0xFF;
        bie[14] = (stripe_height >> 8) & 0xFF;
        bie[15] = (stripe_height >> 0) & 0xFF;
        /* MY:MX as big-endian DWORD (0x08000040: MY=2048, MX=64) */
        bie[16] = 0x00; bie[17] = 0x00; bie[18] = 0x00; bie[19] = 0x40;
        fwrite(bie, 1, sizeof(bie), fp);
    }

    /* Write JBIG compressed data (without BIE header) */
    if (jbig_len > 0)
        fwrite(jbig_data, 1, jbig_len, fp);
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
static void write_lhplh_ep(FILE *fp)
{
    unsigned char cmd[LHPLH_CMD_SIZE];
    memset(cmd, 0, sizeof(cmd));

    /* Prefix: ESC LH @ep */
    cmd[0] = 0x1b; cmd[1] = 'L'; cmd[2] = 'H'; cmd[3] = '@';
    cmd[4] = 'e'; cmd[5] = 'p';

    /* Debian driver end-of-page fields. */
    cmd[8]  = 0x06;
    cmd[15] = 0x80;

    /* XOR checksum over bytes 0-62 → byte 63 */
    lhplh_xor_checksum(cmd, sizeof(cmd));

    fwrite(cmd, 1, sizeof(cmd), fp);
}

/* ── Halftone a single line ────────────────────────────────────────── */

static void halftone_line(const unsigned char *gray_in,
                          unsigned char       *bit_out,
                          unsigned             width,
                          unsigned             y,
                          int                  negative_print)
{
    unsigned col;
    unsigned byte_idx = 0;
    unsigned char byte_val = 0;
    unsigned bit_pos  = 7;

    for (col = 0; col < width; col++) {
        unsigned char v = gray_in[col];
        /*
         * NegativePrint (from PPD) inverts pixel values:
         *   0 = white (originally black), 255 = black (originally white)
         * So we use v > threshold for black pixels.
         * With standard (non-inverted) input, use (255 - v) > threshold.
         */
        unsigned char threshold = bayer8x8[y % 8][col % 8];
        /*
         * NegativePrint (from PPD) inverts pixel values in CUPS raster:
         *   0 = white (originally black), 255 = black (originally white)
         * With NegativePrint:  v > threshold → black dot
         * Without NegativePrint: (255 - v) > threshold → black dot
         */
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
    unsigned lhplh_page_width = PRINTABLE_WIDTH_600;  /* 4768, Debian driver */
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
        pjl_printf(fp, "@PJL SET RENDERMODE=GRAYSCALE");
        pjl_printf(fp, "@PJL SET RESOLUTION=%d", resolution);
        pjl_printf(fp, "@PJL SET BITSPERPIXEL=1");
        pjl_printf(fp, "@PJL SET COPIES=%d", copies);
        pjl_printf(fp, "@PJL ENTER LANGUAGE=LHPL");

        /* ── LHPLH @sj (Job Setup) ── */
        write_lhplh_sj(fp, copies);
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
        jbg85_enc_init(&jbig_state, lhplh_page_width, height, jbig_data_out, &jbig_out);
        /* Official Debian driver: no TPBON, L0=128, MX=64. */
        jbg85_enc_options(&jbig_state, 0, 0, 64);

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
             * Halftone the CUPS raster line to 1-bit, then pad to
             * lhplh_page_width (4768 pixels). The CUPS content area is
             * left-aligned and the remaining pixels are white.
             */
            memset(cur_line, 0, lhplh_bpl);  /* Zero-fill entire line (white) */
            halftone_line(gray_line, cur_line, width, y, header->NegativePrint);
            jbg85_enc_lineout(&jbig_state, cur_line, prev_line, prev2_line);
            unsigned char *tmp = prev2_line;
            prev2_line = prev_line;
            prev_line  = cur_line;
            cur_line   = tmp;
        }
        /*
         * The jbig85 encoder outputs SDNORM (0xFF 0x02) at the end of each
         * stripe, but the Windows driver uses SDRST (0xFF 0x03) instead.
         * Replace all SDNORM markers with SDRST in the JBIG stream so the
         * printer can decode the data correctly.
         *
         * Note: we must NOT replace 0xFF 0x00 (escaped 0xFF byte in data)
         * — only the SDNORM marker (0xFF 0x02) should become SDRST.
         */
        {
            size_t j;
            for (j = 0; j + 1 < jbig_out.len; j++) {
                if (jbig_out.buf[j] == 0xff && jbig_out.buf[j + 1] == 0x02) {
                    jbig_out.buf[j + 1] = 0x03;  /* SDNORM → SDRST */
                }
            }
        }

        /* ── LHPLH @sp (Page Data) ── */
        /*
         * The jbig85 encoder outputs a 20-byte BIE header at the start of
         * the stream, but the LHPLH protocol does NOT include this header —
         * the JBIG parameters are already embedded in the @sp header
         * (bytes 64-83). The Windows driver also omits the BIE header.
         * Skip the first 20 bytes of the jbig85 output.
         */
        {
            size_t jbig_start = 20;  /* skip BIE header */
            size_t jbig_len   = jbig_out.len > 20 ? jbig_out.len - 20 : 0;
            write_lhplh_sp(fp, lhplh_page_width, y, resolution,
                           jbig_out.buf + jbig_start, jbig_len);
        }

        fprintf(stderr, "INFO: page %d %ux%u @ %ddpi, JBIG %zu bytes\n",
                page_num, width, y, resolution, jbig_out.len);

        free(gray_line); free(prev_line); free(prev2_line); free(cur_line);
        free(jbig_out.buf);
    }

    /* ── LHPLH @ep (End Page) ── */
    write_lhplh_ep(fp);

    /* ── PJL footer ── */
    pjl_printf(fp, "\x1b%%-12345X@PJL EOJ");

    return 0;
}

/* ── Main ──────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    int fd = 0;
    cups_raster_t *ras;
    cups_header_subset_t header;
    int page = 0, copies = 1;

    signal(SIGPIPE, SIG_IGN);

    if (argc < 6) {
        fprintf(stderr, "Usage: %s job-id user title copies options [file]\n", argv[0]);
        return 1;
    }

    if (argc >= 7) {
        fd = open(argv[6], O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "ERROR: cannot open %s\n", argv[6]);
            return 1;
        }
    }

    /* Parse copies from argv[4] */
    if (argv[4])
        copies = atoi(argv[4]);
    if (copies < 1) copies = 1;

    ras = ras_open(fd);
    if (!ras) {
        fprintf(stderr, "ERROR: raster open failed\n");
        if (fd > 0) close(fd);
        return 1;
    }

    while (ras_read_header(ras, &header)) {
        if (header.cupsWidth == 0 || header.cupsHeight == 0) continue;
        if (header.cupsWidth > MAX_PAGE_WIDTH || header.cupsHeight > MAX_PAGE_HEIGHT) break;

        /* Use NumCopies from CUPS header if available */
        if (header.NumCopies > 0)
            copies = header.NumCopies;

        /* Convert RGB to grayscale in the header if needed */
        if (header.cupsColorSpace == CUPS_CSPACE_RGB ||
            header.cupsColorSpace == CUPS_CSPACE_RGB2) {
            fprintf(stderr, "WARNING: RGB input (cspace=%d), converting to gray\n",
                    header.cupsColorSpace);
            header.cupsColorSpace = CUPS_CSPACE_W;
            header.cupsBitsPerPixel = 8;
            header.cupsBytesPerLine = header.cupsWidth * 3;
        }
        /* Convert PWG gray to CUPS gray */
        if (header.cupsColorSpace == CUPS_CSPACE_W2) {
            header.cupsColorSpace = CUPS_CSPACE_W;
        }

        page++;
        fprintf(stderr, "INFO: page %d, %ux%u @ %ux%u dpi, %d bpp\n",
                page, header.cupsWidth, header.cupsHeight,
                header.HWResolution[0], header.HWResolution[1],
                header.cupsBitsPerPixel);

        /*
         * We can't peek ahead in the CUPS raster stream to determine
         * if this is the last page. So we always write PJL EOJ after
         * each page. For single-page jobs (the common case), this is
         * identical to the original driver. For multi-page jobs, each
         * page becomes a separate PJL job — the printer handles this
         * correctly by printing each page in sequence.
         */
        if (write_page(stdout, ras, &header, page, copies,
                       argv[3], argv[2]) != 0) break;
    }

    ras_close(ras);
    if (fd > 0) close(fd);
    fprintf(stderr, "INFO: %d page(s) processed\n", page);
    return 0;
}
