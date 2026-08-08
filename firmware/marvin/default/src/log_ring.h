#ifndef MARVIN_LOG_RING_H
#define MARVIN_LOG_RING_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "log.h"

/* In-memory capture of the lines log.c sends to DBGU, so the operator UI can show
 * them (see ui/screens/log). Installed as log.c's sink with
 * log_set_sink(log_ring_vwrite); nothing is captured until then.
 *
 * Only lines that pass the runtime severity filter reach here, so the ring holds
 * exactly what went out over serial and `log level debug` widens both together.
 *
 * The writer is serialized by log.c's own lock; readers are not, and deliberately do
 * not take it — a reader would otherwise queue behind a task parked in polled DBGU
 * output. log_ring_get instead detects the one race that matters (the oldest slot
 * being recycled mid-copy) via the per-entry sequence number. */

/* Depth. An entry is 156 B (measured, not derived — XC32 packs log_level_t to one byte with
 * -fshort-enums), so this costs 76 KB of .bss, which sits in the 216 MB cacheable `ram`
 * region rather than under the canvases' ram_nocache pressure. RAM is not what bounds this:
 * 500 rows is ~35 screenfuls, about as far back as a 1:1 drag will realistically reach, and
 * a full `log dump` is already ~4 s of serial at 115200. Going deeper is for grepping a
 * capture, not for reading on the panel, and would want a filter rather than more rows. */
#define LOG_RING_ENTRIES  500u
#define LOG_RING_TEXT     144u   /* wider than the panel's message column can show */

typedef struct
{
    uint32_t    seq;        /* 1-based publish order; 0 = slot never written */
    uint32_t    uptime_ms;
    log_level_t lvl;
    uint8_t     src_len;    /* leading chars of text[] that are the tag; 0 = untagged */
    uint8_t     msg_off;    /* text[] offset of the message */
    char        text[LOG_RING_TEXT];   /* "TAG: message", trailing CR/LF stripped */
} log_ring_entry_t;

/* log.c sink. Formats into the ring and publishes. */
void log_ring_vwrite(log_level_t lvl, const char *fmt, va_list ap);

/* Total lines captured since boot, monotonic. A reader polls this to find out whether
 * anything changed without copying an entry. */
uint32_t log_ring_seq(void);

/* Lines currently held, 0..LOG_RING_ENTRIES. */
uint32_t log_ring_count(void);

/* Copy the entry `age` lines back from the newest (age 0 = newest). False if `age` is
 * beyond what is held, or if that slot was recycled while being copied — the caller
 * should treat a false as "no line here" rather than retrying, since a slot being
 * recycled means the log is outrunning the reader anyway. */
bool log_ring_get(uint32_t age, log_ring_entry_t *out);

/* Error / warning counts over the lines still held (not since boot), and how many are
 * held. One pass, so a screen footer costs a single call. */
void log_ring_counts(uint32_t *errors, uint32_t *warnings, uint32_t *held);

#endif /* MARVIN_LOG_RING_H */
