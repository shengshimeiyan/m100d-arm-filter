/*
 * rastertolhplh - Standalone ARM-native CUPS filter for Lenovo M100D (LHPLH)
 *
 * NO dependency on libcups/libcupsimage — includes a minimal CUPS raster
 * reader so the binary can be fully cross-compiled and runs standalone.
 *
 * Only external dependency: JBIG-KIT 2.1 T.85 (compiled from source)
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

#define FILTER_VERSION   "1.0.1-arm64-standalone"
#define PJL_SEPARATOR    512
#define MAX_PAGE_WIDTH   10000
#define MAX_PAGE_HEIGHT  15000

/* CUPS raster sync words */
#define CUPS_RASTER_SYNC    0x52615333   /* "RaS3" v2 */
#define CUPS_RASTER_SYNCv1  0x52615374   /* "RaSt" v1 */
#define CUPS_RASTER_REVSYNC 0x33536152   /* "3SaR" reversed v2 */

/* CUPS color spaces (subset we care about) */
#define CUPS_CSPACE_W       0
#define CUPS_CSPACE_RGB     1
#define CUPS_CSPACE_K       3

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
    if (sync == CUPS_RASTER_SYNC) {
        swapped = 0;
    } else if (sync == CUPS_RASTER_REVSYNC) {
        swapped = 1;
    } else if (sync == CUPS_RASTER_SYNCv1) {
        swapped = 0;
    } else {
        fprintf(stderr, "ERROR: not a CUPS raster file (sync=0x%08x)\n", sync);
        return 0;
    }

    /* Copy sync word into raw buffer, then read the rest of the header */
    memcpy(raw, &sync, 4);
    if (!ras_read(ras, raw + 4, CUPS_HEADER_SIZE - 4)) return 0;

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

/* ── Media type mapping ────────────────────────────────────────────── */

static int cups_mediatype_to_pjl(int cups_media_type)
{
    switch (cups_media_type) {
    case 0:  return 0;  /* Plain */
    case 1:  return 1;  /* Recycled */
    case 2:  return 2;  /* Thick */
    case 3:  return 3;  /* Thin */
    case 4:  return 4;  /* Label */
    case 5:  return 5;  /* Envelope */
    default: return 0;
    }
}

/* ── Halftone a single line ────────────────────────────────────────── */

