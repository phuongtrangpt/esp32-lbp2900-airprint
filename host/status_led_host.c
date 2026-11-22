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
