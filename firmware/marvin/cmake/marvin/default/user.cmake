target_sources(marvin_default_default_XC32_compile PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/log.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/video/video.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/video/tc358743.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/video/isc_capture.c"
)

target_compile_definitions(marvin_default_default_XC32_compile PRIVATE
    CAMERA_ENABLE_DEBUG=0
)
