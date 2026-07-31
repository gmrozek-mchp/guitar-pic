target_sources(beatbox_default_default_XC_DSC_compile PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../config.mcc/src/audio.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../config.mcc/src/cli.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../config.mcc/src/rgb_led.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../config.mcc/src/t1s_follower.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../config.mcc/src/third_party/embedded-cli/embedded_cli.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/oa-tc6-lib/libtc6/src/tc6.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/oa-tc6-lib/libtc6/src/tc6-regs.c"
)

target_include_directories(beatbox_default_default_XC_DSC_compile PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../config.mcc/src"
    "${CMAKE_CURRENT_LIST_DIR}/../../../config.mcc/src/third_party/embedded-cli"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/oa-tc6-lib/libtc6/inc"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/oa-tc6-lib/libtc6/src"
)
