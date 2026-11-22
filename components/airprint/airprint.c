// AirPrint / IPP Everywhere server. One thread per HTTP connection; a single
// job prints at a time and is streamed straight from the socket to the
// printer (raster line -> Canon raster -> printer), so documents of any size
// fit in a few tens of KB.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE     // strcasestr
#endif
#include "airprint.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "events.h"
#include "icon_png.h"
#include "ipp.h"
#include "printer_driver.h"
#include "raster.h"
#include "mem.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define LOGI(...) ESP_LOGI("airprint", __VA_ARGS__)
#define LOGW(...) ESP_LOGW("airprint", __VA_ARGS__)
#else
#define LOGI(fmt, ...) fprintf(stderr, "I airprint: " fmt "\n", ##__VA_ARGS__)
#define LOGW(fmt, ...) fprintf(stderr, "W airprint: " fmt "\n", ##__VA_ARGS__)
#endif

// iOS keeps several keep-alive connections open while it watches a job
#ifdef ESP_PLATFORM
// On ESP32-S3 each HTTP connection owns a 12 KiB pthread stack in internal RAM.
// Reserving too many at once can starve Wi-Fi/USB tasks while printing.
// Leave room for one print stream and two control/status connections.
#define MAX_CONNECTIONS 3
#define CONN_STACK_SIZE (12 * 1024)     // the print pipeline runs on this stack
#define LISTEN_STACK_SIZE (4 * 1024)
#else
#define MAX_CONNECTIONS 8
#define CONN_STACK_SIZE (512 * 1024)    // host build runs with sanitizers
#define LISTEN_STACK_SIZE (256 * 1024)
#endif
#ifdef ESP_PLATFORM
#define IDLE_TIMEOUT_S 4
#else
#define IDLE_TIMEOUT_S 15
#endif
#define ACTIVE_IPP_TIMEOUT_S 120
#define ACTIVE_IPP_SLICE_S 1
#define MAX_JOBS 10
#define CANON_MAX_JOBS 3
#define SPOOL_JOB_LIMIT (2 * 1024 * 1024)
#define SPOOL_TOTAL_LIMIT (4 * 1024 * 1024)
#define SPOOL_PSRAM_RESERVE (1024 * 1024)
#define SPOOL_UPLOAD_TIMEOUT_S 30
#define MULTIPLE_OP_TIMEOUT_S 60

#define MAX_PAPERS 8         // of a driver, in the IPP attributes

enum {
    JOB_PENDING = 3,
    JOB_HELD = 4,
    JOB_PROCESSING = 5,
    JOB_CANCELED = 7,
    JOB_ABORTED = 8,
    JOB_COMPLETED = 9,
};

typedef struct {
    int id;
    int state;
    char name[64];
    char user[32];
    char format[64];
    const char *reason;         // job-state-reasons keyword
    time_t created, processing, completed;
    int impressions;
    int64_t bytes;
    volatile int line, height;  // progress of the page being printed (read without the lock)
    atomic_bool cancel;
    uint64_t order;             // document admission order, zero until submitted
    uint8_t *spool;
    size_t spool_size, spool_capacity;
    bool web_print;             // document submitted through the web print dialog
    bool receiving;
    bool replying;              // retain the job until its IPP response is serialized
    bool printer_open;          // permits the hardware cancel hook while the device is in use
} job_t;

static const airprint_config_t *s_cfg;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static job_t s_jobs[MAX_JOBS];      // ring of recent jobs
static int s_next_id = 1;
static job_t *s_active;
static uint64_t s_next_order = 1;
static size_t s_spool_bytes;
static time_t s_start;
static int s_connections;
static atomic_bool s_waiting_for_paper;
static atomic_bool s_capt_restart_required;
static pthread_mutex_t s_link_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned s_link_generation;

void airprint_printer_reconnected(void)
{
    pthread_mutex_lock(&s_link_lock);
    ++s_link_generation;
    atomic_store(&s_capt_restart_required, false);
    atomic_store(&s_waiting_for_paper, false);
    pthread_mutex_unlock(&s_link_lock);
}

#ifdef ESP_PLATFORM
static unsigned s_refused_connections;
#endif

// ===========================================================================
// HTTP connection

struct airprint_http {
    int fd;
    int listener_port;
    uint8_t buf[2048];
    size_t pos, len;
    // request
    char method[8];
    char path[128];
    char host[96];
    char if_none_match[48];
    bool keep_alive;
    bool http10;                // HTTP/1.0 client: no chunked responses
    bool chunked_out;           // streaming response uses chunked encoding
    // body
    bool chunked;
    int64_t remaining;          // Content-Length mode: bytes left; chunked: bytes left in chunk
    bool body_eof;
    bool body_failed;
    int64_t upload_deadline;    // absolute timeout for a queued upload
    job_t *receiving_job;       // only set while an admitted job owns this connection
    bool in_chunks;             // a chunk was read: CRLF precedes the next size line
    bool dispatch_queue;
    bool detached;              // the socket was handed over (airprint_http_detach)
};
typedef struct airprint_http conn_t;

static int64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void set_recv_timeout(conn_t *c, int seconds)
{
    struct timeval timeout = {.tv_sec = seconds};
    setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}

static int conn_fill(conn_t *c)
{
    if (c->pos < c->len)
        return 0;
    ssize_t n = recv(c->fd, c->buf, sizeof(c->buf), 0);
    // Active print sockets check cancellation on short idle intervals.
    // The fast path still needs only one recv and no clock read.
    int64_t deadline = 0;
    while (c->receiving_job && n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        if (c->receiving_job->cancel)
            return -1;
        int64_t now = now_us();
        if (!deadline)
            deadline = now + (int64_t)(ACTIVE_IPP_TIMEOUT_S - ACTIVE_IPP_SLICE_S) * 1000000;
        if (now >= deadline || (c->upload_deadline && now >= c->upload_deadline))
            break;
        n = recv(c->fd, c->buf, sizeof(c->buf), 0);
    }
#ifdef ESP_PLATFORM
    if (n > 0 && deadline)
        LOGI("HTTP upload resumed after %.1f s idle on %s",
             (now_us() - deadline) / 1000000.0 + ACTIVE_IPP_TIMEOUT_S, c->path);
#endif
    if (n <= 0) {
#ifdef ESP_PLATFORM
        // Zero means peer closed. A timeout or network error is negative.
        // Include body bytes remaining to distinguish client cancellation
        // from a truncated raster/short HTTP upload.
        LOGW("HTTP recv %s: fd=%d errno=%d remaining=%lld chunked=%d path=%s",
             n == 0 ? "EOF" : "failed", c->fd, n == 0 ? 0 : errno,
             (long long)c->remaining, (int)c->chunked, c->path);
#endif
        return -1;
    }
    c->pos = 0;
    c->len = (size_t)n;
    return 0;
}

static int read_line(conn_t *c, char *out, size_t out_len)
{
    size_t n = 0;
    for (;;) {
        if (conn_fill(c))
            return -1;
        char ch = (char)c->buf[c->pos++];
        if (ch == '\n')
            break;
        if (ch != '\r' && n < out_len - 1)
            out[n++] = ch;
    }
    out[n] = 0;
    return (int)n;
}

// Sends in pieces through a stack buffer: the source may be in PSRAM, and
// one huge send() call has been seen to fail on lwIP.
static int send_all(int fd, const void *data, size_t len)
{
    const uint8_t *p = data;
    uint8_t chunk[1460];
    while (len) {
        size_t piece = len < sizeof(chunk) ? len : sizeof(chunk);
        memcpy(chunk, p, piece);
        size_t off = 0;
        while (off < piece) {
            ssize_t n = send(fd, chunk + off, piece - off, 0);
            if (n <= 0) {
                LOGW("send failed after %u bytes (errno %d)", (unsigned)(p - (const uint8_t *)data + off), errno);
                return -1;
            }
            off += (size_t)n;
        }
        p += piece;
        len -= piece;
    }
    return 0;
}

// Reads up to n body bytes; 0 at end of body, < 0 on error.
static int body_read_unchecked(conn_t *c, uint8_t *out, size_t n)
{
    if (c->body_eof)
        return 0;
    if (c->chunked && c->remaining == 0) {
        char line[64];
        if (c->in_chunks && read_line(c, line, sizeof(line)) != 0) {
            LOGW("HTTP missing chunk separator on %s", c->path);
            return -1;
        }
        if (read_line(c, line, sizeof(line)) <= 0)
            return -1;
        char *end;
        errno = 0;
        long long size = strtoll(line, &end, 16);
        if (!strchr("0123456789abcdefABCDEF", line[0]) ||
                errno == ERANGE || size < 0 || (*end && *end != ';')) {
            LOGW("HTTP invalid chunk size on %s", c->path);
            return -1;
        }
        c->in_chunks = true;
        if (size == 0) {
            int trailer;
            while ((trailer = read_line(c, line, sizeof(line))) > 0)
                ;
            if (trailer < 0)
                return -1;
            c->body_eof = true;
            return 0;
        }
        c->remaining = size;
    }
    if (c->remaining <= 0) {
        c->body_eof = true;
        return 0;
    }
    if (conn_fill(c))
        return -1;
    size_t take = c->len - c->pos;
    if (take > n)
        take = n;
    if ((int64_t)take > c->remaining)
        take = (size_t)c->remaining;
    memcpy(out, c->buf + c->pos, take);
    c->pos += take;
    c->remaining -= (int64_t)take;
    if (!c->chunked && c->remaining == 0)
        c->body_eof = true;
    return (int)take;
}

// A failed body read is terminal. In particular, draining must not wait
// through a second socket timeout after the decoder has already seen one.
static int body_read(conn_t *c, uint8_t *out, size_t n)
{
    if (c->body_failed)
        return -1;
    int result = body_read_unchecked(c, out, n);
    if (result < 0)
        c->body_failed = true;
    return result;
}

// exact read for the IPP parser
static int body_read_exact(void *ctx, uint8_t *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        int k = body_read(ctx, buf + got, n - got);
        if (k <= 0)
            return -1;
        got += (size_t)k;
    }
    return (int)n;
}

typedef struct {
    conn_t *c;
    job_t *job;
    int64_t wait_us;            // time spent waiting for document data
    size_t offset;
} doc_reader_t;

static int doc_read(void *ctx, uint8_t *buf, size_t n)
{
    doc_reader_t *d = ctx;
    if (!d->c) {
        size_t left = d->job->spool_size - d->offset;
        if (n > left)
            n = left;
        memcpy(buf, d->job->spool + d->offset, n);
        d->offset += n;
        d->job->bytes += n;
        return (int)n;
    }
    int64_t t0 = now_us();
    int k = body_read(d->c, buf, n);
    d->wait_us += now_us() - t0;
    if (k > 0)
        d->job->bytes += k;
    return k;
}

// Discards the rest of the request body. Clients only read the response once
// they have sent the whole body; closing with unread data would reset the
// connection and lose the response (libcups then retries the whole job).
static void drain_body(conn_t *c)
{
    uint8_t tmp[512];
    while (body_read(c, tmp, sizeof(tmp)) > 0)
        ;
    if (!c->body_eof)
        c->keep_alive = false;
}

