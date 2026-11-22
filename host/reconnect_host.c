// Exercise the real status callbacks, including cleanup after USB reattach.
#include <assert.h>
#include "../components/airprint/airprint.c"

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
    airprint_printer_reconnected();
    get_status_cached(&st);
    assert(!st.message[0] && !st.nreasons);
    // Old cleanup must not restore either warning after the same printer returns.
    pipe_restart_required(&old, true);
    pipe_paper_out(&old, true);
    get_status(&st);
    assert(!st.message[0] && !st.nreasons);
    pipe_t fresh = {.link_generation = s_link_generation};
    pipe_restart_required(&fresh, true);
    get_status(&st);
    assert(strstr(st.message, "Máy in bị treo."));
    pipe_restart_required(&fresh, false);
    pipe_paper_out(&fresh, true);
    get_status(&st);
    assert(strstr(st.message, "Hết giấy."));
    airprint_printer_reconnected();
    get_status(&st);
    assert(!st.message[0] && !st.nreasons);
    puts("USB reconnect: clears cached warning, ignores stale cleanup, retains new faults");
    return 0;
}
