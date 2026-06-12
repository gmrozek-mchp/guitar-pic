#pragma once

#include <stdbool.h>

/* Build + register the exact Wiimote (RVL-CNT-01) SDP record into Bluedroid's
 * SDP server, using the internal SDP_* database API. Returns false on failure.
 * See docs/wiimote-sdp.md for the record this replicates. */
bool WiimoteSdp_Register(void);
