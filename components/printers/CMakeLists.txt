# ESP-IDF expands requirements before project profile variables are available.
# Keep discovery complete; the normal pass still selects only its model sources.
if(CMAKE_BUILD_EARLY_EXPANSION)
  idf_component_register(REQUIRES airprint canon_bjl capt xqx heap capt_family)
  return()
endif()
# Dedicated firmware only compiles the selected printer family.
if(PRINTSERVER_PROFILE STREQUAL "lbp6000" OR PRINTSERVER_PROFILE STREQUAL "lbp6200" OR PRINTSERVER_PROFILE STREQUAL "lbp3010")
  idf_component_register(SRCS "printers.c" "canon_capt_family.c" REQUIRES airprint capt_family heap)
elseif(PRINTSERVER_PROFILE STREQUAL "hp" OR PRINTSERVER_PROFILE STREQUAL "hp_p1008")
  idf_component_register(SRCS "printers.c" "hp_p1006.c" REQUIRES airprint xqx heap)
elseif(PRINTSERVER_PROFILE STREQUAL "canon")
  idf_component_register(SRCS "printers.c" "canon_lbp2900.c" REQUIRES airprint capt heap)
elseif(PRINTSERVER_PROFILE STREQUAL "pixma")
  idf_component_register(SRCS "printers.c" "canon_pixma.c" REQUIRES airprint canon_bjl heap)
elseif(PRINTSERVER_PROFILE STREQUAL "standard")
  idf_component_register(SRCS "printers.c" "standard_mono.c" REQUIRES airprint heap)
else()
  idf_component_register(SRCS "printers.c" "canon_pixma.c" "canon_lbp2900.c" "hp_p1006.c" "standard_mono.c"
                         REQUIRES airprint canon_bjl capt xqx heap)
endif()
