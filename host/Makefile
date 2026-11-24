# Native build of the portable printing code for testing on the development machine.
CFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined
# same float evaluation as the firmware build, so the test page CRC matches
CFLAGS += -ffp-contract=off
BJL = ../components/canon_bjl/canon_bjl.c ../components/canon_bjl/testpage.c ../components/canon_bjl/bjl_render.c
AIRPRINT = ../components/airprint/airprint.c ../components/airprint/ipp.c ../components/airprint/raster.c \
           ../components/airprint/events.c ../components/airprint/printer_driver.c
CAPT = $(wildcard ../components/capt/vendor/*.c) ../components/capt/capt_transport.c
PRINTERS = ../components/printers/standard_mono.c ../components/printers/printers.c ../components/printers/canon_pixma.c ../components/printers/canon_lbp2900.c $(CAPT) ../components/printers/hp_p1006.c ../components/xqx/vendor/jbig.c ../components/xqx/vendor/jbig_ar.c
WEBUI = ../components/webui/webui.c ../components/webui/web_assets.c
SCAN_CORE = ../components/scanner/pixma_scan.c ../components/scanner/jpeg_enc.c ../components/scanner/fake_scanner.c
SCANNER = $(SCAN_CORE) ../components/scanner/shared_buf.c ../components/scanner/scan_service.c
OLED = ../components/oled/oled_ui.c
INC = -I../components/xqx/vendor -I../components/capt/include -I../components/capt/vendor -I../components/canon_bjl/include -I../components/airprint/include -I../components/airprint \
      -I../components/scanner/include -I../components/scanner -I../components/webui/include \
      -I../components/oled/include

all: status_led_host reconnect_host mono_host bjl_host airprint_host scan_host oled_host capt_host improv_host hp_host memory_host standard_host network_config_host hp_bootstrap_host portal_dns_host

bjl_host: bjl_host.c $(BJL) $(PRINTERS) ../components/airprint/printer_driver.c ../components/canon_bjl/include/*.h \
          ../components/airprint/include/printer_driver.h
	$(CC) $(CFLAGS) $(INC) bjl_host.c $(BJL) $(PRINTERS) ../components/airprint/printer_driver.c -lm -o $@

airprint_host: airprint_host.c capt_sim.h $(BJL) $(AIRPRINT) $(PRINTERS) $(SCANNER) $(WEBUI) $(OLED) ../components/airprint/include/*.h \
               ../components/airprint/*.h ../components/scanner/include/*.h ../components/webui/include/*.h \
               ../components/oled/include/*.h
	$(CC) $(CFLAGS) $(INC) airprint_host.c $(BJL) $(AIRPRINT) $(PRINTERS) $(SCANNER) $(WEBUI) $(OLED) -lm -lpthread -o $@

# the web UI (web/), built and packed when its sources changed
../components/webui/web_assets.c: FORCE
	@python3 ../tools/gen_web.py
FORCE:
.PHONY: all clean FORCE

scan_host: scan_host.c $(SCAN_CORE) ../components/scanner/include/*.h
	$(CC) $(CFLAGS) $(INC) scan_host.c $(SCAN_CORE) -lm -o $@

oled_host: oled_host.c $(OLED) ../components/oled/include/*.h ../components/airprint/include/*.h \
           ../components/scanner/include/*.h
	$(CC) $(CFLAGS) $(INC) oled_host.c $(OLED) -o $@

clean:
	rm -rf status_led_host reconnect_host bjl_host airprint_host scan_host oled_host hp_p1006_profile_host hp_p1008_host out *.prn *.png *.raw

capt_host: capt_reference/hiscoa-decompress.c capt_host.c capt_sim.h $(PRINTERS) $(BJL) ../components/airprint/printer_driver.c
	$(CC) $(CFLAGS) $(INC) capt_host.c capt_reference/hiscoa-decompress.c $(PRINTERS) $(BJL) ../components/airprint/printer_driver.c -lm -o $@

improv_host: improv_host.c ../components/improv_serial/improv_serial.c ../components/improv_serial/include/improv_serial.h
	$(CC) $(CFLAGS) -I../components/improv_serial/include improv_host.c ../components/improv_serial/improv_serial.c -o $@

hp_host: hp_host.c $(PRINTERS) $(BJL) ../components/airprint/printer_driver.c
	$(CC) $(CFLAGS) $(INC) hp_host.c $(PRINTERS) $(BJL) ../components/airprint/printer_driver.c -lm -o $@

hp_p1006_profile_host: hp_host.c ../components/printers/printers.c ../components/printers/hp_p1006.c \
                       ../components/xqx/vendor/jbig.c ../components/xqx/vendor/jbig_ar.c \
                       ../components/xqx/vendor/jbig_ar.h ../components/printers/mono_raster.h \
                       ../components/printers/hp_p1006_expand.h
	$(CC) $(CFLAGS) $(INC) -DPRINTSERVER_PROFILE_HP $(filter %.c,$^) -lm -o $@

hp_p1008_host: hp_host.c ../components/printers/printers.c ../components/printers/hp_p1006.c \
               ../components/xqx/vendor/jbig.c ../components/xqx/vendor/jbig_ar.c
	$(CC) $(CFLAGS) $(INC) -DPRINTSERVER_PROFILE_HP -DPRINTSERVER_PROFILE_HP_P1008 $(filter %.c,$^) -lm -o $@

memory_host: memory_host.c capt_sim.h $(PRINTERS) $(BJL) ../components/airprint/printer_driver.c
	$(CC) $(CFLAGS) $(INC) memory_host.c $(PRINTERS) $(BJL) ../components/airprint/printer_driver.c -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -lm -o $@

standard_host: standard_host.c $(PRINTERS) $(BJL) ../components/airprint/printer_driver.c
	$(CC) $(CFLAGS) $(INC) standard_host.c $(PRINTERS) $(BJL) ../components/airprint/printer_driver.c -lm -o $@

network_config_host: network_config_host.c ../components/network_config/network_config.c
	$(CC) $(CFLAGS) -I../components/network_config/include $^ -o $@

hp_bootstrap_host: hp_bootstrap_host.c ../components/hp_bootstrap/hp_bootstrap.c ../components/hp_bootstrap/vendor/puff.c
	$(CC) $(CFLAGS) -I../components/hp_bootstrap/include -I../components/hp_bootstrap/vendor $^ -o $@

portal_dns_host: portal_dns_host.c ../components/portal_dns/portal_dns.c
	$(CC) $(CFLAGS) -I../components/portal_dns/include $^ -o $@

# Shared raster implementation changes must invalidate every consumer.
capt_host hp_host hp_p1008_host airprint_host memory_host standard_host: ../components/printers/mono_raster.h
mono_host: mono_host.c ../components/printers/mono_raster.h
	$(CC) $(CFLAGS) mono_host.c -o $@

reconnect_host: reconnect_host.c capt_sim.h ../src/status_led_pattern.h $(BJL) $(AIRPRINT) $(PRINTERS)
	$(CC) $(CFLAGS) $(INC) reconnect_host.c $(BJL) $(filter-out ../components/airprint/airprint.c,$(AIRPRINT)) $(PRINTERS) -lm -lpthread -o $@

# Native sanitizer build with ESP HTTP limits and injected heap admission.
airprint_esp_policy_host: airprint_host.c capt_sim.h $(BJL) $(AIRPRINT) $(PRINTERS) $(SCANNER) $(WEBUI) $(OLED)
	$(CC) $(CFLAGS) -DAIRPRINT_ESP_POLICY_TEST $(INC) airprint_host.c $(BJL) $(AIRPRINT) $(PRINTERS) $(SCANNER) $(WEBUI) $(OLED) -lm -lpthread -o $@

status_led_host: status_led_host.c ../src/status_led_pattern.h
	$(CC) $(CFLAGS) status_led_host.c -o $@

hp_expand_host: hp_expand_host.c ../components/printers/hp_p1006_expand.h
	$(CC) $(CFLAGS) hp_expand_host.c -o $@
hp_host hp_p1008_host: ../components/printers/hp_p1006_expand.h

# Exercise the actual HP profile read-ahead path; the generic host excludes it.
hp_ipp_host: airprint_host.c capt_sim.h $(BJL) $(AIRPRINT) $(PRINTERS) $(SCANNER) $(WEBUI) $(OLED)
	$(CC) $(CFLAGS) -DPRINTSERVER_PROFILE_HP $(INC) airprint_host.c $(BJL) $(AIRPRINT) $(PRINTERS) $(SCANNER) $(WEBUI) $(OLED) -lm -lpthread -o $@
