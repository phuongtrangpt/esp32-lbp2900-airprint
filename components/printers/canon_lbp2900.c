// SPDX-License-Identifier: GPL-3.0-or-later
// LBP2900 raster adapter. Experimental until verified on a physical printer.
#include "printer_driver.h"
#include "mono_raster.h"
#include "capt_transport.h"
#include "printer.h"
#include "paper.h"
#include "capt-status.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

extern const struct printer_ops_s *capt_lbp2900_ops(void);
#define BAND_LINES 70
#define PAGE_CACHE_BANDS 160

typedef struct { uint8_t *payload; size_t size; } capt_cached_band_t;

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
    // Four dither phases; avoids re-packing identical input lines.
    uint8_t *row_cache;
    unsigned row_cache_valid;
    size_t raw_size, compressed_size;
    uint64_t compress_us, send_us;
    unsigned bands;
    capt_cached_band_t *cached_page;
    unsigned cached_count;
    bool cache_usable;
} job_t;

// All upstream operations run inside a guard: a malformed reply, disconnect,
// cancel or printer error aborts this job, never exit()s the firmware process.
#define GUARD(j) do { capt_bind(&(j)->link); if (setjmp((j)->link.guard)) { \
    (j)->failed = true; capt_log( "ERROR: CAPT: %s\n", (j)->link.error); return -1; \
    } capt_deadline(&(j)->link, 120000); } while (0)

static void clear_cached_page(job_t *j)
{
    if (j->cached_page) {
        for (unsigned i = 0; i < j->cached_count; i++)
            free(j->cached_page[i].payload);
        free(j->cached_page);
    }
    j->cached_page = NULL;
    j->cached_count = 0;
    j->cache_usable = false;
}

static void *cache_malloc(size_t n)
{
#ifdef ESP_PLATFORM
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return malloc(n);
#endif
}

