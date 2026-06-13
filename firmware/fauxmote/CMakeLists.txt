# ESP-IDF top-level project file for fauxmote.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Keep the BT link out of sniff (we're USB-powered, and the Wii drops a sniffing
# idle link → supervision timeouts + a BTA PM-slot leak). Bluedroid's JV idle->sniff
# delay defaults to 5 s and is a `#ifndef`'d `#define`, so override it via a global
# compile definition (the timeout field is UINT16, and an internal +197 ms offset is
# added, so keep this < ~65338). Must be set before project() to reach the bt
# component. See firmware/fauxmote/docs/journal.md.
idf_build_set_property(COMPILE_DEFINITIONS "BTA_FTC_OPS_IDLE_TO_SNIFF_DELAY_MS=65000" APPEND)

project(fauxmote)
