# Audio preference and physical recovery indication — 0.9.10

The previous sound toggle existed only in Svelte component memory. The new
AudioAlerts controller persists the requested setting in localStorage. Reload
restores the preference separately from AudioContext playback permission.
Pointer/key interaction resumes the context directly inside the activation
handler when necessary; the page shows a saved-but-not-active hint. Turning
audio off closes the context and late resume callbacks cannot turn it back on.
Leaving the page closes resources without overwriting the user's preference.
Persistence is scoped to the browser and origin, and unavailable storage is
handled without crashing. No ESP speaker/buzzer is configured.

A latched CAPT recovery failure or USB reconnect-required state selects the
on-board RGB LED alarm without an HTTP client. It outranks transient errors,
busy/ready states and Identify-Printer. The button task samples the fault state
using the existing loop; no new polling thread or USB status transfer is added.
The animation task renders five red pulses: 150 ms on, 150 ms off between the
first four pulses, followed by 2000 ms off after the fifth pulse. Repeat until
the fault clears, including successful USB reattach. Local physical test-page
jobs report the same restart-required state. A truncated upload with successful
CAPT cleanup does not automatically mean that a restart is needed.

Uses the existing configurable WS2812 output, default GPIO48, not the power LED.

Validation:
- Native host build: passed (existing vendor warnings).
- LED timing: 20 cycles, exactly five red flashes per cycle and a two-second
  pause; priority and return to ready checked.
- Real CAPT simulator recovery/reconnect test: passed with new LED selection
  assertions through the live protocol fault getter.
- CAPT raster/error suite and IPP integration: passed.
- Web: 21 unit tests passed, including seven audio lifecycle/persistence tests;
  Svelte check has zero errors/warnings and production build passed.
- Installer: 21 passed; six generated package checks await CI firmware assets.
- Local PlatformIO and QEMU self-test attempts are unavailable (pio not installed).
  GitHub CI builds actual ESP32-S3 firmware profiles and runs the new LED test.

No physical LED or speaker playback was observed in this environment, and no
board was flashed. The earlier 512 KiB/1 MiB IPP truncation issue remains open.
