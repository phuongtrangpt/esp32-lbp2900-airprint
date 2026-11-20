// SPDX-License-Identifier: GPL-3.0-or-later
// LBP2900 raster adapter. Experimental until verified on a physical printer.
#include "printer_driver.h"
#include "capt_transport.h"
#include "printer.h"
#include "paper.h"
#include "capt-status.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

extern const struct printer_ops_s *capt_lbp2900_ops(void);
#define BAND_LINES 70

typedef struct {
    capt_transport_t link;
    struct printer_state_s state;
    struct page_dims_s dims;
    const printer_driver_t *driver;
    int scale, paper, width, height, bpp, next_y, row, output_rows;
    int left, top;
    unsigned rows_in_band;
    bool started, page_open, failed;
    uint8_t *raw, *compressed;
    size_t raw_size, compressed_size;
    uint64_t compress_us, send_us;
    unsigned bands;
} job_t;

// All upstream operations run inside a guard: a malformed reply, disconnect,
// cancel or printer error aborts this job, never exit()s the firmware process.
#define GUARD(j) do { capt_bind(&(j)->link); if (setjmp((j)->link.guard)) { \
    (j)->failed = true; capt_log( "ERROR: CAPT: %s\n", (j)->link.error); return -1; \
    } capt_deadline(&(j)->link, 120000); } while (0)

