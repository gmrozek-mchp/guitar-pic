#include "log_ring.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

/* Capture ring for log.c's output — see log_ring.h for the concurrency contract.
 *
 * An entry with sequence number S always lives at index (S - 1) % LOG_RING_ENTRIES, so a
 * reader locates a line by arithmetic on one volatile read of s_seq and needs no index
 * state of its own. */

static log_ring_entry_t s_ring[LOG_RING_ENTRIES];
static volatile uint32_t s_seq;

static uint32_t uptime_ms(void)
{
    /* 0 before the scheduler starts, which is the truthful uptime for the log calls
     * SYS_Initialize / APP_Initialize make. */
    return (uint32_t)xTaskGetTickCount() * (uint32_t)portTICK_PERIOD_MS;
}

/* A tag character: what marvin's "TAG: message" prefixes are made of. Space is allowed
 * because two real tags contain one ("QSPI verify", "QSPI bench"). */
static bool tag_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
           || c == '_' || c == '-' || c == ' ';
}

/* Longest prefix accepted as a tag. Covers "ISC_Capture" (11) and "freertos heap" (13)
 * with room to spare, and is short enough that a message whose first words happen to
 * contain a colon is not mistaken for one. */
#define TAG_MAX  16u

/* Split "TAG: message" in place, filling src_len / msg_off. Untagged lines get
 * src_len 0 and msg_off 0, so the whole line reads as the message.
 *
 * '|' is accepted alongside ':' for health_monitor's "HM| %s" table lines, which are
 * otherwise the only tagged output in the tree that this would miss. */
static void split_tag(log_ring_entry_t *e, uint32_t len)
{
    e->src_len = 0u;
    e->msg_off = 0u;

    for (uint32_t i = 1u; i < len && i <= TAG_MAX; i++)
    {
        char c = e->text[i];

        if (c == ':' || c == '|')
        {
            /* A separator not followed by a space is punctuation inside a message
             * ("12:34", "a:b"), not the end of a tag. */
            if (e->text[i + 1u] != ' ' && e->text[i + 1u] != '\0') { return; }

            uint32_t off = i + 1u;
            while (e->text[off] == ' ') { off++; }

            e->src_len = (uint8_t)i;
            e->msg_off = (uint8_t)off;
            return;
        }

        if (!tag_char(c)) { return; }
    }
}

void log_ring_vwrite(log_level_t lvl, const char *fmt, va_list ap)
{
    /* Serialized by log.c's lock, which is held across this call. */
    log_ring_entry_t *e = &s_ring[s_seq % LOG_RING_ENTRIES];

    /* Retire the slot before touching its text: for the duration of the format it holds
     * neither the old line nor the new one, and a reader that reaches for it gets a
     * clean miss instead of a mixture. */
    e->seq = 0u;

    int n = vsnprintf(e->text, sizeof e->text, fmt, ap);

    uint32_t len;
    if (n < 0)
    {
        e->text[0] = '\0';
        len = 0u;
    }
    else if ((uint32_t)n >= sizeof e->text)
    {
        /* Truncated. Mark it so a clipped line is not read as a complete one. ASCII
         * rather than U+2026: the design's fonts cover ASCII+Latin-1 and no ellipsis. */
        len = (uint32_t)sizeof e->text - 1u;
        memcpy(&e->text[len - 3u], "...", 3u);
    }
    else
    {
        len = (uint32_t)n;
    }

    /* Log format strings end in "\r\n"; the panel wants one line's worth of text. */
    while (len > 0u && (e->text[len - 1u] == '\n' || e->text[len - 1u] == '\r'
                        || e->text[len - 1u] == ' '))
    {
        e->text[--len] = '\0';
    }

    split_tag(e, len);

    e->uptime_ms = uptime_ms();
    e->lvl       = lvl;

    /* Publish. The critical section is what makes the slot's sequence number and the
     * ring's own counter agree from a reader's point of view. */
    taskENTER_CRITICAL();
    e->seq = s_seq + 1u;
    s_seq  = e->seq;
    taskEXIT_CRITICAL();
}

uint32_t log_ring_seq(void)
{
    return s_seq;
}

uint32_t log_ring_count(void)
{
    uint32_t seq = s_seq;
    return (seq < LOG_RING_ENTRIES) ? seq : LOG_RING_ENTRIES;
}

bool log_ring_get(uint32_t age, log_ring_entry_t *out)
{
    if (out == NULL) { return false; }

    uint32_t seq = s_seq;                    /* one read: the snapshot everything below uses */
    uint32_t held = (seq < LOG_RING_ENTRIES) ? seq : LOG_RING_ENTRIES;
    if (age >= held) { return false; }

    uint32_t want = seq - age;                /* sequence number of the line asked for */
    *out = s_ring[(want - 1u) % LOG_RING_ENTRIES];

    /* The copy raced the writer if the slot no longer holds the line we wanted — either
     * mid-format (seq 0) or already recycled (a higher seq). Either way there is no line
     * to show at this age. */
    return out->seq == want;
}

void log_ring_counts(uint32_t *errors, uint32_t *warnings, uint32_t *held)
{
    uint32_t seq = s_seq;
    uint32_t n   = (seq < LOG_RING_ENTRIES) ? seq : LOG_RING_ENTRIES;
    uint32_t err = 0u, warn = 0u;

    /* Held entries always occupy indices 0..n-1, so this needs no sequence arithmetic.
     * Reading a level straight from the slot can race the writer, but the worst outcome is
     * one line miscounted in a footer — not worth a retry loop. */
    for (uint32_t i = 0u; i < n; i++)
    {
        if (s_ring[i].seq == 0u) { continue; }   /* slot mid-format */

        if      (s_ring[i].lvl == LOG_LEVEL_ERROR) { err++;  }
        else if (s_ring[i].lvl == LOG_LEVEL_WARN)  { warn++; }
    }

    if (errors   != NULL) { *errors   = err;  }
    if (warnings != NULL) { *warnings = warn; }
    if (held     != NULL) { *held     = n;    }
}
