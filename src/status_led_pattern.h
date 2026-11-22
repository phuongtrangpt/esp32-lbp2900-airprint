// Portable on-board LED states and timing, shared with native regression tests.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LED_OFF,
    LED_WAITING,              // solid yellow: no supported printer attached
    LED_READY,                // solid green
    LED_BUSY,                 // blinking green
    LED_ERROR,                // transient fast red blink
    LED_RECOVERY_REQUIRED,    // five red flashes, two-second pause, repeat
} led_state_t;

typedef struct {
    uint8_t r, g, b;
    uint32_t duration_ms;
    unsigned next;
} status_led_frame_t;

static inline led_state_t status_led_select(bool recovery, bool error, bool busy, bool connected)
{
    return recovery ? LED_RECOVERY_REQUIRED : error ? LED_ERROR : busy ? LED_BUSY :
           connected ? LED_READY : LED_WAITING;
}

static inline status_led_frame_t status_led_frame(led_state_t state, unsigned step)
{
    status_led_frame_t frame = {.duration_ms = 500};
    switch (state) {
    case LED_WAITING: frame.r = 255; frame.g = 180; break;
    case LED_READY: frame.g = 255; break;
    case LED_BUSY:
        frame.g = (step & 1) ? 0 : 255;
        frame.duration_ms = 250; frame.next = (step + 1) % 2; break;
    case LED_ERROR:
        frame.r = (step & 1) ? 0 : 255;
        frame.duration_ms = 100; frame.next = (step + 1) % 2; break;
    case LED_RECOVERY_REQUIRED:
        step %= 10;
        frame.r = (step & 1) ? 0 : 255;
        frame.duration_ms = step == 9 ? 2000 : 150;
        frame.next = (step + 1) % 10; break;
    default: break;
    }
    return frame;
}
