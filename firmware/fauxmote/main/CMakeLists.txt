# The marvin link splits into a transport-neutral message layer (mf_link.c) plus
# one transport backend, selected by Kconfig. T1S also pulls in the vendored
# OPEN Alliance TC6 driver (third_party/oa-tc6-lib).
#
# Only SRCS/INCLUDE_DIRS may be gated on CONFIG_ — REQUIRES is evaluated in
# ESP-IDF's early-expansion pass where CONFIG_ symbols are not yet defined, so
# both transports' component requirements are listed unconditionally (the unused
# transport's source is simply not compiled).
set(tc6_dir "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/oa-tc6-lib/libtc6")
set(tc6_srcs "${tc6_dir}/src/tc6.c" "${tc6_dir}/src/tc6-regs.c")

set(link_srcs "mf_link.c")
set(link_incs "")

if(CONFIG_FAUXMOTE_LINK_TRANSPORT_T1S)
    list(APPEND link_srcs "mf_t1s.c" ${tc6_srcs})
    list(APPEND link_incs "${tc6_dir}/inc")
else()
    list(APPEND link_srcs "marvin_link.c")
endif()

idf_component_register(SRCS "neopixel.c" "bt_role.c" "ext_crypto.c" "guitar.c" "console_cli.c" "main.c"
                            "bt_hid_device.c" "wiimote_sdp.c" "wiimote.c" "status_led.c"
                            ${link_srcs}
                       INCLUDE_DIRS "." ${link_incs}
                       REQUIRES bt nvs_flash esp_driver_gpio console
                                esp_driver_spi esp_timer esp_driver_uart esp_driver_rmt)

# The vendored OPEN Alliance TC6 sources are compiled as-is (do not edit third_party);
# their designated-init style trips ESP-IDF's -Werror=missing-field-initializers.
if(CONFIG_FAUXMOTE_LINK_TRANSPORT_T1S)
    set_source_files_properties(${tc6_srcs}
        PROPERTIES COMPILE_OPTIONS "-Wno-error=missing-field-initializers")
endif()

# wiimote_sdp.c uses Bluedroid's internal SDP database API (SDP_CreateRecord /
# SDP_AddAttribute / …) — the public esp_sdp API cannot host an arbitrary record.
# These headers are not exported, so add the internal include dirs explicitly.
idf_component_get_property(bt_dir bt COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${bt_dir}/common/include"
    "${bt_dir}/host/bluedroid/stack/include"
    "${bt_dir}/host/bluedroid/common/include"
    "${bt_dir}/host/bluedroid/osi/include"
    "${bt_dir}/host/bluedroid/api/include/api")