static void dispose(job_t *j)
{
    clear_cached_page(j);
    free(j->raw); free(j->compressed); free(j->row_cache);
    free(j->link.buffer); free(j);
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
    j->row_cache = malloc(m->line_size * 4);
    j->link.buffer = malloc(65536);
    if (!j->raw || !j->compressed || !j->row_cache || !j->link.buffer) { dispose(j); return NULL; }
    return j;
}
static uint64_t micros(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000; }
static int flush_band(job_t *j)
{
    GUARD(j);   // compression and USB writes can raise a CAPT error

    bool last = (unsigned)j->output_rows == j->dims.num_lines;
    uint64_t start = micros();
    size_t n = j->state.ops->compress_band(&j->state, j->compressed, j->compressed_size,
        j->raw, j->dims.line_size, j->rows_in_band, last);
    if (!n || n >= j->compressed_size) capt_fail("CAPT compression overflow");
    j->compress_us += micros() - start;
    // Keep compressed CAPT bands for the current page in PSRAM, so paper-out
    // recovery can replay just the interrupted page after printer counters reset.
    if (j->cache_usable) {
        if (j->cached_count >= PAGE_CACHE_BANDS) {
            j->cache_usable = false;
        } else {
            uint8_t *copy = cache_malloc(n);
            if (!copy) {
                j->cache_usable = false;
            } else {
                memcpy(copy, j->compressed, n);
                j->cached_page[j->cached_count++] = (capt_cached_band_t){copy, n};
            }
        }
        if (!j->cache_usable)
            capt_log("WARNING: CAPT page replay cache unavailable; paper-out will abort safely\n");
    }
    start = micros();
    j->state.ops->send_band(&j->state, j->compressed, n);
    j->send_us += micros() - start;
    j->bands++;
    j->state.iband++;
    j->rows_in_band = 0;
    memset(j->raw, 0, j->raw_size);
    return 0;
}
static int page_begin(void *ctx, int width, int height, int bpp)
{
    job_t *j = ctx;
    if (j->failed || j->page_open || width <= 0 || width > 10000 || height <= 0 ||
        height > 10000 || (bpp != 1 && bpp != 3)) return -1;
    GUARD(j);
    clear_cached_page(j);
    j->cached_page = cache_malloc(sizeof(capt_cached_band_t) * PAGE_CACHE_BANDS);
    if (j->cached_page) {
        memset(j->cached_page, 0, sizeof(capt_cached_band_t) * PAGE_CACHE_BANDS);
        j->cache_usable = true;
    }
    j->width = width; j->height = height; j->bpp = bpp;
    j->next_y = j->output_rows = j->rows_in_band = 0;
    j->row_cache_valid = 0;
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
// Cached bitmap rows survive flush_band(): that routine clears j->raw.
// Phase-aware caching preserves the original 600 dpi ordered-dither pattern
// across PWG/URF repeat counts. For 300 dpi, two output rows intentionally
// share one 1-bit bitmap (nearest-neighbour 2x row replication).
static int append_row(job_t *j, const uint8_t *px, int output_y, bool reusable)
{
    uint8_t *out = j->raw + j->rows_in_band * j->dims.line_size;
    if (px) {
        int dither_y = j->scale == 2 ? (output_y & ~1) : output_y;
        unsigned phase = (unsigned)dither_y & 3u;
        uint8_t *cache = j->row_cache + phase * j->dims.line_size;
        if (reusable && (j->row_cache_valid & (1u << phase))) {
            memcpy(out, cache, j->dims.line_size);
        } else {
            int max = (j->driver->papers[j->paper].width
                       - j->driver->margin_left - j->driver->margin_right) * 600 / 2540;
            mono_pack(out, max, px, j->width, j->bpp, j->left,
                      j->scale == 2, dither_y);
            memcpy(cache, out, j->dims.line_size);
            j->row_cache_valid |= (1u << phase);
        }
    }
    j->rows_in_band++;
    j->output_rows++;
    if (j->rows_in_band == BAND_LINES ||
        (unsigned)j->output_rows == j->dims.num_lines)
        return flush_band(j);
    return 0;
}

static int page_line_repeat(void *ctx, int y, const uint8_t *px, bool is_repeat)
{
    job_t *j = ctx;
    if (j->failed || !j->page_open || y != j->next_y || y >= j->height)
        return -1;
    j->next_y++;
    if (!is_repeat)
        j->row_cache_valid = 0;
    for (int repeat = 0; repeat < j->scale; repeat++) {
        int output_y = y * j->scale + repeat;
        if (output_y >= j->top && output_y < j->top + (int)j->dims.num_lines &&
            append_row(j, px, output_y, is_repeat || repeat != 0))
            return -1;
    }
    return 0;
}

static int page_line(void *ctx, int y, const uint8_t *px)
{
    return page_line_repeat(ctx, y, px, false);
}
static int page_end(void *ctx)
{
    job_t *j = ctx;
    if (j->failed || !j->page_open || j->next_y != j->height) return -1;
    while ((unsigned)j->output_rows < j->dims.num_lines) {
        if (append_row(j, NULL, j->output_rows + j->top, false))
            return -1;
    }
    GUARD(j);
    // On out-of-paper, upstream CAPT recovery resets the printer page
    // counters and requests a replay. Re-send only the current page's cached
    // compressed bands; never replay pages already acknowledged by the engine.
    // Keep a bounded retry count to prevent a repeating paper-out loop.
    for (int retry = 0; ; retry++) {
        if (j->state.ops->page_epilogue(&j->state, &j->dims))
            break;
        if (!j->state.oop_retry_pending)
            capt_fail("CAPT page did not complete");
        j->state.oop_retry_pending = false;
        if (!j->cache_usable || j->cached_count == 0 || retry >= 3)
            capt_fail("CAPT paper-out replay unavailable or limit reached");
        capt_log("INFO: CAPT: replaying interrupted page %u after paper refill (attempt %d, bands=%u)\n",
                 j->state.ipage, retry + 1, j->cached_count);
        j->state.iband = j->state.isend = 0;
        if (!j->state.ops->page_prologue(&j->state, &j->dims))
            capt_fail("CAPT cannot restart page after paper refill");
        for (unsigned i = 0; i < j->cached_count; i++) {
            j->state.ops->send_band(&j->state, j->cached_page[i].payload, j->cached_page[i].size);
            j->state.iband++;
        }
    }
    j->page_open = false;
    clear_cached_page(j);
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
    .color=false, .scanner=false, .resolutions={300}, .nresolutions=1,
    .papers=papers, .npapers=4, .margin_left=500, .margin_right=450, .margin_top=400, .margin_bottom=400,
    .job_begin=begin, .page_begin=page_begin, .page_line=page_line,
    .page_line_repeat=page_line_repeat, .page_end=page_end, .job_end=end,
    .job_stats=stats, .test_page=test_page
};
