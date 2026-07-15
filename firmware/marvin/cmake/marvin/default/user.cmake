target_sources(marvin_default_default_XC32_compile PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/log.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/video/video.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/video/tc358743.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/video/isc_capture.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/detector/detector.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/detector/cv_marvin_v1.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/game/game_engine.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/game/gameplay_classify.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/game/gameplay_present.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/game/gameplay_select.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/game/gameplay_score.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/game/game_selection.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/game/game_controller.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/game/game_timing.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/actuator/fretboard_link.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/actuator/manual_control.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/ui/ui_manager.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/ui/dashboard_feed.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/ui/manual_input.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/ui/song_detail.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/ui/screens/dashboard/screen_dashboard.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/ui/screens/video/screen_video.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/ui/screens/navigation/screen_navigation.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/ui/screens/song_select/screen_song_select.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/ui/screens/album_art/screen_album_art.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/ui/screens/splash/screen_splash.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/ui/screens/wiimotes/screen_wiimotes.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/ui/widgets/song_list/widget_song_list.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/ui/widgets/button_aa/widget_button_aa.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/ui/widgets/panel_aa/widget_panel_aa.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/ui/widgets/progressbar_aa/widget_progressbar_aa.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/ui/gfx/aa_corners.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/perf_log/perf_log.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/perf_log/perf_log_rx.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/perf_log/perf_log_sink_cdc.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/console/console.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/health/health_monitor.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/fault/fault_report.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/storage/storage.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/flash/qspi_smoke.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/flash/settings.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/results/results.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/util/csv.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/util/legato_utf8.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/game/game_catalog.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/game/game_art.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/third_party/embedded-cli/embedded_cli.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/net/t1s/t1s_link.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/net/fauxmote/fauxmote_link.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/oa-tc6-lib/libtc6/src/tc6.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/oa-tc6-lib/libtc6/src/tc6-regs.c"
)

target_include_directories(marvin_default_default_XC32_compile PRIVATE
    # Compat shim root: provides a stub gfx/legato/generated/le_gen_init.h so the
    # MGS-emitted `#include le_gen_init.h` in le_gen_harmony.h still resolves after
    # "Generate Screen State Machine" is disabled (MGS deletes the header but keeps
    # the include). Resolves only when the generated copy is absent. See the stub.
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/compat"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/third_party/embedded-cli"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/net/t1s"
    "${CMAKE_CURRENT_LIST_DIR}/../../../default/src/net/fauxmote"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/oa-tc6-lib/libtc6/inc"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../third_party/oa-tc6-lib/libtc6/src"
)

target_compile_definitions(marvin_default_default_XC32_compile PRIVATE
    CAMERA_ENABLE_DEBUG=0
    # Place the Legato CPU-rendered scratch buffer in non-cached DDR (same section
    # as the XLCDC framebuffer). Works around an MGS gen gap: with "Cacheable
    # Frame Buffers" off, the framebuffer is non-cached but the Legato render
    # buffer (LE_NO_CACHE_ATTR) is left empty/cacheable, so the 2D-engine blit
    # reads stale CPU-cached pixels -> streaky bleed. The #ifndef guard in
    # legato_renderer.c lets this -D win, surviving MGS regen.
    "LE_NO_CACHE_ATTR=__attribute__((section(\".region_nocache\")))"
    # Fretboard link transport: default is FLEXCOM1 UART. Uncomment to route
    # the fretboard command/data over the 10BASE-T1S link (LAN8651) instead —
    # flip this once the fretboard PIC32CM T1S side is up.
    MARVIN_FRETBOARD_TRANSPORT=1
)
