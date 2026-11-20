// SPDX-License-Identifier: GPL-3.0-or-later
// CAPT packet framing over byte-counted binary USB I/O. No CUPS dependency.
#include "capt_transport.h"
#include "capt-command.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif
#include <time.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static _Thread_local capt_transport_t *current;
static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
void capt_bind(capt_transport_t *t) { current = t; }
void capt_deadline(capt_transport_t *t, unsigned ms) { t->deadline_ms = now_ms() + ms; }
_Noreturn void capt_fail(const char *message)
{
    capt_log("ERROR: CAPT cmd=0x%04x tx=%u rx=%u: %s\n",current->command,(unsigned)current->used,(unsigned)current->rx_bytes,message);
    current->error = message;
    longjmp(current->guard, 1);
}
static void check(void)
{
    if (now_ms() >= current->deadline_ms) capt_fail("CAPT operation timed out");
#ifdef ESP_PLATFORM
    // Poll loops must let USB, Wi-Fi, cancel timer and watchdog tasks run.
    uint64_t now = now_ms();
    if (now - current->last_yield_ms >= 20) {
        vTaskDelay(1);
        current->last_yield_ms = now_ms();
    }
#endif
}
static unsigned remaining(void)
{
    check();
    uint64_t n = current->deadline_ms - now_ms();
    return n > 15000 ? 15000 : (unsigned)n;
}
static void read_exact(uint8_t *buf, size_t n)
{
    while (n) {
        int got = current->io.read(current->io.ctx, buf, n, remaining());
        if (got <= 0 || (size_t)got > n) capt_fail("CAPT USB read failed");
        current->rx_bytes += got;
        buf += got; n -= got;
    }
}
static void send_buffer(void)
{
    check();
    if (current->io.write(current->io.ctx, current->buffer, current->used) < 0 ||
        current->io.flush(current->io.ctx) < 0) capt_fail("CAPT USB write failed");
}
static void add(uint16_t cmd, const void *buf, size_t n)
{
    if (n > 65531 || current->used + n + 4 > 65535 || (n && !buf))
        capt_fail("CAPT command too large");
    uint8_t *p = current->buffer + current->used;
    p[0] = cmd; p[1] = cmd >> 8; p[2] = n + 4; p[3] = (n + 4) >> 8;
    if (n) memcpy(p + 4, buf, n);
    current->used += n + 4;
}
void capt_send(uint16_t cmd, const void *buf, size_t n)
{
    current->command=cmd; current->rx_bytes=0;
    current->used = 0; add(cmd, buf, n); send_buffer();
}
void capt_sendrecv(uint16_t cmd, const void *buf, size_t n, void *reply, size_t *reply_size)
{
    capt_send(cmd, buf, n);
    uint8_t *p = current->buffer;
    read_exact(p, 6);
    size_t total = p[2] | (size_t)p[3] << 8;
    if ((p[0] | (uint16_t)p[1] << 8) != cmd || total < 6)
        capt_fail("Invalid CAPT reply header");
    if (total > 6) read_exact(p + 6, total - 6);
    size_t payload = total - 4;
    if (reply) {
        if (!reply_size || payload > *reply_size) capt_fail("CAPT reply exceeds destination");
        memcpy(reply, p + 4, payload);
    }
    if (reply_size) *reply_size = payload;
}
void capt_multi_begin(uint16_t cmd)
{
    current->command=cmd; current->rx_bytes=0;
    current->used = 0; add(cmd, NULL, 0);
}
void capt_multi_add(uint16_t cmd, const void *buf, size_t n) { add(cmd, buf, n); }
void capt_multi_send(void)
{
    current->buffer[2] = current->used;
    current->buffer[3] = current->used >> 8;
    send_buffer();
}
const char *capt_identify(void) { return "MFG:Canon;MDL:LBP2900;CMD:CAPT;"; }

void capt_log(const char *fmt, ...)
{
    va_list ap; va_start(ap,fmt);
#ifdef ESP_PLATFORM
    char line[256]; vsnprintf(line,sizeof(line),fmt,ap);
    size_t n=strlen(line); while(n && (line[n-1]=='\n' || line[n-1]=='\r')) line[--n]=0;
    if (!strncmp(line,"ERROR",5)) { ESP_LOGE("capt","%s",line); }
    else if (!strncmp(line,"WARNING",7)) { ESP_LOGW("capt","%s",line); }
    else if (strstr(line, "IC_VIDEO_DATA")) { ESP_LOGD("capt","%s",line); }
    else { ESP_LOGI("capt","%s",line); }
#else
    vfprintf(stderr,fmt,ap);
#endif
    va_end(ap);
}
