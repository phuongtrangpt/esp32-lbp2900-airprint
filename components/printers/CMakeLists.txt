# Dedicated firmware only compiles the selected printer family.
if(PRINTSERVER_PROFILE STREQUAL "hp")
  idf_component_register(SRCS "printers.c" "hp_p1006.c" REQUIRES airprint xqx heap)
elseif(PRINTSERVER_PROFILE STREQUAL "canon")
  idf_component_register(SRCS "printers.c" "canon_lbp2900.c" REQUIRES airprint capt heap)
else()
  idf_component_register(SRCS "printers.c" "canon_pixma.c" "canon_lbp2900.c" "hp_p1006.c" "standard_mono.c"
                         REQUIRES airprint canon_bjl capt xqx heap)
endif()
