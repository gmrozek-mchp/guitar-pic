target_sources(lemmy_default_default_XC32_compile PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../config.mcc/src/cli.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../config.mcc/src/third_party/embedded-cli/embedded_cli.c"
)

target_include_directories(lemmy_default_default_XC32_compile PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../config.mcc/src"
    "${CMAKE_CURRENT_LIST_DIR}/../../../config.mcc/src/third_party/embedded-cli"
)
