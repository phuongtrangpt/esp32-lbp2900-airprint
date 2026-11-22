// Exercise the real status callbacks, including cleanup after USB reattach.
#include <assert.h>
#include "../src/status_led_pattern.h"
#include "../components/airprint/airprint.c"
#include "capt-command.h"
#define CAPT_SIM_NO_RASTER_CHECK
#include "capt_sim.h"

typedef struct {
    fake_t printer;
    pipe_t pipe;
} session_t;
static void restart_bridge(void *ctx, bool required)
{
    session_t *s = ctx;
    pipe_restart_required(&s->pipe, required);
}
static void paper_bridge(void *ctx, bool waiting)
{
    session_t *s = ctx;
    pipe_paper_out(&s->pipe, waiting);
}
static int write_bridge(void *ctx, const uint8_t *data, size_t len)
{ return write_device(&((session_t *)ctx)->printer, data, len); }
static int read_bridge(void *ctx, uint8_t *data, size_t len, unsigned ms)
{ return read_device(&((session_t *)ctx)->printer, data, len, ms); }

static void real_session_recovery(const printer_driver_t *d)
{
    session_t s = {.printer = {.strict_session = 1, .refuse_release = 1},
                   .pipe = {.link_generation = s_link_generation}};
    printer_io_t link = {.ctx = &s, .write = write_bridge, .read = read_bridge,
                        .flush = flush_device, .restart_required = restart_bridge,
                        .paper_out = paper_bridge};
    void *j = d->job_begin(d, &link, 300, 0);
    assert(j && !d->page_begin(j, 2480, 3508, 1));
    assert(d->job_abort(j) < 0); // real CAPT cleanup refuses ReleaseUnit
    airprint_status_t st;
    get_status(&st);
    assert(strstr(st.message, "Máy in bị treo."));
    assert(airprint_printer_restart_required());
    assert(status_led_select(airprint_printer_restart_required(), false, false, true) == LED_RECOVERY_REQUIRED);
    pipe_t old = s.pipe;
    // Simulate the physical power cycle: printer protocol state starts afresh.
    memset(&s.printer, 0, sizeof(s.printer));
    s.printer.strict_session = 1;
    airprint_printer_reconnected();
    s.pipe.link_generation = s_link_generation;
    pipe_restart_required(&old, true);
    for (int n = 0; n < 3; ++n) {
        j = d->job_begin(d, &link, 300, 0);
        assert(j && !d->page_begin(j, 2480, 3508, 1));
        for (int y = 0; y < 3508; ++y) assert(!d->page_line(j, y, NULL));
        assert(!d->page_end(j) && !d->job_end(j));
        get_status(&st);
        assert(!st.message[0] && !st.nreasons && !s.printer.reserved);
    }
    assert(s.printer.released_jobs == 3);
    puts("CAPT reconnect lifecycle: failed release, physical restart, three successful following jobs");
}


static void connected(void *ctx, airprint_status_t *st) { st->connected = true; }
int main(void)
{
    airprint_config_t cfg = {.driver = printer_driver_find("canon-lbp2900"), .get_status = connected};
    assert(cfg.driver);
    s_cfg = &cfg;
    pipe_t old = {.link_generation = s_link_generation};
    airprint_status_t st;
    pipe_restart_required(&old, true);
    get_status_cached(&st);
    assert(strstr(st.message, "Máy in bị treo."));
    assert(airprint_printer_restart_required());
    assert(status_led_select(airprint_printer_restart_required(), false, false, true) == LED_RECOVERY_REQUIRED);
    airprint_printer_reconnected();
    get_status_cached(&st);
    assert(!st.message[0] && !st.nreasons);
    assert(!airprint_printer_restart_required());
    // Old cleanup must not restore either warning after the same printer returns.
    pipe_restart_required(&old, true);
    pipe_paper_out(&old, true);
    get_status(&st);
    assert(!st.message[0] && !st.nreasons);
    assert(!airprint_printer_restart_required());
    pipe_t fresh = {.link_generation = s_link_generation};
    pipe_restart_required(&fresh, true);
    get_status(&st);
    assert(strstr(st.message, "Máy in bị treo."));
    assert(airprint_printer_restart_required());
    assert(status_led_select(airprint_printer_restart_required(), false, false, true) == LED_RECOVERY_REQUIRED);
    pipe_restart_required(&fresh, false);
    pipe_paper_out(&fresh, true);
    get_status(&st);
    assert(strstr(st.message, "Hết giấy."));
    airprint_printer_reconnected();
    get_status(&st);
    assert(!st.message[0] && !st.nreasons);
    assert(!airprint_printer_restart_required());
    real_session_recovery(cfg.driver);
    puts("USB reconnect: clears cached warning, ignores stale cleanup, retains new faults");
    return 0;
}