static void send_http_ex(conn_t *c, const char *status, const char *type, const char *extra_headers,
                         const void *body, size_t len)
{
    char hdr[512];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n%sConnection: %s\r\n\r\n", status,
                     type, (unsigned)len, extra_headers ? extra_headers : "", c->keep_alive ? "keep-alive" : "close");
    if (n >= (int)sizeof(hdr)) {
        c->keep_alive = false;
        return;
    }
    if (send_all(c->fd, hdr, n) == 0 && len)
        send_all(c->fd, body, len);
}

static void send_http(conn_t *c, const char *status, const char *type, const void *body, size_t len)
{
    send_http_ex(c, status, type, NULL, body, len);
}

void airprint_http_send(airprint_http_t *c, const char *status, const char *type, const void *body, size_t len)
{
    send_http(c, status, type, body, len);
}

void airprint_http_send_ex(airprint_http_t *c, const char *status, const char *type, const char *extra_headers,
                           const void *body, size_t len)
{
    send_http_ex(c, status, type, extra_headers, body, len);
}

int airprint_http_read_body(airprint_http_t *c, void *buf, size_t max)
{
    size_t got = 0;
    while (got < max) {
        int k = body_read(c, (uint8_t *)buf + got, max - got);
        if (k < 0)
            return -1;
        if (k == 0)
            break;
        got += (size_t)k;
    }
    return (int)got;
}

const char *airprint_http_host(airprint_http_t *c)
{
    return c->host;
}

const char *airprint_http_if_none_match(airprint_http_t *c)
{
    return c->if_none_match;
}

