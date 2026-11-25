# Separate CAPT family prototypes

These profiles are separate from stable LBP2900 and HP P1006. No release or
installer catalog change is included. No physical printer or ESP32 is attached.
A successful protocol simulation does **not** establish hardware compatibility.

| Build profile | USB VID:PID | Model aliases | Protocol basis |
|---|---|---|---|
| lbp6000 | 04a9:271a | LBP6000 / LBP6018 | Upstream LBP6000 prologue, extended status |
| lbp6200 | 04a9:271b | LBP6200 / LBP6200d | Assumes the LBP6000 sequence; unconfirmed |
| lbp3010 | 04a9:26da | LBP3010 / LBP3018 / LBP3050 | Upstream LBP3010 prologue, extended status |
| lbp3010 | 04a9:26db | LBP3100 / LBP3108 / LBP3150 | Upstream experimental LBP3100 mapping |

## Sources examined

* https://github.com/mounaiban/captdriver at
  `62719249ac34633338be54bc74beddd0e7003d38`: family command flow and status
  fields, GPL-3.0-or-later. The upstream registration marks 3010 as WORKS,
  3100 and 6000 as EXPERIMENTAL. There is no LBP6200 implementation.
* https://github.com/muxoc/captdriver-lbp6000 at
  `ca52686e380be289a5339c1e5ef30e52fc82a225`: same LBP6000 flow; its README
  reports testing a LBP6000B on Linux/Raspberry Pi, not on ESP32.
* https://github.com/Magister/cndrvcups-capt at
  `ec32bf85d47ba1b800a45d5436d04903ecd8732f`,
  `ppd/CNCUPSLBP6200CAPTK.ppd`: LBP6200, 600 dpi, duplex, pstocapt3 and
  libcanonc3pl.so. PPD settings are evidence about options, not the USB command
  sequence. Linux binary filters/libraries cannot be copied onto ESP32.
* https://github.com/OpenPrinting/cups/issues/80: model USB ID list.

## Port boundaries

The new `components/capt_family` is a private, namespaced copy. Existing
`components/capt`, `canon_lbp2900.c`, `hp_p1006.c`, RAW bridge and releases are
unchanged. The registry enables these drivers only in their dedicated builds;
no IEEE1284 string fallback or generic PCL/PS match is allowed there.

The 6000 sequence includes E0BA payload 01 00; 3010 does not. These profiles
poll A0A8 extended status, including during band transfer. Replies are bounded,
short extended statuses are rejected, reservation reply capacity is initialized,
release acknowledgements are checked, and waits obey cancellation/deadlines.
The 64 KiB upstream stack status buffer is replaced with 256 bytes. Page counters
must match the submitted page, so zero/stale counters cannot acknowledge it.
The existing fork's Hi-SCoA compressor is copied privately; its final band marker
is preserved and tested with the independent upstream decoder.

Initial raster options: A4/Letter/A5, gray/RGB input, 300 or 600 dpi input scaled
to 600 dpi output, simplex only. Margins are provisional 5 mm. No A6 or duplex
is advertised, and no model-specific recovery/replay has been validated.
Paper-out fails the current job; refill and resubmit. Do not infer the native
Canon driver's duplex capability is implemented here.

## Build and verify

```
python tools/test_capt_family.py
pio run -e lbp6000
pio run -e lbp6200
pio run -e lbp3010
```

The native test compiles the actual adapter, transport and command implementation
with ASan/UBSan. It uses a virtual clock and fragmented fake USB replies. It
checks independent Hi-SCoA decoding against source pixels, multi-page/repeated
jobs, model isolation and error/cancel/release/deadline paths. Its fake printer
models the upstream sequence, so it cannot prove the sequence works on 6200.

## Flash on Windows

Unzip the matching USB package. In its model folder, run:

```
py -m pip install esptool==4.9.0
py tools/flash.py --port COM15 --build-dir firmware
```

Replace COM15 with the board's actual COM port. Keep the existing stable board
and use a separate ESP32-S3 N16R8 for this prototype where possible. Initial Wi-Fi
setup uses the existing browser/serial setup. Flashing has not been performed by
the development environment.

Hardware acceptance: verify descriptors, one-page A4, three consecutive jobs,
ten-page PDF, 300/600 dpi, paper-out, cancel, USB reconnect, power cycle. LBP6200
acceptance additionally needs USB capture comparison with Canon's own driver
before claiming native AirPrint support.
