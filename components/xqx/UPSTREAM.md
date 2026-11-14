# XQX / JBIG provenance

Protocol reference: OpenPrinting/foo2zjs, commit
80499ed5bf6caa2963ad337e37cfda78a80aab1e, foo2xqx.c and xqx.h.
https://github.com/OpenPrinting/foo2zjs

HP raster adapter uses the simplex, monochrome XQX framing and JBIG options from
foo2xqx, by Rick Richardson et al., GPL-2.0-or-later. This combined project is
GPL-3.0-or-later. Vendor jbig.c, jbig.h, jbig_ar.c, jbig_ar.h preserve Markus
Kuhn's notices (JBIG-KIT, GPL-2.0-or-later). Local modification: jbig.c includes
port_alloc.h to track allocations and recover from OOM without resetting ESP.

No proprietary HP firmware is included. User supplies sihpP1006.dl through
http://<ESP-IP>/hp-firmware. HP firmware is separate from the ESP application.
The loader follows hplj1000: USB 03f0:3e17, FWVER in IEEE1284 ID indicates loaded
firmware; otherwise send HPDL to bulk OUT before the first print after attach.
Firmware persistence uses a dedicated data partition, bounds and CRC checks.
