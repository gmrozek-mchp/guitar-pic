idf_component_register(SRCS "main.c" "bt_hid_device.c" "wiimote_sdp.c" "wiimote.c" "status_led.c"
                       INCLUDE_DIRS "."
                       REQUIRES bt nvs_flash esp_driver_gpio)

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