int airprint_http_stream_begin(airprint_http_t *c, const char *type, const char *extra_headers)
{
    // a client that stops reading must not block the sender forever
    struct timeval tv = {.tv_sec = 30};
    setsockopt(c->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    c->chunked_out = !c->http10;
    if (!c->chunked_out)
        c->keep_alive = false;      // the end of the body is the end of the connection
    char hdr[512];
    int n = snprintf(hdr, sizeof(hdr), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n%s%sConnection: %s\r\n\r\n", type,
                     c->chunked_out ? "Transfer-Encoding: chunked\r\n" : "", extra_headers ? extra_headers : "",
                     c->keep_alive ? "keep-alive" : "close");
    if (n >= (int)sizeof(hdr) || send_all(c->fd, hdr, (size_t)n) < 0) {
        c->keep_alive = false;
        return -1;
    }
    return 0;
}

int airprint_http_stream_write(airprint_http_t *c, const void *data, size_t len)
{
    if (!len)
        return 0;
    if (c->chunked_out) {
        char size[16];
        int n = snprintf(size, sizeof(size), "%x\r\n", (unsigned)len);
        if (send_all(c->fd, size, (size_t)n) < 0 || send_all(c->fd, data, len) < 0 || send_all(c->fd, "\r\n", 2) < 0) {
            c->keep_alive = false;
            return -1;
        }
        return 0;
    }
    if (send_all(c->fd, data, len) < 0) {
        c->keep_alive = false;
        return -1;
    }
    return 0;
}

int airprint_http_detach(airprint_http_t *c)
{
    c->detached = true;
    c->keep_alive = false;
    return c->fd;
}

int airprint_http_stream_end(airprint_http_t *c)
{
    struct timeval tv = {0};
    setsockopt(c->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (c->chunked_out && send_all(c->fd, "0\r\n\r\n", 5) < 0) {
        c->keep_alive = false;
        return -1;
    }
    return 0;
}

// ===========================================================================
// printer attributes

typedef struct {
    const ipp_request_t *req;
    int idx;                    // requested-attributes index, -1 = all
    bool all;
} filter_t;

static void filter_init(filter_t *f, const ipp_request_t *req)
{
    f->req = req;
    f->idx = ipp_find_index(req, IPP_TAG_OPERATION, "requested-attributes");
    f->all = f->idx < 0;
    for (int i = f->idx; i >= 0 && i < req->nattrs && (i == f->idx || req->attrs[i].name_len == 0); i++) {
        const ipp_attr_t *a = &req->attrs[i];
        if (ipp_attr_is(a, "all") || ipp_attr_is(a, "printer-description") || ipp_attr_is(a, "job-template"))
            f->all = true;
    }
}

static bool want(const filter_t *f, const char *name)
{
    bool explicit_only = strcmp(name, "media-col-database") == 0;
    if (f->all && !explicit_only)
        return true;
    for (int i = f->idx; i >= 0 && i < f->req->nattrs && (i == f->idx || f->req->attrs[i].name_len == 0); i++)
        if (ipp_attr_is(&f->req->attrs[i], name))
            return true;
    return false;
}

static void uri_for(const conn_t *c, char *out, size_t n, const char *scheme, const char *path)
{
    snprintf(out, n, "%s://%s%s", scheme, c->host[0] ? c->host : "localhost", path);
}

static void media_size(ipp_out_t *o, const char *member, int x, int y)
{
    if (member)
        ipp_out_member(o, member);
    ipp_out_coll_begin(o, NULL);
    ipp_out_member(o, "x-dimension");
    ipp_out_int(o, IPP_TAG_INTEGER, NULL, x);
    ipp_out_member(o, "y-dimension");
    ipp_out_int(o, IPP_TAG_INTEGER, NULL, y);
    ipp_out_coll_end(o);
}

// one media-col value: name == NULL adds another value to the attribute
static void media_col(ipp_out_t *o, const char *name, const char *key, int x, int y)
{
    ipp_out_coll_begin(o, name);
    ipp_out_member(o, "media-key");
    ipp_out_str(o, IPP_TAG_KEYWORD, NULL, key);
    media_size(o, "media-size", x, y);
    ipp_out_member(o, "media-size-name");
    ipp_out_str(o, IPP_TAG_KEYWORD, NULL, key);
    ipp_out_member(o, "media-left-margin");
    ipp_out_int(o, IPP_TAG_INTEGER, NULL, s_cfg->driver->margin_left);
    ipp_out_member(o, "media-right-margin");
    ipp_out_int(o, IPP_TAG_INTEGER, NULL, s_cfg->driver->margin_right);
    ipp_out_member(o, "media-top-margin");
    ipp_out_int(o, IPP_TAG_INTEGER, NULL, s_cfg->driver->margin_top);
    ipp_out_member(o, "media-bottom-margin");
    ipp_out_int(o, IPP_TAG_INTEGER, NULL, s_cfg->driver->margin_bottom);
    ipp_out_member(o, "media-source");
    ipp_out_str(o, IPP_TAG_KEYWORD, NULL, "auto");
    ipp_out_member(o, "media-type");
    ipp_out_str(o, IPP_TAG_KEYWORD, NULL, "stationery");
    ipp_out_coll_end(o);
}

// media-col-database / -ready: one value per paper of the driver
static void media_cols(ipp_out_t *o, const char *name)
{
    const printer_driver_t *d = s_cfg->driver;
    for (int i = 0; i < d->npapers; i++)
        media_col(o, i ? NULL : name, d->papers[i].pwg, d->papers[i].width, d->papers[i].height);
}

// the papers' names, NULL-terminated
static const char **media_names(const char *names[MAX_PAPERS + 1])
{
    const printer_driver_t *d = s_cfg->driver;
    int n = 0;
    for (; n < d->npapers && n < MAX_PAPERS; n++)
        names[n] = d->papers[n].pwg;
    names[n] = NULL;
    return names;
}

static void resolutions(ipp_out_t *o, const char *name)
{
    const printer_driver_t *d = s_cfg->driver;
    for (int i = 0; i < d->nresolutions; i++)
        ipp_out_res(o, i ? NULL : name, d->resolutions[i], d->resolutions[i]);
}

static void get_status(airprint_status_t *st)
{
    memset(st, 0, sizeof(*st));
    if (s_cfg->get_status)
        s_cfg->get_status(s_cfg->ctx, st);
    else
        st->connected = true;
    if (st->connected && atomic_load(&s_waiting_for_paper)) {
        if (st->nreasons < (int)(sizeof(st->reasons) / sizeof(st->reasons[0])))
            st->reasons[st->nreasons++] = "media-empty-error";
        snprintf(st->message, sizeof(st->message), "Hết giấy. Nạp thêm giấy rồi bấm nút giấy trên Canon.");
    } else if (st->connected && !strcmp(s_cfg->driver->id, "canon-lbp2900") &&
               atomic_load(&s_capt_restart_required)) {
        if (st->nreasons < (int)(sizeof(st->reasons) / sizeof(st->reasons[0])))
            st->reasons[st->nreasons++] = "other-error";
        snprintf(st->message, sizeof(st->message), "Máy in bị treo. Hãy tắt rồi bật lại máy Canon để tiếp tục.");
    }
}

// printer-state from the job table and the printer
static bool queue_enabled(void)
{
    return !strcmp(s_cfg->driver->id, "canon-lbp2900");
}

static int job_limit(void)
{
    return queue_enabled() ? CANON_MAX_JOBS : 1;
}

// The job table and queue fields are protected by s_lock.
static int outstanding_jobs(void)
{
    int count = 0;
    for (int i = 0; i < MAX_JOBS; i++)
        if (s_jobs[i].id && s_jobs[i].state < JOB_CANCELED)
            count++;
    return count;
}

static int printer_state(const airprint_status_t *st)
{
    if (!st->connected)
        return 5;   // stopped
    return (s_active && s_active->state == JOB_PROCESSING) || st->busy ? 4 : 3;
}

static void add_printer_attrs(ipp_out_t *o, const conn_t *c, const filter_t *f)
{
    const airprint_config_t *cfg = s_cfg;
    const printer_driver_t *drv = cfg->driver;
    const char *papers[MAX_PAPERS + 1];
    airprint_status_t st;
    get_status(&st);
    time_t now = time(NULL);
    char uri[160];

#define W(name) want(f, name)
    if (W("charset-configured"))
        ipp_out_str(o, IPP_TAG_CHARSET, "charset-configured", "utf-8");
    if (W("charset-supported"))
        ipp_out_strs(o, IPP_TAG_CHARSET, "charset-supported", (const char *[]){"us-ascii", "utf-8", NULL});
    if (W("color-supported"))
        ipp_out_bool(o, "color-supported", drv->color);
    if (W("compression-supported"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "compression-supported", "none");
    if (W("copies-default"))
        ipp_out_int(o, IPP_TAG_INTEGER, "copies-default", 1);
    if (W("copies-supported"))
        ipp_out_range(o, "copies-supported", 1, 1);
    if (W("document-format-default"))
        ipp_out_str(o, IPP_TAG_MIMETYPE, "document-format-default", "application/octet-stream");
    if (W("document-format-supported"))
        ipp_out_strs(o, IPP_TAG_MIMETYPE, "document-format-supported",
                     (const char *[]){"application/octet-stream", "image/pwg-raster", "image/urf", NULL});
    if (W("finishings-default"))
        ipp_out_int(o, IPP_TAG_ENUM, "finishings-default", 3);
    if (W("finishings-supported"))
        ipp_out_int(o, IPP_TAG_ENUM, "finishings-supported", 3);
    if (W("generated-natural-language-supported"))
        ipp_out_str(o, IPP_TAG_LANGUAGE, "generated-natural-language-supported", "en");
    if (W("identify-actions-default"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "identify-actions-default", "flash");
    if (W("identify-actions-supported"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "identify-actions-supported", "flash");
    if (W("ipp-features-supported"))
        ipp_out_strs(o, IPP_TAG_KEYWORD, "ipp-features-supported",
                     (const char *[]){"airprint-1.4", "ipp-everywhere", NULL});
    if (W("ipp-versions-supported"))
        ipp_out_strs(o, IPP_TAG_KEYWORD, "ipp-versions-supported", (const char *[]){"1.1", "2.0", NULL});
    if (W("job-creation-attributes-supported"))
        ipp_out_strs(o, IPP_TAG_KEYWORD, "job-creation-attributes-supported",
                     (const char *[]){"copies", "finishings", "media", "media-col", "orientation-requested",
                                      "output-bin", "print-color-mode", "print-content-optimize", "print-quality",
                                      "print-rendering-intent", "printer-resolution", "sides", NULL});
    if (W("job-ids-supported"))
        ipp_out_bool(o, "job-ids-supported", true);
    if (W("media-bottom-margin-supported"))
        ipp_out_int(o, IPP_TAG_INTEGER, "media-bottom-margin-supported", drv->margin_bottom);
    if (W("media-left-margin-supported"))
        ipp_out_int(o, IPP_TAG_INTEGER, "media-left-margin-supported", drv->margin_left);
    if (W("media-right-margin-supported"))
        ipp_out_int(o, IPP_TAG_INTEGER, "media-right-margin-supported", drv->margin_right);
    if (W("media-top-margin-supported"))
        ipp_out_int(o, IPP_TAG_INTEGER, "media-top-margin-supported", drv->margin_top);
    if (W("media-col-database"))
        media_cols(o, "media-col-database");
    if (W("media-col-default"))
        media_col(o, "media-col-default", drv->papers[0].pwg, drv->papers[0].width, drv->papers[0].height);
    if (W("media-col-ready"))
        media_cols(o, "media-col-ready");
    if (W("media-col-supported"))
        ipp_out_strs(o, IPP_TAG_KEYWORD, "media-col-supported",
                     (const char *[]){"media-bottom-margin", "media-left-margin", "media-right-margin", "media-size",
                                      "media-size-name", "media-source", "media-top-margin", "media-type", NULL});
    if (W("media-default"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "media-default", drv->papers[0].pwg);
    if (W("media-ready"))
        ipp_out_strs(o, IPP_TAG_KEYWORD, "media-ready", media_names(papers));
    if (W("media-size-supported")) {
        for (int i = 0; i < drv->npapers; i++) {
            ipp_out_coll_begin(o, i ? NULL : "media-size-supported");
            ipp_out_member(o, "x-dimension");
            ipp_out_int(o, IPP_TAG_INTEGER, NULL, drv->papers[i].width);
            ipp_out_member(o, "y-dimension");
            ipp_out_int(o, IPP_TAG_INTEGER, NULL, drv->papers[i].height);
            ipp_out_coll_end(o);
        }
    }
    if (W("media-source-supported"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "media-source-supported", "auto");
    if (W("media-supported"))
        ipp_out_strs(o, IPP_TAG_KEYWORD, "media-supported", media_names(papers));
    if (W("media-type-supported"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "media-type-supported", "stationery");
    if (W("multiple-document-handling-supported"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "multiple-document-handling-supported",
                    "separate-documents-uncollated-copies");
    if (W("multiple-document-jobs-supported"))
        ipp_out_bool(o, "multiple-document-jobs-supported", false);
    if (W("multiple-operation-time-out"))
        ipp_out_int(o, IPP_TAG_INTEGER, "multiple-operation-time-out", MULTIPLE_OP_TIMEOUT_S);
    if (W("multiple-operation-time-out-action"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "multiple-operation-time-out-action", "abort-job");
    if (W("natural-language-configured"))
        ipp_out_str(o, IPP_TAG_LANGUAGE, "natural-language-configured", "en");
    if (W("operations-supported")) {
        static const int ops[] = {IPP_OP_PRINT_JOB, IPP_OP_VALIDATE_JOB, IPP_OP_CREATE_JOB, IPP_OP_SEND_DOCUMENT,
                                  IPP_OP_CANCEL_JOB, IPP_OP_GET_JOB_ATTRIBUTES, IPP_OP_GET_JOBS,
                                  IPP_OP_GET_PRINTER_ATTRIBUTES, IPP_OP_CANCEL_MY_JOBS, IPP_OP_CLOSE_JOB,
                                  IPP_OP_IDENTIFY_PRINTER};
        for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++)
            ipp_out_int(o, IPP_TAG_ENUM, i == 0 ? "operations-supported" : NULL, ops[i]);
    }
    if (W("orientation-requested-default"))
        ipp_out_int(o, IPP_TAG_ENUM, "orientation-requested-default", 3);
    if (W("orientation-requested-supported"))
        ipp_out_int(o, IPP_TAG_ENUM, "orientation-requested-supported", 3);
    if (W("output-bin-default"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "output-bin-default", "face-up");
    if (W("output-bin-supported"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "output-bin-supported", "face-up");
    if (W("overrides-supported"))
        ipp_out_strs(o, IPP_TAG_KEYWORD, "overrides-supported", (const char *[]){"document-number", "pages", NULL});
    if (W("page-ranges-supported"))
        ipp_out_bool(o, "page-ranges-supported", false);
    if (W("pages-per-minute"))
        ipp_out_int(o, IPP_TAG_INTEGER, "pages-per-minute", 8);
    if (drv->color && W("pages-per-minute-color"))
        ipp_out_int(o, IPP_TAG_INTEGER, "pages-per-minute-color", 4);
    if (W("pdl-override-supported"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "pdl-override-supported", "attempted");
    if (W("preferred-attributes-supported"))
        ipp_out_bool(o, "preferred-attributes-supported", false);
    if (W("print-color-mode-default"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "print-color-mode-default", drv->color ? "color" : "monochrome");
    if (W("print-color-mode-supported")) {
        if (drv->color)
            ipp_out_strs(o, IPP_TAG_KEYWORD, "print-color-mode-supported",
                         (const char *[]){"auto", "color", "monochrome", NULL});
        else
            ipp_out_str(o, IPP_TAG_KEYWORD, "print-color-mode-supported", "monochrome");
    }
    if (W("print-content-optimize-default"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "print-content-optimize-default", "auto");
    if (W("print-content-optimize-supported"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "print-content-optimize-supported", "auto");
    if (W("print-quality-default"))
        ipp_out_int(o, IPP_TAG_ENUM, "print-quality-default", 4);
    if (W("print-quality-supported")) {
        ipp_out_int(o, IPP_TAG_ENUM, "print-quality-supported", 3);
        ipp_out_int(o, IPP_TAG_ENUM, NULL, 4);
        ipp_out_int(o, IPP_TAG_ENUM, NULL, 5);
    }
    if (W("print-rendering-intent-default"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "print-rendering-intent-default", "auto");
    if (W("print-rendering-intent-supported"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "print-rendering-intent-supported", "auto");
    if (W("printer-config-change-date-time"))
        ipp_out_date(o, "printer-config-change-date-time", s_start);
    if (W("printer-config-change-time"))
        ipp_out_int(o, IPP_TAG_INTEGER, "printer-config-change-time", 0);
    if (W("printer-current-time"))
        ipp_out_date(o, "printer-current-time", now);
    if (W("printer-device-id"))
        ipp_out_str(o, IPP_TAG_TEXT, "printer-device-id", cfg->device_id);
    if (W("printer-geo-location"))
        ipp_out_raw(o, IPP_TAG_UNKNOWN, "printer-geo-location", NULL, 0);
    if (W("printer-get-attributes-supported"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "printer-get-attributes-supported", "document-format");
    if (W("printer-icons")) {
        uri_for(c, uri, sizeof(uri), "http", "/icon.png");
        ipp_out_str(o, IPP_TAG_URI, "printer-icons", uri);
    }
    if (W("printer-info"))
        ipp_out_str(o, IPP_TAG_TEXT, "printer-info", cfg->name);
    if (W("printer-is-accepting-jobs"))
        ipp_out_bool(o, "printer-is-accepting-jobs", st.connected);
    if (W("printer-kind"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "printer-kind", "document");
    if (W("printer-location"))
        ipp_out_str(o, IPP_TAG_TEXT, "printer-location", "");
    if (W("printer-make-and-model"))
        ipp_out_str(o, IPP_TAG_TEXT, "printer-make-and-model", cfg->make_model);
    if (W("printer-more-info")) {
        uri_for(c, uri, sizeof(uri), "http", "/");
        ipp_out_str(o, IPP_TAG_URI, "printer-more-info", uri);
    }
    if (W("printer-name"))
        ipp_out_str(o, IPP_TAG_NAME, "printer-name", cfg->name);
    if (W("printer-organization"))
        ipp_out_str(o, IPP_TAG_TEXT, "printer-organization", "");
    if (W("printer-organizational-unit"))
        ipp_out_str(o, IPP_TAG_TEXT, "printer-organizational-unit", "");
    if (W("printer-resolution-default")) {
        int dpi = drv->resolutions[drv->nresolutions - 1];
        ipp_out_res(o, "printer-resolution-default", dpi, dpi);
    }
    if (W("printer-resolution-supported"))
        resolutions(o, "printer-resolution-supported");
    if (W("printer-state"))
        ipp_out_int(o, IPP_TAG_ENUM, "printer-state", printer_state(&st));
    if (W("printer-state-change-date-time"))
        ipp_out_date(o, "printer-state-change-date-time", s_start);
    if (W("printer-state-change-time"))
        ipp_out_int(o, IPP_TAG_INTEGER, "printer-state-change-time", 0);
    if (W("printer-state-message"))
        ipp_out_str(o, IPP_TAG_TEXT, "printer-state-message",
                    st.message[0] ? st.message : st.connected ? "Ready" : "Printer not connected");
    if (W("printer-state-reasons")) {
        if (!st.connected) {
            ipp_out_str(o, IPP_TAG_KEYWORD, "printer-state-reasons", "offline-report");
        } else if (st.nreasons == 0) {
            ipp_out_str(o, IPP_TAG_KEYWORD, "printer-state-reasons", "none");
        } else {
            for (int i = 0; i < st.nreasons; i++)
                ipp_out_str(o, IPP_TAG_KEYWORD, i == 0 ? "printer-state-reasons" : NULL, st.reasons[i]);
        }
    }
    // supplies: printer-supply (PWG 5100.13) and the older marker-* attributes
    if (st.nsupplies > 0) {
        if (W("printer-supply")) {
            for (int i = 0; i < st.nsupplies; i++) {
                char s[200];
                int n = snprintf(s, sizeof(s),
                                 "index=%d;class=supplyThatIsConsumed;type=ink;unit=percent;maxcapacity=100;level=%d;"
                                 "colorantname=%s;",
                                 i + 1, st.supply[i].level, i == 0 ? "black" : "multi-color");
                ipp_out_raw(o, IPP_TAG_STRING, i == 0 ? "printer-supply" : NULL, s, n);
            }
        }
        if (W("printer-supply-description"))
            for (int i = 0; i < st.nsupplies; i++)
                ipp_out_str(o, IPP_TAG_TEXT, i == 0 ? "printer-supply-description" : NULL, st.supply[i].name);
        if (W("marker-names"))
            for (int i = 0; i < st.nsupplies; i++)
                ipp_out_str(o, IPP_TAG_NAME, i == 0 ? "marker-names" : NULL, st.supply[i].name);
        if (W("marker-colors"))
            for (int i = 0; i < st.nsupplies; i++)
                ipp_out_str(o, IPP_TAG_NAME, i == 0 ? "marker-colors" : NULL, st.supply[i].color);
        if (W("marker-levels"))
            for (int i = 0; i < st.nsupplies; i++)
                ipp_out_int(o, IPP_TAG_INTEGER, i == 0 ? "marker-levels" : NULL, st.supply[i].level);
        if (W("marker-low-levels"))
            for (int i = 0; i < st.nsupplies; i++)
                ipp_out_int(o, IPP_TAG_INTEGER, i == 0 ? "marker-low-levels" : NULL, 10);
        if (W("marker-high-levels"))
            for (int i = 0; i < st.nsupplies; i++)
                ipp_out_int(o, IPP_TAG_INTEGER, i == 0 ? "marker-high-levels" : NULL, 100);
        if (W("marker-types"))
            for (int i = 0; i < st.nsupplies; i++)
                ipp_out_str(o, IPP_TAG_KEYWORD, i == 0 ? "marker-types" : NULL, "ink-cartridge");
    } else {
        // the attributes must exist, with matching counts
        if (W("printer-supply"))
            ipp_out_raw(o, IPP_TAG_NOVALUE, "printer-supply", NULL, 0);
        if (W("printer-supply-description"))
            ipp_out_raw(o, IPP_TAG_NOVALUE, "printer-supply-description", NULL, 0);
    }
    if (W("printer-supply-info-uri")) {
        uri_for(c, uri, sizeof(uri), "http", "/");
        ipp_out_str(o, IPP_TAG_URI, "printer-supply-info-uri", uri);
    }
    if (W("printer-up-time"))
        ipp_out_int(o, IPP_TAG_INTEGER, "printer-up-time", (int32_t)(now - s_start) + 1);
    if (W("printer-uri-supported")) {
        uri_for(c, uri, sizeof(uri), "ipp", "/ipp/print");
        ipp_out_str(o, IPP_TAG_URI, "printer-uri-supported", uri);
    }
    if (W("printer-uuid")) {
        snprintf(uri, sizeof(uri), "urn:uuid:%s", cfg->uuid);
        ipp_out_str(o, IPP_TAG_URI, "printer-uuid", uri);
    }
    if (W("pwg-raster-document-resolution-supported"))
        resolutions(o, "pwg-raster-document-resolution-supported");
    if (W("pwg-raster-document-sheet-back"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "pwg-raster-document-sheet-back", "normal");
    if (W("pwg-raster-document-type-supported"))
        ipp_out_strs(o, IPP_TAG_KEYWORD, "pwg-raster-document-type-supported",
                     drv->color ? (const char *[]){"sgray_8", "srgb_8", NULL} : (const char *[]){"sgray_8", NULL});
    if (W("queued-job-count"))
        ipp_out_int(o, IPP_TAG_INTEGER, "queued-job-count", outstanding_jobs());
    if (W("sides-default"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "sides-default", "one-sided");
    if (W("sides-supported"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "sides-supported", "one-sided");
    if (W("uri-authentication-supported"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "uri-authentication-supported", "none");
    if (W("uri-security-supported"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "uri-security-supported", "none");
    if (W("urf-supported")) {
        // comma separated list -> 1setOf keyword
        char urf[64];
        printer_driver_urf(drv, urf, sizeof(urf));
        char *save = NULL;
        int i = 0;
        for (char *t = strtok_r(urf, ",", &save); t; t = strtok_r(NULL, ",", &save), i++)
            ipp_out_str(o, IPP_TAG_KEYWORD, i == 0 ? "urf-supported" : NULL, t);
    }
    if (W("which-jobs-supported"))
        ipp_out_strs(o, IPP_TAG_KEYWORD, "which-jobs-supported",
                     (const char *[]){"completed", "not-completed", "all", NULL});
#undef W
}

// ===========================================================================
// jobs (s_lock held by callers)

static const char *job_reason(const job_t *j)
{
    if (j->reason)
        return j->reason;
    switch (j->state) {
    case JOB_PENDING: return j->spool_size ? "job-queued" : "job-incoming";
    case JOB_PROCESSING: return "job-printing";
    case JOB_CANCELED: return "job-canceled-by-user";
    case JOB_ABORTED: return "aborted-by-system";
    case JOB_COMPLETED: return "job-completed-successfully";
    default: return "none";
    }
}

static void add_job_attrs(ipp_out_t *o, const conn_t *c, const job_t *j, const filter_t *f)
{
    char uri[160], path[48];
#define W(name) (!f || want(f, name))
    if (W("job-id"))
        ipp_out_int(o, IPP_TAG_INTEGER, "job-id", j->id);
    if (W("job-uri")) {
        snprintf(path, sizeof(path), "/ipp/print/%d", j->id);
        uri_for(c, uri, sizeof(uri), "ipp", path);
        ipp_out_str(o, IPP_TAG_URI, "job-uri", uri);
    }
    if (W("job-printer-uri")) {
        uri_for(c, uri, sizeof(uri), "ipp", "/ipp/print");
        ipp_out_str(o, IPP_TAG_URI, "job-printer-uri", uri);
    }
    if (W("job-state"))
        ipp_out_int(o, IPP_TAG_ENUM, "job-state", j->state);
    if (W("job-state-reasons"))
        ipp_out_str(o, IPP_TAG_KEYWORD, "job-state-reasons", job_reason(j));
    if (f) {    // the short form (Print-Job/Create-Job responses) stops here
        if (W("job-name"))
            ipp_out_str(o, IPP_TAG_NAME, "job-name", j->name[0] ? j->name : "Untitled");
        if (W("job-originating-user-name"))
            ipp_out_str(o, IPP_TAG_NAME, "job-originating-user-name", j->user[0] ? j->user : "anonymous");
        if (W("job-impressions-completed"))
            ipp_out_int(o, IPP_TAG_INTEGER, "job-impressions-completed", j->impressions);
        if (W("job-k-octets-processed"))
            ipp_out_int(o, IPP_TAG_INTEGER, "job-k-octets-processed", (int32_t)(j->bytes / 1024));
        if (W("document-format") && j->format[0])
            ipp_out_str(o, IPP_TAG_MIMETYPE, "document-format", j->format);
        if (W("job-printer-up-time"))
            ipp_out_int(o, IPP_TAG_INTEGER, "job-printer-up-time", (int32_t)(time(NULL) - s_start) + 1);
        if (W("time-at-creation"))
            ipp_out_int(o, IPP_TAG_INTEGER, "time-at-creation", (int32_t)(j->created - s_start) + 1);
        if (W("time-at-processing")) {
            if (j->processing)
                ipp_out_int(o, IPP_TAG_INTEGER, "time-at-processing", (int32_t)(j->processing - s_start) + 1);
            else
                ipp_out_raw(o, IPP_TAG_NOVALUE, "time-at-processing", NULL, 0);
        }
        if (W("time-at-completed")) {
            if (j->completed)
                ipp_out_int(o, IPP_TAG_INTEGER, "time-at-completed", (int32_t)(j->completed - s_start) + 1);
            else
                ipp_out_raw(o, IPP_TAG_NOVALUE, "time-at-completed", NULL, 0);
        }
        if (W("date-time-at-creation"))
            ipp_out_date(o, "date-time-at-creation", j->created);
    }
#undef W
}

static job_t *job_find(int id)
{
    for (int i = 0; i < MAX_JOBS; i++)
        if (s_jobs[i].id == id && id > 0)
            return &s_jobs[i];
    return NULL;
}

// Iterates by ID even when a long-running job predates the recent history.
static job_t *job_before(int id, bool live_only)
{
    job_t *latest = NULL;
    for (int i = 0; i < MAX_JOBS; i++) {
        job_t *job = &s_jobs[i];
        if (job->id && job->id < id && (!live_only || job->state < JOB_CANCELED) &&
            (!latest || job->id > latest->id))
            latest = job;
    }
    return latest;
}

static bool job_done(const job_t *j)
{
    return j->state >= JOB_CANCELED;
}

static job_t *next_document(void)
{
    job_t *next = NULL;
    for (int i = 0; i < MAX_JOBS; i++) {
        job_t *j = &s_jobs[i];
        if (j->state == JOB_PENDING && j->order && (!next || j->order < next->order))
            next = j;
    }
    return next;
}

static int queue_position(const job_t *j)
{
    if (j->state != JOB_PENDING || !j->order)
        return 0;
    int position = 1;
    for (int i = 0; i < MAX_JOBS; i++)
        if (s_jobs[i].state == JOB_PENDING && s_jobs[i].order && s_jobs[i].order < j->order)
            position++;
    return position;
}

static void job_finish(job_t *j, int state)
{
    free(j->spool);
    s_spool_bytes -= j->spool_capacity;
    j->spool = NULL;
    j->spool_size = j->spool_capacity = 0;
    j->receiving = false;
    j->state = state;
    j->completed = time(NULL);
    if (s_active == j)
        s_active = NULL;
}

// Aborts Create-Job reservations whose documents never arrived.
static void expire_pending(void)
{
    time_t now = time(NULL);
    for (int i = 0; i < MAX_JOBS; i++) {
        job_t *j = &s_jobs[i];
        if (j->state == JOB_PENDING && !j->order && now - j->created > MULTIPLE_OP_TIMEOUT_S) {
            LOGW("job %d: no document within %d s, aborted", j->id, MULTIPLE_OP_TIMEOUT_S);
            j->reason = "aborted-by-system";
            job_finish(j, JOB_ABORTED);
        }
    }
}

static job_t *job_create(const ipp_request_t *req)
{
    job_t *j = NULL;
    for (int i = 0; i < MAX_JOBS; i++) {
        job_t *slot = &s_jobs[i];
        if (!slot->id || (job_done(slot) && !slot->replying && (!j || slot->id < j->id)))
            j = slot;
        if (!slot->id)
            break;
    }
    if (!j)
        return NULL;
    memset(j, 0, sizeof(*j));
    atomic_init(&j->cancel, false);
    j->id = s_next_id++;
    j->state = JOB_PENDING;
    j->created = time(NULL);
    ipp_attr_str(ipp_find(req, IPP_TAG_OPERATION, "job-name"), j->name, sizeof(j->name));
    ipp_attr_str(ipp_find(req, IPP_TAG_OPERATION, "requesting-user-name"), j->user, sizeof(j->user));
    return j;
}

// ===========================================================================
// print pipeline: raster -> printer driver -> job_write

typedef struct {
    job_t *job;
    int64_t read_us, flush_us;
    int64_t out_us;             // time spent in the printer output hook
    int64_t render_us;          // time spent converting lines (render + dither + compress)
    int64_t last_yield_us;      // per-job watchdog cooperative yield clock
    uint32_t bytes_out;         // printer data so far
    printer_io_t io;
    void *drv_job;              // the driver's job, once the first page started
    unsigned link_generation;  // reject status callbacks from an unplugged session
    int dpi;
    int downsample_width, downsample_height, downsample_bpp;
    uint8_t *downsample_row;
    int err;                    // first failure, an IPP status
} pipe_t;

static void pipe_paper_out(void *ctx, bool waiting)
{
    pipe_t *p = ctx;
    pthread_mutex_lock(&s_link_lock);
    if (p->link_generation == s_link_generation)
        atomic_store(&s_waiting_for_paper, waiting);
    pthread_mutex_unlock(&s_link_lock);
}

static bool pipe_canceled(void *ctx)
{
    pipe_t *p = ctx;
    return p->job->cancel;
}

static void pipe_restart_required(void *ctx, bool required)
{
    pipe_t *p = ctx;
    pthread_mutex_lock(&s_link_lock);
    if (p->link_generation == s_link_generation)
        atomic_store(&s_capt_restart_required, required);
    pthread_mutex_unlock(&s_link_lock);
}

static int pipe_write(void *ctx, const uint8_t *data, size_t len)
{
    pipe_t *p = ctx;
    int64_t t0 = now_us();
    int r = s_cfg->job_write(s_cfg->ctx, data, len) < 0 ? -1 : 0;
    p->out_us += now_us() - t0;
    p->bytes_out += (uint32_t)len;
    return r;
}

static int pipe_read(void *ctx, uint8_t *data, size_t len, unsigned timeout_ms)
{
    pipe_t *p = ctx;
    int64_t start = now_us();
    int r = s_cfg->job_read ? s_cfg->job_read(s_cfg->ctx, data, len, timeout_ms) : -1;
    p->read_us += now_us() - start;
    return r;
}
static int pipe_flush(void *ctx)
{
    pipe_t *p = ctx;
    int64_t start = now_us();
    int r = s_cfg->job_flush ? s_cfg->job_flush(s_cfg->ctx) : -1;
    p->flush_us += now_us() - start;
    return r;
}

static int pipe_sync(void *ctx, const char *what)
{
    return s_cfg->job_sync ? s_cfg->job_sync(s_cfg->ctx, what) : 0;
}

// Some clients continue sending 600 dpi PWG/URF after the printer starts
// advertising only 300 dpi (cached queue capability or web raster renderer).
// Accept this legacy CAPT resolution without re-advertising it.
static bool accepts_raster_dpi(const printer_driver_t *d, int dpi)
{
    return printer_driver_has_dpi(d, dpi) ||
           (strcmp(d->id, "canon-lbp2900") == 0 && dpi == 600);
}

static int pipe_page_begin(void *user, const raster_page_t *pg)
{
    pipe_t *p = user;
    const printer_driver_t *d = s_cfg->driver;
    if (p->job->cancel)
        return 1;
    if (!accepts_raster_dpi(d, pg->dpi)) {
        LOGW("page %d: unsupported resolution %d dpi", pg->page, pg->dpi);
        p->err = IPP_DOCUMENT_FORMAT_ERROR;
        return 2;
    }
    bool downsample = pg->dpi == 600 && !p->job->web_print && !strcmp(d->id, "canon-lbp2900");
    int dpi = downsample ? 300 : pg->dpi;
    if (!p->drv_job) {
        int paper = printer_driver_paper_for(d, pg->height, pg->dpi);
        p->io = (printer_io_t){.write = pipe_write, .read = pipe_read, .flush = pipe_flush, .sync = pipe_sync,
                               .paper_out = pipe_paper_out, .canceled = pipe_canceled,
                               .restart_required = pipe_restart_required, .ctx = p};
        p->dpi = pg->dpi;
        LOGI("job %d: %s %d dpi, %s", p->job->id, d->papers[paper].name, pg->dpi, pg->bytes_pp == 3 ? "color" : "gray");
        if (downsample)
            LOGI("job %d: reducing external IPP raster from 600 to 300 dpi", p->job->id);
        p->drv_job = d->job_begin(d, &p->io, dpi, paper);
        if (!p->drv_job) {
            p->err = IPP_INTERNAL_ERROR;
            return 3;
        }
    } else if (pg->dpi != p->dpi) {
        LOGW("page %d: resolution changed within the job", pg->page);
        p->err = IPP_DOCUMENT_FORMAT_ERROR;
        return 2;
    }
    p->job->line = 0;
    p->job->height = pg->height;
    int width = pg->width, height = pg->height;
    if (downsample) {
        width = (width + 1) / 2;
        height = (height + 1) / 2;
        uint8_t *row = realloc(p->downsample_row, (size_t)width * pg->bytes_pp);
        if (!row) {
            p->err = IPP_INTERNAL_ERROR;
            return 3;
        }
        p->downsample_row = row;
        p->downsample_width = pg->width;
        p->downsample_height = pg->height;
        p->downsample_bpp = pg->bytes_pp;
    }
    if (d->page_begin(p->drv_job, width, height, pg->bytes_pp)) {
        p->err = IPP_DEVICE_ERROR;
        return 4;
    }
    return 0;
}

static int pipe_line_repeat(void *user, int y, const uint8_t *px, bool is_repeat)
{
    pipe_t *p = user;
    if (p->job->cancel)
        return 1;
#ifdef ESP_PLATFORM
    // Yield based on elapsed CPU time, not a fixed number of lines.
    // Use a per-job timestamp: connections/jobs may run on different tasks.
    int64_t t = now_us();
    if (!p->last_yield_us)
        p->last_yield_us = t;
    if (t - p->last_yield_us >= 50000) {
        vTaskDelay(1);
        p->last_yield_us = now_us();
    }
#endif
    int64_t t0 = now_us();
    const printer_driver_t *d = s_cfg->driver;
    int input_y = y;
    if (p->downsample_row) {
        // Box filter two rows at a time. Repeat the edge pixel for odd sizes.
        int bpp = p->downsample_bpp;
        int width = p->downsample_width;
        for (int x = 0; x < width; x += 2) {
            int right = x + 1 < width ? x + 1 : x;
            for (int channel = 0; channel < bpp; channel++) {
                int average = px ? (px[x * bpp + channel] + px[right * bpp + channel] + 1) / 2 : 255;
                uint8_t *sample = &p->downsample_row[(x / 2) * bpp + channel];
                *sample = y % 2 ? (*sample + average + 1) / 2 : average;
            }
        }
        if (y % 2 == 0 && y + 1 < p->downsample_height) {
            p->render_us += now_us() - t0;
            p->job->line = input_y + 1;
            return 0;
        }
        y /= 2;
        px = p->downsample_row;
        is_repeat = false;
    }
    int result = d->page_line_repeat
        ? d->page_line_repeat(p->drv_job, y, px, is_repeat)
        : d->page_line(p->drv_job, y, px);
    p->render_us += now_us() - t0;
    p->job->line = input_y + 1;
    if (result) {
        p->err = IPP_DEVICE_ERROR;
        return 4;
    }
    return 0;
}

static int pipe_line(void *user, int y, const uint8_t *px)
{
    return pipe_line_repeat(user, y, px, false);
}

static int pipe_page_end(void *user, const raster_page_t *pg)
{
    pipe_t *p = user;
    if (s_cfg->driver->page_end(p->drv_job)) {
        p->err = IPP_DEVICE_ERROR;
        return 4;
    }
    pthread_mutex_lock(&s_lock);
    p->job->impressions++;
    pthread_mutex_unlock(&s_lock);
    LOGI("job %d: page %d done (%u bytes so far)", p->job->id, pg->page, (unsigned)p->bytes_out);
    return 0;
}

// Claim the printer under s_lock before opening the hardware.
static void job_start(job_t *j)
{
    s_active = j;
    j->state = JOB_PROCESSING;
    j->processing = time(NULL);
}

static void close_printer(job_t *j, bool link_ok)
{
    pthread_mutex_lock(&s_lock);
    j->printer_open = false;
    pthread_mutex_unlock(&s_lock);
    if (s_cfg->job_close)
        s_cfg->job_close(s_cfg->ctx, link_ok);
    if (s_cfg->job_state)
        s_cfg->job_state(s_cfg->ctx, false);
}

// Prints the document for a claimed job. Returns an IPP status.
static int print_document(conn_t *c, job_t *j)
{
    bool opened = !j->cancel && (!s_cfg->job_open || s_cfg->job_open(s_cfg->ctx) == 0);
    pthread_mutex_lock(&s_lock);
    if (!opened) {
        bool canceled = j->cancel;
        if (!canceled)
            j->reason = "printer-stopped";
        job_finish(j, canceled ? JOB_CANCELED : JOB_ABORTED);
        pthread_mutex_unlock(&s_lock);
        return canceled ? IPP_OK : IPP_BUSY;
    }
    j->printer_open = true;
    pthread_mutex_unlock(&s_lock);
    if (s_cfg->job_state)
        s_cfg->job_state(s_cfg->ctx, true);
    LOGI("job %d: printing \"%s\" (%s) from %s", j->id, j->name, j->format, j->user);

    pipe_t *p = calloc(1, sizeof(*p));
    if (!p) {
        close_printer(j, false);
        pthread_mutex_lock(&s_lock);
        bool canceled = j->cancel;
        job_finish(j, canceled ? JOB_CANCELED : JOB_ABORTED);
        pthread_mutex_unlock(&s_lock);
        return canceled ? IPP_OK : IPP_INTERNAL_ERROR;
    }
    p->job = j;
    // job_open may wait for USB recovery. Capture the connection it actually
    // acquired, rather than the disconnected generation before that wait.
    pthread_mutex_lock(&s_link_lock);
    p->link_generation = s_link_generation;
    pthread_mutex_unlock(&s_link_lock);
    doc_reader_t rd = {.c = c, .job = j};
    raster_cb_t cb = {.page_begin=pipe_page_begin, .line=pipe_line,
                       .page_end=pipe_page_end, .line_repeat=pipe_line_repeat};
    int64_t t_start = now_us();
    if (c) {
        c->receiving_job = j;
        set_recv_timeout(c, ACTIVE_IPP_SLICE_S);
    }
    int r = j->cancel ? 0 : raster_decode(doc_read, &rd, &cb, p);
    if (c && r == RASTER_ERR_IO && !j->cancel) {
        if (c->chunked && c->body_eof && !c->body_failed)
            LOGW("job %d: IPP upload sent its final HTTP chunk before the raster was complete", j->id);
        LOGW("job %d: raster stream truncated: recv=%lld bytes, pending HTTP body=%lld, "
             "eof=%d, chunked=%d, remaining buffered=%u",
             j->id, (long long)j->bytes, (long long)c->remaining,
             (int)c->body_eof, (int)c->chunked, (unsigned)(c->len - c->pos));
    }
    if (p->drv_job && s_cfg->driver->job_stats) {
        char stats[160];
        s_cfg->driver->job_stats(p->drv_job, stats, sizeof(stats));
        if (stats[0])
            LOGI("job %d: %s", j->id, stats);
    }
    int64_t t_total = now_us() - t_start;
    LOGI("job %d: %.1f s: %.1f s waiting for data (%.0f KB/s), %.1f s printer output, %.1f s rendering, "
         "%.1f s decoding", j->id, t_total / 1e6, rd.wait_us / 1e6,
         rd.wait_us ? j->bytes / 1.024 / (rd.wait_us / 1000.0) : 0.0, p->out_us / 1e6,
         (p->render_us - p->out_us) / 1e6, (t_total - rd.wait_us - p->render_us) / 1e6);

    LOGI("job %d: USB response waits %.3f s, USB flush waits %.3f s (included in pipeline totals)",
         j->id, p->read_us / 1e6, p->flush_us / 1e6);

    // A positive decoder return may also be a callback error code. Printer
    // callbacks set p->err, and the page count must match completed callbacks.
    bool decoded = r > 0 && r == j->impressions && !p->err;
    bool link_ok = true;
    if (p->drv_job) {
        // Finalize or clean up the CAPT session even after cancellation/EOF.
        int64_t t_end = now_us();
        const printer_driver_t *driver = s_cfg->driver;
        if (driver->job_abort && (!decoded || j->cancel))
            link_ok = driver->job_abort(p->drv_job) == 0;
        else {
            int result = driver->job_end(p->drv_job);
            link_ok = result >= 0;
            if (result > 0) {
                decoded = false;
                p->err = IPP_DEVICE_ERROR;
            }
        }
        LOGI("job %d: printer finalization %.3f s", j->id, (now_us() - t_end) / 1e6);
    }
    pipe_paper_out(p, false);
    close_printer(j, link_ok);

    // A counted URF stream can finish decoding before the HTTP body ends.
    // Confirm its transport completed before publishing a successful job.
    if (c) {
        if (decoded && link_ok && !j->cancel) {
            drain_body(c);
            if (!c->body_eof)
                r = RASTER_ERR_IO;
        }
        c->receiving_job = NULL;
        set_recv_timeout(c, IDLE_TIMEOUT_S);
    }
    int status = IPP_OK;
    pthread_mutex_lock(&s_lock);
    if (j->cancel) {
        job_finish(j, JOB_CANCELED);
    } else if (decoded && link_ok && r > 0) {
        job_finish(j, JOB_COMPLETED);
    } else {
        if (p->err == IPP_INTERNAL_ERROR || r == RASTER_ERR_NOMEM) {
            j->reason = "aborted-by-system";
            status = IPP_INTERNAL_ERROR;
        } else if (p->err == IPP_DEVICE_ERROR || (decoded && !link_ok)) {
            j->reason = "printer-stopped";
            status = IPP_DEVICE_ERROR;
        } else {
            j->reason = p->err || r == RASTER_ERR_FORMAT || r == RASTER_ERR_UNSUPPORTED
                ? "document-unprintable-error" : "document-format-error";
            status = !p->err && !p->drv_job && (r == RASTER_ERR_FORMAT || r == RASTER_ERR_UNSUPPORTED)
                ? IPP_DOCUMENT_FORMAT_NOT_SUPPORTED : IPP_DOCUMENT_FORMAT_ERROR;
        }
        job_finish(j, JOB_ABORTED);
    }
    LOGI("job %d: %s, %d page(s), %lld bytes received%s%s", j->id,
         j->state == JOB_COMPLETED ? "completed" : j->state == JOB_CANCELED ? "canceled" : "aborted", j->impressions,
         (long long)j->bytes, r < 0 ? ": " : "", r < 0 ? raster_strerror(r) : "");
    pthread_mutex_unlock(&s_lock);
    free(p->downsample_row);
    free(p);
    return status;
}

// Admission and the ordering decision are made under the same lock.
static void admit_document(const conn_t *c, job_t *j)
{
    j->web_print = !strcmp(c->path, "/ipp/web-print") &&
                   (!s_cfg->web_port || c->listener_port == s_cfg->web_port);
    bool wait = s_active || next_document();
    j->order = s_next_order++;
    j->replying = true;
    if (wait)
        j->receiving = true;
    else
        job_start(j);
}

static int spool_document(conn_t *c, job_t *j, const char **message)
{
    int status = IPP_OK;
    size_t capacity = c->chunked ? SPOOL_JOB_LIMIT : c->remaining > 0 ? (size_t)c->remaining : 0;
    if (!c->chunked && c->remaining > SPOOL_JOB_LIMIT) {
        *message = "A queued document may use at most 2 MiB";
        status = IPP_REQUEST_ENTITY_TOO_LARGE;
    } else if (!capacity) {
        status = IPP_DOCUMENT_FORMAT_ERROR;
    } else {
        pthread_mutex_lock(&s_lock);
        if (s_spool_bytes + capacity <= SPOOL_TOTAL_LIMIT) {
#ifdef ESP_PLATFORM
            if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >= capacity + SPOOL_PSRAM_RESERVE)
                j->spool = heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
            j->spool = malloc(capacity);
#endif
            if (j->spool) {
                j->spool_capacity = capacity;
                s_spool_bytes += capacity;
            }
        }
        pthread_mutex_unlock(&s_lock);
        if (!j->spool) {
            *message = "Queue memory is full. Try again when an earlier job finishes";
            status = IPP_BUSY;
        }
    }

    size_t received = 0;
    if (status == IPP_OK) {
        c->receiving_job = j;
        c->upload_deadline = now_us() + (int64_t)SPOOL_UPLOAD_TIMEOUT_S * 1000000;
        set_recv_timeout(c, ACTIVE_IPP_SLICE_S);
        while (!c->body_eof && !j->cancel && now_us() < c->upload_deadline) {
            uint8_t extra;
            int n = received == capacity ? body_read(c, &extra, 1) :
                body_read(c, j->spool + received, capacity - received);
            if (n <= 0)
                break;
            if (received == capacity) {
                *message = "A queued document may use at most 2 MiB";
                status = IPP_REQUEST_ENTITY_TOO_LARGE;
                break;
            }
            received += n;
        }
        if (status == IPP_OK && (!received || !c->body_eof))
            status = IPP_DOCUMENT_FORMAT_ERROR;
        if (!c->body_eof && (j->cancel || now_us() >= c->upload_deadline))
            c->body_failed = true;
        c->receiving_job = NULL;
        c->upload_deadline = 0;
        set_recv_timeout(c, IDLE_TIMEOUT_S);
    }
    pthread_mutex_lock(&s_lock);
    if (j->cancel) {
        status = IPP_JOB_CANCELED;
        job_finish(j, JOB_CANCELED);
    } else if (status != IPP_OK) {
        job_finish(j, JOB_ABORTED);
    } else {
        j->spool_size = received;
        j->receiving = false;
    }
    pthread_mutex_unlock(&s_lock);
    return status;
}

// Reuse an existing request thread after sending its response. If the next
// upload is still in flight, its own request will resume dispatch on exit.
static void print_waiting_documents(void)
{
    for (;;) {
        pthread_mutex_lock(&s_lock);
        job_t *j = s_active ? NULL : next_document();
        if (!j || j->receiving || !j->spool_size) {
            pthread_mutex_unlock(&s_lock);
            return;
        }
        job_start(j);
        pthread_mutex_unlock(&s_lock);
        print_document(NULL, j);
    }
}

static int submit_document(conn_t *c, job_t *j, ipp_out_t *body, const char **message)
{
    int status = j->receiving ? spool_document(c, j, message) : print_document(c, j);
    pthread_mutex_lock(&s_lock);
    ipp_out_group(body, IPP_TAG_JOB);
    add_job_attrs(body, c, j, NULL);
    j->replying = false;
    pthread_mutex_unlock(&s_lock);
    return status;
}

// s_lock held. An upload retains ownership of its spool until it exits.
static void cancel_job(job_t *j)
{
    j->cancel = true;
    if (j->state == JOB_PENDING && !j->receiving)
        job_finish(j, JOB_CANCELED);
    else if (j->printer_open && s_cfg->job_cancel)
        s_cfg->job_cancel(s_cfg->ctx);
    LOGI("job %d: cancel requested", j->id);
}

// ===========================================================================
// IPP operations

static bool format_supported(const char *fmt)
{
    return !fmt[0] || !strcmp(fmt, "application/octet-stream") || !strcmp(fmt, "image/urf") ||
           !strcmp(fmt, "image/pwg-raster");
}

// Checks shared by Print-Job / Validate-Job / Create-Job. Returns an IPP status.
static int check_job_request(const ipp_request_t *req, ipp_out_t *unsup)
{
    char fmt[64];
    ipp_attr_str(ipp_find(req, IPP_TAG_OPERATION, "document-format"), fmt, sizeof(fmt));
    if (!format_supported(fmt)) {
        ipp_out_str(unsup, IPP_TAG_MIMETYPE, "document-format", fmt);
        return IPP_DOCUMENT_FORMAT_NOT_SUPPORTED;
    }
    char comp[16];
    ipp_attr_str(ipp_find(req, IPP_TAG_OPERATION, "compression"), comp, sizeof(comp));
    if (comp[0] && strcmp(comp, "none") != 0) {
        ipp_out_str(unsup, IPP_TAG_KEYWORD, "compression", comp);
        return IPP_COMPRESSION_NOT_SUPPORTED;
    }
    const ipp_attr_t *res = ipp_find(req, IPP_TAG_JOB, "printer-resolution");
    if (res) {
        if (res->tag != IPP_TAG_RESOLUTION || res->value_len != 9 || res->value[8] != 3)
            return IPP_ATTRIBUTES_NOT_SUPPORTED;
        const uint8_t *v = res->value;
        uint32_t x = (uint32_t)v[0] << 24 | (uint32_t)v[1] << 16 | (uint32_t)v[2] << 8 | v[3];
        uint32_t y = (uint32_t)v[4] << 24 | (uint32_t)v[5] << 16 | (uint32_t)v[6] << 8 | v[7];
        if (x != y || x > 10000 || !accepts_raster_dpi(s_cfg->driver, (int)x)) {
            ipp_out_res(unsup, "printer-resolution", (int)x, (int)y);
            return IPP_ATTRIBUTES_NOT_SUPPORTED;
        }
    }
    return IPP_OK;
}

static void handle_ipp(conn_t *c)
{
    ipp_request_t *req = AP_MALLOC_BULK(sizeof(*req));
    if (req) memset(req, 0, sizeof(*req));
    ipp_out_t out, unsup;
    ipp_out_init(&out);
    ipp_out_init(&unsup);
    if (!req) {
        c->keep_alive = false;
        send_http(c, "500 Internal Server Error", "text/plain", "", 0);
        return;
    }
    int status = ipp_request_read(req, body_read_exact, c);
    uint8_t major = req->major >= 1 && req->major <= 2 ? req->major : 1;
    uint8_t minor = req->major == 2 ? 0 : 1;
    if (status == IPP_BAD_REQUEST || status == IPP_INTERNAL_ERROR) {
        c->keep_alive = false;
        ipp_request_free(req);
        free(req);
        send_http(c, "400 Bad Request", "text/plain", "", 0);
        return;
    }

    // RFC 8011 4.1.x: request-id != 0, charset and language first, printer-uri present
    if (status == 0) {
        const ipp_attr_t *a0 = req->nattrs > 0 ? &req->attrs[0] : NULL;
        const ipp_attr_t *a1 = req->nattrs > 1 ? &req->attrs[1] : NULL;
        if (req->request_id == 0)
            status = IPP_BAD_REQUEST;
        else if (!a0 || a0->group != IPP_TAG_OPERATION || a0->tag != IPP_TAG_CHARSET ||
                 a0->name_len != 18 || memcmp(a0->name, "attributes-charset", 18) != 0)
            status = IPP_BAD_REQUEST;
        else if (!a1 || a1->group != IPP_TAG_OPERATION || a1->tag != IPP_TAG_LANGUAGE ||
                 a1->name_len != 27 || memcmp(a1->name, "attributes-natural-language", 27) != 0)
            status = IPP_BAD_REQUEST;
        else if (!ipp_find(req, IPP_TAG_OPERATION, "printer-uri") && !ipp_find(req, IPP_TAG_OPERATION, "job-uri"))
            status = IPP_BAD_REQUEST;
    }

    char fmt[64];
    ipp_attr_str(ipp_find(req, IPP_TAG_OPERATION, "document-format"), fmt, sizeof(fmt));
    ipp_out_t body;     // operation results (job / printer groups)
    ipp_out_init(&body);
    const char *message = NULL;
    airprint_status_t st;

    if (status == 0) {
        filter_t f;
        filter_init(&f, req);
        int job_id = ipp_attr_int(ipp_find(req, IPP_TAG_OPERATION, "job-id"), 0);
        if (!job_id) {      // job-uri form: ipp://host/ipp/print/<id>
            char juri[160];
            ipp_attr_str(ipp_find(req, IPP_TAG_OPERATION, "job-uri"), juri, sizeof(juri));
            const char *slash = strrchr(juri, '/');
            if (slash)
                job_id = atoi(slash + 1);
        }
        job_t *j = NULL;

        switch (req->op) {
        case IPP_OP_GET_PRINTER_ATTRIBUTES:
            pthread_mutex_lock(&s_lock);
            ipp_out_group(&body, IPP_TAG_PRINTER);
            add_printer_attrs(&body, c, &f);
            pthread_mutex_unlock(&s_lock);
            break;

        case IPP_OP_VALIDATE_JOB:
            status = check_job_request(req, &unsup);
            break;

        case IPP_OP_PRINT_JOB:
        case IPP_OP_CREATE_JOB:
            status = check_job_request(req, &unsup);
            if (status)
                break;
            get_status(&st);
            if (!st.connected) {
                status = IPP_NOT_ACCEPTING_JOBS;
                message = st.message[0] ? st.message : "Printer not connected";
                break;
            }
            pthread_mutex_lock(&s_lock);
            expire_pending();
            if ((!s_active && st.busy) || outstanding_jobs() >= job_limit() ||
                !(j = job_create(req))) {
                pthread_mutex_unlock(&s_lock);
                status = IPP_BUSY;
                message = queue_enabled() ? "Printer or print queue is full" : "Printer is busy";
                break;
            }
            snprintf(j->format, sizeof(j->format), "%s", fmt[0] ? fmt : "application/octet-stream");
            if (req->op == IPP_OP_PRINT_JOB) {
                admit_document(c, j);
                pthread_mutex_unlock(&s_lock);
                status = submit_document(c, j, &body, &message);
            } else {
                ipp_out_group(&body, IPP_TAG_JOB);
                add_job_attrs(&body, c, j, NULL);
                pthread_mutex_unlock(&s_lock);
            }
            break;

        case IPP_OP_SEND_DOCUMENT:
            pthread_mutex_lock(&s_lock);
            expire_pending();
            j = job_find(job_id);
            if (!ipp_find(req, IPP_TAG_OPERATION, "last-document")) {
                status = IPP_BAD_REQUEST;
                message = "Missing last-document";
            } else if (!j) {
                status = IPP_NOT_FOUND;
            } else if (j->state != JOB_PENDING || j->order) {
                status = IPP_NOT_POSSIBLE;
                message = "Job does not accept documents";
            } else if (!format_supported(fmt)) {
                status = IPP_DOCUMENT_FORMAT_NOT_SUPPORTED;
            } else {
                if (fmt[0])
                    snprintf(j->format, sizeof(j->format), "%s", fmt);
                admit_document(c, j);
            }
            if (status && j) {
                ipp_out_group(&body, IPP_TAG_JOB);
                add_job_attrs(&body, c, j, NULL);
            }
            pthread_mutex_unlock(&s_lock);
            if (status == IPP_OK)
                status = submit_document(c, j, &body, &message);
            break;

        case IPP_OP_CLOSE_JOB:
            pthread_mutex_lock(&s_lock);
            j = job_find(job_id);
            if (!j)
                status = IPP_NOT_FOUND;
            else if (j->state == JOB_PENDING && !j->order)
                job_finish(j, JOB_COMPLETED);
            pthread_mutex_unlock(&s_lock);
            break;

        case IPP_OP_CANCEL_JOB:
            pthread_mutex_lock(&s_lock);
            j = job_find(job_id);
            if (!j)
                status = IPP_NOT_FOUND;
            else if (job_done(j))
                status = IPP_NOT_POSSIBLE;
            else
                cancel_job(j);
            pthread_mutex_unlock(&s_lock);
            break;

        case IPP_OP_CANCEL_MY_JOBS: {
            char user[sizeof(s_jobs[0].user)];
            ipp_attr_str(ipp_find(req, IPP_TAG_OPERATION, "requesting-user-name"), user, sizeof(user));
            pthread_mutex_lock(&s_lock);
            for (int i = 0; user[0] && i < MAX_JOBS; i++) {
                j = &s_jobs[i];
                if (j->id && !job_done(j) && !strcmp(j->user, user))
                    cancel_job(j);
            }
            pthread_mutex_unlock(&s_lock);
            break;
        }

        case IPP_OP_GET_JOB_ATTRIBUTES:
            pthread_mutex_lock(&s_lock);
            j = job_find(job_id);
            if (!j) {
                status = IPP_NOT_FOUND;
            } else {
                ipp_out_group(&body, IPP_TAG_JOB);
                add_job_attrs(&body, c, j, &f);
            }
            pthread_mutex_unlock(&s_lock);
            break;

        case IPP_OP_GET_JOBS: {
            char which[24];
            ipp_attr_str(ipp_find(req, IPP_TAG_OPERATION, "which-jobs"), which, sizeof(which));
            bool completed = !strcmp(which, "completed"), all = !strcmp(which, "all");
            bool minimal = f.idx < 0;   // RFC 8011: default is job-id + job-uri only
            pthread_mutex_lock(&s_lock);
            for (job_t *jj = job_before(s_next_id, false); jj; jj = job_before(jj->id, false)) {
                if (!all && job_done(jj) != completed)
                    continue;
                ipp_out_group(&body, IPP_TAG_JOB);
                if (minimal) {
                    char juri[160], path[48];
                    ipp_out_int(&body, IPP_TAG_INTEGER, "job-id", jj->id);
                    snprintf(path, sizeof(path), "/ipp/print/%d", jj->id);
                    uri_for(c, juri, sizeof(juri), "ipp", path);
                    ipp_out_str(&body, IPP_TAG_URI, "job-uri", juri);
                } else {
                    add_job_attrs(&body, c, jj, &f);
                }
            }
            pthread_mutex_unlock(&s_lock);
            break;
        }

        case IPP_OP_IDENTIFY_PRINTER:
            if (s_cfg->identify)
                s_cfg->identify(s_cfg->ctx);
            break;

        default:
            status = IPP_OPERATION_NOT_SUPPORTED;
            break;
        }
    }

    // response: operation group (+ unsupported group) + results
    ipp_out_header(&out, major, minor, (uint16_t)status, req->request_id);
    ipp_out_group(&out, IPP_TAG_OPERATION);
    ipp_out_str(&out, IPP_TAG_CHARSET, "attributes-charset", "utf-8");
    ipp_out_str(&out, IPP_TAG_LANGUAGE, "attributes-natural-language", "en");
    if (message)
        ipp_out_str(&out, IPP_TAG_TEXT, "status-message", message);
    if (unsup.len) {
        ipp_out_group(&out, IPP_TAG_UNSUPPORTED_GROUP);
        ipp_out_append(&out, &unsup);
    }
    ipp_out_append(&out, &body);
    ipp_out_end(&out);

    c->dispatch_queue = queue_enabled() &&
        (req->op == IPP_OP_PRINT_JOB || req->op == IPP_OP_SEND_DOCUMENT ||
         req->op == IPP_OP_CANCEL_JOB || req->op == IPP_OP_CANCEL_MY_JOBS);
    if (c->dispatch_queue)
        c->keep_alive = false;

    // document data the operation did not consume (e.g. a rejected Print-Job)
    drain_body(c);
    if (out.oom || body.oom)
        send_http(c, "500 Internal Server Error", "text/plain", "", 0);
    else
        send_http(c, "200 OK", "application/ipp", out.buf, out.len);
    if (req->op != IPP_OP_GET_PRINTER_ATTRIBUTES && req->op != IPP_OP_GET_JOB_ATTRIBUTES)
        LOGI("IPP op 0x%04x -> status 0x%04x", req->op, status);
    ipp_out_free(&out);
    ipp_out_free(&unsup);
    ipp_out_free(&body);
    ipp_request_free(req);
    free(req);
}

// ===========================================================================
// live state for the web UI (events.h)

static const char *job_state_name(int state)
{
    switch (state) {
    case JOB_PENDING: return "pending";
    case JOB_HELD: return "held";
    case JOB_PROCESSING: return "processing";
    case JOB_CANCELED: return "canceled";
    case JOB_ABORTED: return "aborted";
    case JOB_COMPLETED: return "completed";
    default: return "unknown";
    }
}

#define STATUS_REFRESH_S 2      // the printer status costs a USB control transfer

static void get_status_cached(airprint_status_t *st)
{
    static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;
    static airprint_status_t cached;
    static time_t cached_at;
    static unsigned cached_generation;
    time_t now = time(NULL);
    pthread_mutex_lock(&cache_lock);
    pthread_mutex_lock(&s_link_lock);
    unsigned generation = s_link_generation;
    pthread_mutex_unlock(&s_link_lock);
    if (!cached_at || cached_generation != generation || now - cached_at >= STATUS_REFRESH_S || now < cached_at) {
        cached_generation = generation;
        get_status(&cached);
        cached_at = now;
    }
    *st = cached;
    pthread_mutex_unlock(&cache_lock);
}

int airprint_get_activity(airprint_activity_t *a)
{
    memset(a, 0, sizeof(*a));
    if (!s_cfg)
        return -1;
    get_status_cached(&a->status);
    pthread_mutex_lock(&s_lock);
    expire_pending();
    a->state = printer_state(&a->status);
    const job_t *j = s_active;
    if (!j && s_next_id > 1)
        j = job_find(s_next_id - 1);
    if (j) {
        a->job.id = j->id;
        snprintf(a->job.name, sizeof(a->job.name), "%s", j->name[0] ? j->name : "Untitled");
        a->job.state = job_state_name(j->state);
        a->job.pages = j->impressions;
        int line = j->line, height = j->height;
        a->job.percent = j->state == JOB_PROCESSING && height > 0 ? (int)(100LL * line / height) : -1;
        time_t now = time(NULL);
        a->job.ended_ago = !job_done(j) ? -1 : now >= j->completed ? (long)(now - j->completed) : 0;
    }
    pthread_mutex_unlock(&s_lock);
    return 0;
}

int airprint_status_json(void *ctx, char *buf, size_t n)
{
    airprint_status_t st;
    get_status_cached(&st);
    size_t p = 0;
#define PUT(...) (p += (size_t)snprintf(buf + p, p < n ? n - p : 0, __VA_ARGS__))
#define STR(s) (p = events_json_str(buf, p, n, (s)))
    PUT("{\"name\":");
    STR(s_cfg->name);
    PUT(",\"model\":");
    STR(s_cfg->make_model);
    PUT(",\"papers\":[");
    const printer_driver_t *driver = s_cfg->driver;
    for (int i = 0; i < driver->npapers; i++) {
        if (i) PUT(",");
        STR(driver->papers[i].pwg);
    }
    PUT("],\"resolutions\":[");
    for (int i = 0; i < driver->nresolutions; i++) {
        if (i) PUT(",");
        PUT("%d", driver->resolutions[i]);
    }
    PUT("]");
    pthread_mutex_lock(&s_lock);
    expire_pending();
    int state = printer_state(&st);
    PUT(",\"connected\":%s,\"state\":\"%s\",\"message\":", st.connected ? "true" : "false",
        state == 5 ? "stopped" : state == 4 ? "processing" : "idle");
    STR(st.message[0] ? st.message : st.connected ? "Ready" : "Printer not connected");
    PUT(",\"reasons\":[");
    for (int i = 0; i < st.nreasons; i++) {
        PUT(i ? "," : "");
        STR(st.reasons[i]);
    }
    PUT("],\"supplies\":[");
    for (int i = 0; i < st.nsupplies; i++) {
        PUT("%s{\"name\":", i ? "," : "");
        STR(st.supply[i].name);
        PUT(",\"color\":");
        STR(st.supply[i].color);
        PUT(",\"level\":%d}", st.supply[i].level);
    }
    PUT("],\"active\":%d,\"jobCount\":%d,\"jobLimit\":%d,\"jobs\":[",
        s_active ? s_active->id : 0, outstanding_jobs(), job_limit());
    int listed = 0;
    for (job_t *j = job_before(s_next_id, false); j && p < n - 300; j = job_before(j->id, listed >= 6)) {
        if (listed >= 6 && job_done(j))
            continue;
        PUT("%s{\"id\":%d,\"name\":", listed++ ? "," : "", j->id);
        STR(j->name[0] ? j->name : "Untitled");
        PUT(",\"user\":");
        STR(j->user);
        PUT(",\"queuePosition\":%d,\"receiving\":%s", queue_position(j), j->receiving ? "true" : "false");
        // a time, not an age: the snapshot only changes when something happens
        PUT(",\"state\":\"%s\",\"reason\":\"%s\",\"pages\":%d,\"bytes\":%lld,\"time\":%lld}",
            job_state_name(j->state), job_reason(j), j->impressions, (long long)j->bytes,
            (long long)(j->completed ? j->completed : j->created));
    }
    pthread_mutex_unlock(&s_lock);
    PUT("]}");
#undef PUT
#undef STR
    return p < n ? (int)p : -1;
}

// ===========================================================================
// web page

static void handle_get(conn_t *c)
{
    if (!strcmp(c->path, "/icon.png")) {
        send_http(c, "200 OK", "image/png", ICON_PNG, sizeof(ICON_PNG));
        return;
    }
    if (strcmp(c->path, "/") != 0) {
        send_http(c, "404 Not Found", "text/plain", "not found", 9);
        return;
    }
    airprint_status_t st;
    get_status(&st);
    char *html = malloc(4096);
    if (!html) {
        send_http(c, "500 Internal Server Error", "text/plain", "", 0);
        return;
    }
    int n = snprintf(html, 4096,
                     "<!doctype html><html><head><meta charset=utf-8><meta name=viewport "
                     "content='width=device-width'><title>%s</title><style>body{font-family:sans-serif;"
                     "max-width:40em;margin:2em auto;padding:0 1em}td{padding:.2em 1em .2em 0}</style></head>"
                     "<body><h1>%s</h1><p>%s &middot; %s</p>%s<table>",
                     s_cfg->name, s_cfg->name, s_cfg->make_model,
                     st.connected ? (st.message[0] ? st.message : "Ready") : "Printer not connected",
                     s_cfg->home_html ? s_cfg->home_html : "");
    for (int i = 0; i < st.nsupplies && n < 3500; i++)
        n += snprintf(html + n, 4096 - n, "<tr><td>%s ink</td><td>%s%d%%</td></tr>", st.supply[i].name,
                      st.supply[i].level < 0 ? "unknown " : "", st.supply[i].level < 0 ? 0 : st.supply[i].level);
    n += snprintf(html + n, 4096 - n, "</table><h2>Recent jobs</h2><table>");
    pthread_mutex_lock(&s_lock);
    for (int id = s_next_id - 1; id > 0 && id >= s_next_id - MAX_JOBS && n < 3800; id--) {
        job_t *j = job_find(id);
        if (j)
            n += snprintf(html + n, 4096 - n, "<tr><td>#%d</td><td>%.40s</td><td>%s</td><td>%d page(s)</td></tr>",
                          j->id, j->name[0] ? j->name : "Untitled", job_reason(j), j->impressions);
    }
    pthread_mutex_unlock(&s_lock);
    n += snprintf(html + n, 4096 - n, "</table></body></html>");
    send_http(c, "200 OK", "text/html; charset=utf-8", html, n);
    free(html);
}

// ===========================================================================
// connections

static void *conn_thread(void *arg)
{
    conn_t *c = arg;
    set_recv_timeout(c, IDLE_TIMEOUT_S);
    int one = 1;
    setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
    setsockopt(c->fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif

    for (;;) {
        char line[512];
        if (read_line(c, line, sizeof(line)) <= 0)
            break;
        char version[16] = "";
        if (sscanf(line, "%7s %127s %15s", c->method, c->path, version) < 2)
            break;
        c->http10 = strcmp(version, "HTTP/1.0") == 0;
        c->keep_alive = !c->http10;
        c->chunked = false;
        c->remaining = 0;
        c->body_eof = false;
        c->body_failed = false;
        c->in_chunks = false;
        c->host[0] = 0;
        c->if_none_match[0] = 0;
        bool expect_continue = false, has_length = false;
        int hl;
        while ((hl = read_line(c, line, sizeof(line))) > 0) {
            char *v = strchr(line, ':');
            if (!v)
                continue;
            *v++ = 0;
            while (*v == ' ')
                v++;
            if (!strcasecmp(line, "Content-Length")) {
                c->remaining = strtoll(v, NULL, 10);
                has_length = true;
            } else if (!strcasecmp(line, "Transfer-Encoding") && strcasestr(v, "chunked")) {
                c->chunked = true;
            } else if (!strcasecmp(line, "Expect") && strcasestr(v, "100-continue")) {
                expect_continue = true;
            } else if (!strcasecmp(line, "Connection")) {
                if (strcasestr(v, "close"))
                    c->keep_alive = false;
                else if (strcasestr(v, "keep-alive"))
                    c->keep_alive = true;
            } else if (!strcasecmp(line, "Host")) {
                snprintf(c->host, sizeof(c->host), "%s", v);
            } else if (!strcasecmp(line, "If-None-Match")) {
                snprintf(c->if_none_match, sizeof(c->if_none_match), "%s", v);
            }
        }
        if (hl < 0)
            break;
#ifdef ESP_PLATFORM
        // A long-lived idle HTTP/1.1 keep-alive occupies an entire FreeRTOS
        // pthread stack. Close after each request on the microcontroller;
        // IPP clients may open a fresh connection for their next request.
        c->keep_alive = false;
#endif
        if (!c->chunked && !has_length)
            c->body_eof = true;
        if (expect_continue && !c->body_eof)
            send_all(c->fd, "HTTP/1.1 100 Continue\r\n\r\n", 25);

        bool ipp_path = !strcmp(c->path, "/") || !strncmp(c->path, "/ipp", 4);
        if (!strcmp(c->method, "POST") && (ipp_path || !s_cfg->http_handler)) {
            handle_ipp(c);
        } else if (!strcmp(c->method, "POST") || !strcmp(c->method, "DELETE")) {
            // the handler may read the body (airprint_http_read_body); the rest is dropped after
            if (!s_cfg->http_handler || !s_cfg->http_handler(s_cfg->ctx, c, c->method, c->path)) {
                drain_body(c);
                send_http(c, "404 Not Found", "text/plain", "not found", 9);
            }
            drain_body(c);
        } else if (!strcmp(c->method, "GET") || !strcmp(c->method, "HEAD")) {
            drain_body(c);
            if (!s_cfg->http_handler || !s_cfg->http_handler(s_cfg->ctx, c, c->method, c->path))
                handle_get(c);
        } else {
            drain_body(c);
            c->keep_alive = false;
            send_http(c, "501 Not Implemented", "text/plain", "", 0);
        }
        if (!c->keep_alive)
            break;
    }
    if (!c->detached)
        close(c->fd);
    bool dispatch = c->dispatch_queue;
    free(c);
    if (dispatch)
        print_waiting_documents();
    pthread_mutex_lock(&s_lock);
    s_connections--;
    pthread_mutex_unlock(&s_lock);
    return NULL;
}

static void *listen_thread(void *arg)
{
    int port = (int)(intptr_t)arg;
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(port), .sin_addr.s_addr = INADDR_ANY};
    if (ls < 0 || bind(ls, (struct sockaddr *)&addr, sizeof(addr)) || listen(ls, 4)) {
        LOGW("cannot listen on port %d (errno %d)", port, errno);
        return NULL;
    }
    LOGI("%s server listening on port %d", port == s_cfg->port ? "IPP" : "web", port);
    for (;;) {
        int fd = accept(ls, NULL, NULL);
        if (fd < 0)
            continue;
        // Both listeners share the worker budget. Keep the heap check and
        // worker allocation together so they cannot admit against the same RAM.
        pthread_mutex_lock(&s_lock);
#ifdef ESP_PLATFORM
        // A FreeRTOS pthread stack needs contiguous internal RAM. Refuse
        // optional control connections before they starve the active print
        // pipeline, Wi-Fi, or USB tasks.
        size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        bool heap_low = internal_free < (36 * 1024) || internal_largest < (14 * 1024);
#else
        bool heap_low = false;
#endif
        bool at_limit = s_connections >= MAX_CONNECTIONS;
        if (heap_low || at_limit) {
            int active = s_connections;
#ifdef ESP_PLATFORM
            bool report = (++s_refused_connections % 32) == 1;
#endif
            pthread_mutex_unlock(&s_lock);
#ifdef ESP_PLATFORM
            if (report)
                LOGW("HTTP connection refused (%s; port=%u active=%d max=%d; internal=%u largest=%u)",
                     heap_low ? "low internal RAM" : "worker limit", (unsigned)port,
                     active, MAX_CONNECTIONS, (unsigned)internal_free, (unsigned)internal_largest);
#else
            LOGW("too many connections (%d), refusing one", active);
#endif
            close(fd);
            continue;
        }
        conn_t *c = calloc(1, sizeof(*c));
        if (!c) {
            pthread_mutex_unlock(&s_lock);
            LOGW("HTTP connection allocation failed on port %u", (unsigned)port);
            close(fd);
            continue;
        }
        c->fd = fd;
        c->listener_port = port;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, CONN_STACK_SIZE);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_t th;
        int created = pthread_create(&th, &attr, conn_thread, c);
        if (!created)
            s_connections++;
        pthread_mutex_unlock(&s_lock);
        pthread_attr_destroy(&attr);
        if (created) {
            LOGW("HTTP worker creation failed on port %u: %d", (unsigned)port, created);
            close(fd);
            free(c);
        }
    }
    return NULL;
}

int airprint_start(const airprint_config_t *cfg)
{
    s_cfg = cfg;
    s_start = time(NULL);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, LISTEN_STACK_SIZE);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t th;
    int r = pthread_create(&th, &attr, listen_thread, (void *)(intptr_t)cfg->port);
    if (r == 0 && cfg->web_port)
        r = pthread_create(&th, &attr, listen_thread, (void *)(intptr_t)cfg->web_port);
    pthread_attr_destroy(&attr);
    return r;
}
