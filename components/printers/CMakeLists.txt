# Printer drivers (printer_driver.h): one source file per printer family.
idf_component_register(SRCS "printers.c" "canon_pixma.c" "canon_lbp2900.c"
                       REQUIRES airprint canon_bjl capt)
