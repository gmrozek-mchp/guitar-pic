target_sources(marvin_default_default_XC32_compile PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/log.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/video/video.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/video/tc358743.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/video/isc_capture.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/detector/detector.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/detector/cv_marvin_v1.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/game/gameplay_engine.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/game/gameplay_classify.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/game/gameplay_select.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/actuator/timing_pipeline.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/actuator/fretboard_link.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/actuator/manual_control.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/ui/manual_input.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/perf_log/perf_log.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/perf_log/perf_log_rx.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/perf_log/perf_log_sink_cdc.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/console/console.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/third_party/embedded-cli/embedded_cli.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/net/t1s/t1s_link.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/oa-tc6-lib/libtc6/src/tc6.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/oa-tc6-lib/libtc6/src/tc6-regs.c"
)

target_include_directories(marvin_default_default_XC32_compile PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/third_party/embedded-cli"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/net/t1s"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/oa-tc6-lib/libtc6/inc"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/oa-tc6-lib/libtc6/src"
)

target_compile_definitions(marvin_default_default_XC32_compile PRIVATE
    CAMERA_ENABLE_DEBUG=0
)