static void dispose(job_t *j)
{
    free(j->raw); free(j->compressed); free(j->link.buffer); free(j);
}
static void *begin(const printer_driver_t *d, const printer_io_t *io, int dpi, int paper)
{
    if ((dpi != 300 && dpi != 600) || paper < 0 || paper >= d->npapers || !io || !io->write || !io->read || !io->flush)
        return NULL;
    job_t *j = calloc(1, sizeof(*j));
    if (!j) return NULL;
    j->scale = 600 / dpi; j->driver = d; j->paper = paper; j->link.io = *io;
    j->state.ops = capt_lbp2900_ops();
    const printer_paper_t *p = &d->papers[paper];
    struct page_dims_s *m = &j->dims;
    m->paper_width = p->width * 600 / 2540;
    m->paper_height = p->height * 600 / 2540;
    m->margin_width = d->margin_left * 600 / 2540;
    m->margin_height = d->margin_top * 600 / 2540;
    unsigned pixels = (p->width - d->margin_left - d->margin_right) * 600 / 2540;
    m->line_size = (pixels + 7) / 8;
    m->num_lines = (p->height - d->margin_top - d->margin_bottom) * 600 / 2540;
    m->band_size = BAND_LINES; m->media_adapt = 0x11; m->media_type = 0x80; m->ink_k = 7;
    // A6 uses the upstream fallback paper id with explicit custom dimensions.
    // Physical feed validation is required; do not infer A6 hardware success.
    snprintf(m->media_size, sizeof(m->media_size), "%s", p->name);
    j->left = m->margin_width; j->top = m->margin_height;
    j->raw_size = m->line_size * BAND_LINES;
    j->compressed_size = j->raw_size * 2 + 64;
    j->raw = calloc(1, j->raw_size);
    j->compressed = malloc(j->compressed_size);
    j->link.buffer = malloc(65536);
    if (!j->raw || !j->compressed || !j->link.buffer) { dispose(j); return NULL; }
    return j;
}
static uint64_t micros(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000; }
static void flush_band(job_t *j)
{
    if (!j->rows_in_band) return;
    bool last = (unsigned)j->output_rows == j->dims.num_lines;
    uint64_t start = micros();
    size_t n = j->state.ops->compress_band(&j->state, j->compressed, j->compressed_size,
        j->raw, j->dims.line_size, j->rows_in_band, last);
    if (!n || n >= j->compressed_size) capt_fail("CAPT compression overflow");
    j->compress_us += micros() - start;
    start = micros();
    j->state.ops->send_band(&j->state, j->compressed, n);
    j->send_us += micros() - start;
    j->bands++;
    j->state.iband++;
    j->rows_in_band = 0;
    memset(j->raw, 0, j->raw_size);
}
static int page_begin(void *ctx, int width, int height, int bpp)
{
    job_t *j = ctx;
    if (j->failed || j->page_open || width <= 0 || width > 10000 || height <= 0 ||
        height > 10000 || (bpp != 1 && bpp != 3)) return -1;
    GUARD(j);
    j->width = width; j->height = height; j->bpp = bpp;
    j->next_y = j->output_rows = j->rows_in_band = 0;
    memset(j->raw, 0, j->raw_size);
    j->state.ipage++;
    j->state.iband = j->state.isend = 0;
    if (!j->started) {
        capt_init_status();
        j->started = true;
        j->state.ops->job_prologue(&j->state);
    }
    if (!j->state.ops->page_prologue(&j->state, &j->dims)) capt_fail("CAPT cannot start page");
    j->page_open = true;
    return 0;
}
static const uint8_t bayer[4][4] = {{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
static void append_row(job_t *j, const uint8_t *px, int y)
{
    uint8_t *out = j->raw + j->rows_in_band * j->dims.line_size;
    int max = (j->driver->papers[j->paper].width - j->driver->margin_left - j->driver->margin_right) * 600 / 2540;
    for (int x = 0; px && x < max && (x + j->left) / j->scale < j->width; x++) {
        const uint8_t *p = px + ((x + j->left) / j->scale) * j->bpp;
        unsigned gray = j->bpp == 1 ? p[0] : (77u*p[0] + 150u*p[1] + 29u*p[2]) >> 8;
        unsigned threshold = bayer[y & 3][x & 3] * 16 + 8;
        if (gray < threshold) out[x / 8] |= 0x80 >> (x & 7);
    }
    j->rows_in_band++; j->output_rows++;
    if (j->rows_in_band == BAND_LINES || (unsigned)j->output_rows == j->dims.num_lines) flush_band(j);
}
static int page_line(void *ctx, int y, const uint8_t *px)
{
    job_t *j = ctx;
    if (j->failed || !j->page_open || y != j->next_y || y >= j->height) return -1;
    GUARD(j);
    j->next_y++;
    for (int repeat = 0; repeat < j->scale; repeat++) {
        int output_y = y * j->scale + repeat;
        if (output_y >= j->top && output_y < j->top + (int)j->dims.num_lines) append_row(j, px, output_y);
    }
    return 0;
}
static int page_end(void *ctx)
{
    job_t *j = ctx;
    if (j->failed || !j->page_open || j->next_y != j->height) return -1;
    GUARD(j);
    while ((unsigned)j->output_rows < j->dims.num_lines) append_row(j, NULL, j->output_rows + j->top);
    if (!j->state.ops->page_epilogue(&j->state, &j->dims)) capt_fail("CAPT page did not complete");
    j->page_open = false;
    return 0;
}
static int end(void *ctx)
{
    job_t *j = ctx;
    volatile int result = j->failed || j->page_open ? -1 : 0;
    capt_bind(&j->link);
    if (setjmp(j->link.guard)) result = -1;
    else if (j->started && !j->failed) {
        capt_deadline(&j->link, 120000);
        if (j->page_open) j->state.ops->cancel_cleanup(&j->state);
        else j->state.ops->job_epilogue(&j->state);
    }
    dispose(j);
    capt_bind(NULL);
    return result;
}
static int test_page(const printer_driver_t *d, const printer_io_t *io, int dpi, const char *label)
{
    (void)label;
    job_t *j = begin(d, io, dpi, 0);
    if (!j) return -1;
    int w = d->papers[0].width * dpi / 2540, h = d->papers[0].height * dpi / 2540;
    uint8_t *line = malloc(w);
    if (!line) { end(j); return -1; }
    int rc = page_begin(j, w, h, 1);
    for (int y = 0; !rc && y < h; y++) {
        memset(line, 255, w);
        // A visible rectangular frame and four grayscale calibration blocks.
        if (y >= 400 && y < 1100) for (int x = 400; x < w - 400; x++) {
            if (y < 410 || y >= 1090 || x < 410 || x >= w - 410) line[x] = 0;
            else if (y >= 500 && y < 1000) line[x] = ((x - 410) * 4 / (w - 820)) * 64;
        }
        rc = page_line(j, y, line);
        if (io->progress && y % 64 == 0) io->progress(io->ctx, y, h);
    }
    if (!rc) rc = page_end(j);
    free(line);
    int final = end(j);
    return rc ? rc : final;
}
static void stats(void *ctx, char *buf, size_t n)
{
    job_t *j = ctx;
    snprintf(buf, n, "CAPT input=%d dpi bands=%u compression=%.3f s band-transfer-and-status=%.3f s",
             600/j->scale, j->bands, j->compress_us/1e6, j->send_us/1e6);
}
static const printer_paper_t papers[] = {
    {"A4", "iso_a4_210x297mm", 21000, 29700},
    {"Letter", "na_letter_8.5x11in", 21590, 27940},
    {"A5", "iso_a5_148x210mm", 14800, 21000},
    {"A6", "iso_a6_105x148mm", 10500, 14800}
};
const printer_driver_t canon_lbp2900_driver = {
    .id="canon-lbp2900", .make_model="Canon LBP2900 (experimental CAPT)",
    .usb_mfg="Canon", .usb_mdl="LBP2900", .usb_vid=0x04a9, .usb_pid=0x2676,
    .color=false, .scanner=false, .resolutions={600,300}, .nresolutions=2,
    .papers=papers, .npapers=4, .margin_left=500, .margin_right=450, .margin_top=400, .margin_bottom=400,
    .job_begin=begin, .page_begin=page_begin, .page_line=page_line, .page_end=page_end, .job_end=end,
    .job_stats=stats, .test_page=test_page
};
