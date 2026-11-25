# Separate protocol/compressor copy: stable CAPT2900 is never linked here.
idf_component_register(SRCS "capt_family_transport.c" "vendor/prn_lbp2900.c"
    "vendor/capt-status.c" "vendor/generic-ops.c" "vendor/hiscoa-common.c"
    "vendor/hiscoa-compress.c"
    INCLUDE_DIRS "include" "vendor" REQUIRES airprint)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -Wno-unused-variable)
