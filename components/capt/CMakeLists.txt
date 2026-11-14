idf_component_register(SRCS "capt_transport.c" "vendor/prn_lbp2900.c"
    "vendor/capt-status.c" "vendor/generic-ops.c" "vendor/hiscoa-common.c"
    "vendor/hiscoa-compress.c"
    INCLUDE_DIRS "include" "vendor" REQUIRES airprint)
# Upstream retains additional model helper functions that are not exported.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function)