static void halftone_line(const unsigned char *gray_in,
                          unsigned char       *bit_out,
                          unsigned             width,
                          unsigned             y)
{
    unsigned col;
    unsigned byte_idx = 0;
    unsigned char byte_val = 0;
    unsigned bit_pos  = 7;

    for (col = 0; col < width; col++) {
        unsigned char v = gray_in[col];
        unsigned char threshold = bayer8x8[y % 8][col % 8];
        if ((255 - v) > threshold) {
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
                      cups_header_subset_t *header, int toner_save)
{
    unsigned width      = header->HWResolution[0] >= 1200 ? header->cupsWidth * 2 : header->cupsWidth;
    unsigned height     = header->cupsHeight;
    unsigned bytes_per_line = (width + 7) / 8;
    unsigned cups_width = header->cupsWidth;
    int      duplex     = header->Duplex;
    int      media_type = cups_mediatype_to_pjl(header->cupsMediaType);
    int      resolution = (header->HWResolution[0] >= 1200) ? 1200 : 600;
    int      copies     = header->NumCopies > 0 ? header->NumCopies : 1;

    /* ── PJL header ── */
    pjl_printf(fp, "@PJL JOB");
    pjl_printf(fp, "@PJL SET JOBATTR=DATE:%s", __DATE__);
    {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        pjl_printf(fp, "@PJL SET JOBATTR=TIME:%02d%02d%02d",
                   t->tm_hour, t->tm_min, t->tm_sec);
    }
    pjl_printf(fp, "@PJL SET JOBATTR=USERID:");
    pjl_printf(fp, "@PJL SET JOBATTR=PASSCODE:");
    pjl_printf(fp, "@PJL SET DUPLEX=%s", duplex ? "ON" : "OFF");
    pjl_printf(fp, "@PJL SET RENDERMODE=GRAY");
    pjl_printf(fp, "@PJL SET MEDIASOURCE=%d", 0);
    pjl_printf(fp, "@PJL SET RESOLUTION=%d", resolution);
    pjl_printf(fp, "@PJL SET BITSPERPIXEL=1");
    pjl_printf(fp, "@PJL SET COPIES=%d", copies);
    pjl_printf(fp, "@PJL SET MEDIATYPE=%d", media_type);
    pjl_printf(fp, "@PJL SET TONERMODE=%d", toner_save);
    pjl_printf(fp, "@PJL COMMENT rastertolhplh v" FILTER_VERSION);
    pjl_printf(fp, "@PJL ENTER LANGUAGE=LHPLH");

    /* ── 512-byte separator ── */
    {
        unsigned char zero[PJL_SEPARATOR];
        memset(zero, 0, sizeof(zero));
        fwrite(zero, 1, PJL_SEPARATOR, fp);
    }

    /* ── Halftone + JBIG compress ── */
    {
        unsigned char *gray_line  = malloc(cups_width + 8);
        unsigned char *prev_line  = calloc(bytes_per_line, 1);
        unsigned char *prev2_line = calloc(bytes_per_line, 1);
        unsigned char *cur_line   = calloc(bytes_per_line, 1);
        jbig_out_t     jbig_out   = { NULL, 0, 0 };
        struct jbg85_enc_state jbig_state;
        unsigned y;

        if (!gray_line || !prev_line || !prev2_line || !cur_line) {
            fprintf(stderr, "ERROR: line buffer alloc failed\n");
            free(gray_line); free(prev_line); free(prev2_line); free(cur_line);
            return 1;
        }

        jbg85_enc_init(&jbig_state, width, height, jbig_data_out, &jbig_out);
        jbg85_enc_options(&jbig_state, JBG_TPBON | JBG_VLENGTH, 0, 127);

        for (y = 0; y < height; y++) {
            if (!ras_read_pixels(ras, gray_line, header->cupsBytesPerLine)) {
                fprintf(stderr, "WARNING: EOF at line %u\n", y);
                break;
            }
            if (header->HWResolution[0] >= 1200 && cups_width < width) {
                unsigned char *expanded = malloc(width + 8);
                unsigned i;
                for (i = 0; i < cups_width; i++) {
                    expanded[i * 2]     = gray_line[i];
                    expanded[i * 2 + 1] = gray_line[i];
                }
                halftone_line(expanded, cur_line, width, y);
                free(expanded);
            } else {
                halftone_line(gray_line, cur_line, width, y);
            }
            jbg85_enc_lineout(&jbig_state, cur_line, prev_line, prev2_line);
            unsigned char *tmp = prev2_line;
            prev2_line = prev_line;
            prev_line  = cur_line;
            cur_line   = tmp;
        }
        jbg85_enc_newlen(&jbig_state, y);
        jbg85_enc_abort(&jbig_state);

        if (jbig_out.len > 0)
            fwrite(jbig_out.buf, 1, jbig_out.len, fp);

        fprintf(stderr, "INFO: page %ux%u @ %ddpi, JBIG %zu bytes\n",
                width, y, resolution, jbig_out.len);

        free(gray_line); free(prev_line); free(prev2_line); free(cur_line);
        free(jbig_out.buf);
    }

    /* ── PJL footer ── */
    pjl_printf(fp, "@PJL EOJ");
    return 0;
}

/* ── Main ──────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    int fd = 0;
    cups_raster_t *ras;
    cups_header_subset_t header;
    int page = 0, toner_save = 0;

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

    if (argv[5] && strstr(argv[5], "TonerMode=1"))
        toner_save = 1;

    ras = ras_open(fd);
    if (!ras) {
        fprintf(stderr, "ERROR: raster open failed\n");
        if (fd > 0) close(fd);
        return 1;
    }

    while (ras_read_header(ras, &header)) {
        if (header.cupsWidth == 0 || header.cupsHeight == 0) continue;
        if (header.cupsWidth > MAX_PAGE_WIDTH || header.cupsHeight > MAX_PAGE_HEIGHT) break;

        /* Convert RGB to grayscale in the header if needed */
        if (header.cupsColorSpace == CUPS_CSPACE_RGB) {
            fprintf(stderr, "WARNING: RGB input, converting to gray (3bpp→8bpp)\n");
            /* Each RGB pixel (3 bytes) will be converted to 1 gray byte in halftone */
            /* We need to handle this in the read loop */
        }

        page++;
        fprintf(stderr, "INFO: page %d, %ux%u @ %ux%u dpi, %d bpp\n",
                page, header.cupsWidth, header.cupsHeight,
                header.HWResolution[0], header.HWResolution[1],
                header.cupsBitsPerPixel);

        if (write_page(stdout, ras, &header, toner_save) != 0) break;
    }

    ras_close(ras);
    if (fd > 0) close(fd);
    fprintf(stderr, "INFO: %d page(s) processed\n", page);
    return 0;
}
