target_sources(beatbox_default_default_XC_DSC_compile PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../config.mcc/src/cli.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../config.mcc/src/third_party/embedded-cli/embedded_cli.c"
)

target_include_directories(beatbox_default_default_XC_DSC_compile PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../config.mcc/src"
    "${CMAKE_CURRENT_LIST_DIR}/../../../config.mcc/src/third_party/embedded-cli"
)
