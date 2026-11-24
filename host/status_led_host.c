#include <assert.h>
#include <stdio.h>
#include "../src/status_led_pattern.h"

int main(void)
{
    assert(status_led_select(true, true, true, true) == LED_RECOVERY_REQUIRED);
    assert(status_led_select(true, false, false, false) == LED_RECOVERY_REQUIRED);
    assert(status_led_select(false, true, true, true) == LED_ERROR);
    assert(status_led_select(false, false, true, true) == LED_BUSY);
    assert(status_led_select(false, false, false, true) == LED_READY);
    assert(status_led_select(false, false, false, false) == LED_WAITING);
    assert(status_led_print_only_state(LED_OFF, false) == LED_OFF);
    assert(status_led_print_only_state(LED_WAITING, false) == LED_OFF);
    assert(status_led_print_only_state(LED_READY, false) == LED_OFF);
    assert(status_led_print_only_state(LED_BUSY, false) == LED_BUSY);
    assert(status_led_print_only_state(LED_READY, true) == LED_BUSY);
    assert(status_led_print_only_state(LED_ERROR, false) == LED_ERROR);
    assert(status_led_print_only_state(LED_ERROR, true) == LED_ERROR);
    assert(status_led_print_only_state(LED_RECOVERY_REQUIRED, false) == LED_RECOVERY_REQUIRED);
    assert(status_led_print_only_state(LED_RECOVERY_REQUIRED, true) == LED_RECOVERY_REQUIRED);
    status_led_frame_t off = status_led_frame(LED_OFF, 0);
    assert(off.r == 0 && off.g == 0 && off.b == 0);
    status_led_frame_t blink = status_led_frame(LED_BUSY, 0);
    assert(blink.r == 0 && blink.g == 255 && blink.b == 0 && blink.duration_ms == 250);
    blink = status_led_frame(LED_BUSY, blink.next);
    assert(blink.r == 0 && blink.g == 0 && blink.b == 0 && blink.duration_ms == 250);

    unsigned step = 0;
    for (unsigned cycle = 0; cycle < 20; ++cycle) {
        unsigned flashes = 0, duration = 0;
        bool previous = false;
        do {
            status_led_frame_t f = status_led_frame(LED_RECOVERY_REQUIRED, step);
            bool lit = f.r != 0;
            if (lit && !previous) ++flashes;
            assert(f.g == 0 && f.b == 0);
            if (step == 9) assert(!lit && f.duration_ms == 2000);
            else assert(f.duration_ms == 150);
            previous = lit; duration += f.duration_ms; step = f.next;
        } while (step != 0);
        assert(flashes == 5 && duration == 3350);
    }
    status_led_frame_t ready = status_led_frame(LED_READY, 0);
    assert(ready.g == 255 && ready.r == 0 && ready.b == 0);
    puts("LED: 20 cycles, exactly five red flashes + 2 s pause; recovery priority and return to ready passed");
    return 0;
}
