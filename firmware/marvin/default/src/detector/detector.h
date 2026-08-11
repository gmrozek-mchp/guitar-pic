#ifndef DETECTOR_H
#define DETECTOR_H

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "queue.h"

#include "fret.h"

/* Detector subsystem — see firmware/marvin/docs/spec.md §4.2.
 *
 * A detector is anything that turns observation data into a detector_state_t
 * event: cv_marvin_v1 reads captured video frames; adc_fretboard (future)
 * reads UART-streamed ADC samples from the fretboard MCU; off-board detectors
 * may feed state in over UART/Ethernet.
 *
 * Every detector publishes detector_state_t records onto a single FreeRTOS
 * queue (the "detector-state bus"). The timing pipeline (§4.4) is the sole
 * consumer in steady state; until it lands a stub task drains the queue so
 * producers don't block. */

typedef enum
{
    DETECTOR_CV_MARVIN_V1 = 0,
    DETECTOR_FRETBOARD    = 1,
} detector_id_t;

/* Canonical record. Layout is fixed-width / naturally aligned / little-endian
 * because this is also the on-disk format for state.bin (spec §4.6.5). Do not
 * reorder fields without bumping the recording schema version. */
typedef struct
{
    uint32_t frame_epoch;     /* master sync token, spec §4.6.4 */
    uint32_t strike_at_ms;    /* when this observation reaches the strike line:
                               * timestamp_us/1000 + detector observation lead.
                               * The timing pipeline schedules in this time base
                               * (spec §4.4); the detector owns the lead. */
    uint64_t timestamp_us;    /* monotonic, marvin-local */
    uint8_t  detector_id;     /* detector_id_t */
    uint8_t  reserved[3];
    struct
    {
        uint8_t  pressed;     /* 0/1 hard call */
        uint16_t confidence;  /* 0..65535, detector-defined */
        uint16_t raw_value;   /* detector-defined: ADC sample, pixel mean, etc. */
    } fret[FRET_COUNT];
} detector_state_t;

/* Brings up the detector-state bus queue, the cv_marvin_v1 detector task,
 * and (for now) a stub consumer. All detectors start disabled; the app
 * must Detector_Enable() each one it wants publishing. Active selection
 * defaults to DETECTOR_CV_MARVIN_V1 for symmetry but is independent of
 * enabled-state. Call after Video_Initialize so the video frame queue
 * can be subscribed to from inside cv_marvin_v1. */
void Detector_Initialize(void);

/* Producers post records here; consumers (timing pipeline, eventually) read
 * them. Returns NULL until Detector_Initialize has run. */
QueueHandle_t Detector_BusQueue(void);

/* Publish a record onto the bus. The record reaches the bus only if its
 * detector_id is the active one (Detector_SetActive) — arbitration lives
 * here, so the single consumer never sees more than one source and does not
 * filter. Non-active detectors still run and feed their own recording/UI
 * paths; they just don't drive the pipeline. Records are dropped if the bus
 * is full (latest observation matters, not a stale backlog). */
void Detector_Publish(const detector_state_t *state);

/* Per-detector enable: controls whether a detector publishes records onto
 * the bus. Multiple detectors may be enabled simultaneously — recording
 * (§4.6) snoops everything for side-by-side training data. Disabled
 * detectors still consume their input (frames, ADC samples) so input
 * queues don't back up; they just don't emit. Bit operations are atomic
 * on aligned 32-bit reads; writes use a critical section. */
void Detector_Enable(detector_id_t id);
void Detector_Disable(detector_id_t id);
bool Detector_IsEnabled(detector_id_t id);

/* Active selector: which detector's records the timing pipeline (§4.4)
 * acts on. Only one active at a time, irrespective of how many are
 * enabled. The timing pipeline is responsible for filtering by
 * detector_id when it lands; for M1 this is a held setting only. */
void          Detector_SetActive(detector_id_t id);
detector_id_t Detector_GetActive(void);

/* Gameplay window: set true only while marvin is inside an active song, false
 * during menus / navigation / manual control. Driven by the game timing
 * pipeline (GameTiming_SetEnabled). The fretboard is allowed to drive the game
 * only inside this window — outside it marvin owns the controller. */
void Detector_SetGameActive(bool active);

/* True when the fretboard node currently owns the game-critical path: the
 * gameplay window is open AND the fretboard is the active detector. This is
 * both the fretboard's arm predicate and the gate that makes marvin cede
 * fauxmote + the guitar node. False during menus/manual control regardless of
 * the active-detector selection, so marvin always drives navigation. */
bool Detector_FretboardDriving(void);

#endif
