#ifndef PERF_LOG_RECORDS_H
#define PERF_LOG_RECORDS_H

#include <stdint.h>

#include "game/fret.h"

/* Perf-log wire format. Mirror this header byte-for-byte in the host
 * decoder (tools/marvin-perf). Bump PERF_LOG_SCHEMA_VERSION on
 * every layout change; the host refuses streams that don't match.
 *
 * All fields little-endian, naturally aligned, fixed width. Records
 * are framed on the wire by the drain task (SOF magic + length + CRC);
 * the structs below are the framed payload only. */

#define PERF_LOG_SCHEMA_VERSION   5u

#define PERF_LOG_HDR_MAGIC        0x4D56u   /* 'M','V' little-endian */

/* SOF bytes: 0x55 0x4D 0x52 0x56 ("UMRV"). Distinct from the 16-bit
 * header magic so a mid-stream reader can resync on a 4-byte pattern. */
#define PERF_LOG_SOF_0            0x55u
#define PERF_LOG_SOF_1            0x4Du
#define PERF_LOG_SOF_2            0x52u
#define PERF_LOG_SOF_3            0x56u

typedef enum
{
    PERF_REC_SESSION         = 0x01,
    PERF_REC_STAMP           = 0x02,
    PERF_REC_DETECTOR        = 0x03,
    PERF_REC_TIMING          = 0x04,
    PERF_REC_STRIP           = 0x05,
    PERF_REC_DROP            = 0x06,
    PERF_REC_TASK_HIGHWATER  = 0x07,
    PERF_REC_TASK_RUNTIME    = 0x08,
    PERF_REC_DETECTOR_CONFIG = 0x09,
    PERF_REC_ACTUATOR        = 0x0A,
    PERF_REC_FRETBOARD_RAW   = 0x0B,
} perf_rec_type_t;

/* stage_id values for PERF_REC_STAMP. Producer call sites map 1:1. */
typedef enum
{
    PERF_STAGE_ISC_IRQ            = 0x10,
    PERF_STAGE_VIDEO_PUBLISH      = 0x11,
    PERF_STAGE_CV_START           = 0x20,
    PERF_STAGE_CV_END             = 0x21,
    PERF_STAGE_TP_TICK            = 0x30,
    PERF_STAGE_FBL_SEND           = 0x40,
    PERF_STAGE_CDC_WRITE_COMPLETE = 0x41,
    /* PERF_STAGE_FBL_READ_COMPLETE = 0x42 — slot reserved; per-Read emit
     * was tried during fretboard-RX bring-up but at the fretboard's 240 Hz
     * polling rate it doubled the timeline marker count and made Plotly's
     * SVG scatter freeze the live page. FRETBOARD_RAW records carry their
     * own ts_counter so the same Read-side timing is recoverable offline
     * without a per-Read stamp. Re-enable here only if a sub-frame USB
     * latency tuning task explicitly needs it. */
} perf_stage_t;

#define PERF_FLAG_FROM_ISR        0x01u

/* Common header on every record. 16 B. */
typedef struct __attribute__((packed))
{
    uint16_t magic;           /* PERF_LOG_HDR_MAGIC */
    uint8_t  type;            /* perf_rec_type_t */
    uint8_t  flags;           /* PERF_FLAG_* */
    uint32_t frame_epoch;     /* Video_FrameInfo.frame_count at producer site */
    uint64_t ts_counter;      /* SYS_TIME raw counter; host divides by timer_freq_hz */
} perf_hdr_t;

/* PERF_REC_SESSION — emitted once when the drain task starts. The host
 * decoder uses timer_freq_hz to convert ts_counter to seconds and
 * schema_version to refuse mismatched streams. */
typedef struct __attribute__((packed))
{
    perf_hdr_t hdr;
    uint32_t   timer_freq_hz;
    uint16_t   schema_version;
    uint16_t   reserved;
    uint32_t   fw_git_short;  /* zero until we wire a build-time symbol */
    uint32_t   reserved2;
} perf_rec_session_t;

/* PERF_REC_STAMP — single-point timestamp marker. aux is stage-specific:
 *   ISC_IRQ           : reserved (0)
 *   VIDEO_PUBLISH     : subscriber bitmask
 *   CV_START / CV_END : reserved (0)
 *   TP_TICK           : publish_mask (low 7 bits)
 *   FBL_SEND          : queued mask
 *   CDC_WRITE_COMPLETE: USB CDC result code */
typedef struct __attribute__((packed))
{
    perf_hdr_t hdr;
    uint8_t    stage_id;      /* perf_stage_t */
    uint8_t    reserved[3];
    uint32_t   aux;
    uint32_t   reserved2;
    uint32_t   reserved3;
} perf_rec_stamp_t;

/* PERF_REC_DETECTOR — per-frame snapshot of cv_marvin_v1 internals.
 * hold/edge values use the same 0..65535 scaling as
 * detector_state_t.{raw_value, confidence} so the host can share one
 * conversion path. */
typedef struct __attribute__((packed))
{
    perf_hdr_t hdr;
    uint16_t   hold_dist[FRET_COUNT];   /* 5 × 2 = 10 B */
    uint16_t   edge_dist[FRET_COUNT];   /* 5 × 2 = 10 B */
    uint8_t    pressed_mask;
    uint8_t    edge_active_mask;
    uint16_t   reserved;
} perf_rec_detector_t;

/* PERF_REC_TIMING — per-frame snapshot of timing_pipeline internal state.
 * v3 widens the v2 counts-only record into a full snapshot so the host
 * can answer "why did it publish that mask at frame N" by replaying
 * detector events forward through the visible queue state. All *_at_ms
 * deadlines are deltas from the same now_ms field; chord_age_ms is
 * (now_ms - chord_start_ms) for the currently-open chord window. Counts
 * are 0..TP_FIFO_CAP (32 today). Masks use the same TIMING_BIT_* bit
 * layout as the wire byte to fretboard. */
typedef struct __attribute__((packed))
{
    perf_hdr_t hdr;
    uint32_t   now_ms;                /* timing pipeline clock */
    uint8_t    chord_open;            /* 0/1 — window currently accepting */
    uint8_t    chord_mask;            /* mask accumulated in the open window */
    uint16_t   chord_age_ms;          /* now_ms - chord_start_ms when open */
    uint8_t    note_q_count;
    uint8_t    note_head_mask;        /* mask of front-of-queue (next to fire) */
    uint8_t    note_tail_mask;        /* union of all queued note masks */
    uint8_t    reserved;
    uint32_t   note_head_at_ms;       /* assert_at_ms of front-of-queue */
    uint8_t    strum_q_count;
    uint8_t    strum_head_mask;
    uint8_t    strum_dir_next;        /* 0=none, 1=down, 2=up — next strum */
    uint8_t    reserved2;
    uint32_t   strum_head_at_ms;      /* strum_at_ms of front-of-queue */
    uint8_t    frets_active;          /* asserted mask, pre-strum overlay */
    uint8_t    strum_active;          /* 0/1 — strum pulse currently held */
    uint8_t    release_pending_mask;  /* frets with pending release timer */
    uint8_t    publish_mask;          /* final wire output mask */
    uint32_t   strum_release_at_ms;   /* deadline for current strum pulse */
    uint32_t   release_min_at_ms;     /* earliest pending-release deadline */
} perf_rec_timing_t;

/* PERF_REC_DROP — drain task emits at 1 Hz. Counters are cumulative
 * since session start; host computes deltas. */
typedef struct __attribute__((packed))
{
    perf_hdr_t hdr;
    uint32_t   dropped_state;
    uint32_t   dropped_strip;
    uint32_t   dropped_sink;
    uint32_t   reserved;
} perf_rec_drop_t;

/* PERF_REC_TASK_HIGHWATER — periodic stack high-water dump. task_id is
 * a stable enum mirrored in the host decoder; words is the
 * uxTaskGetStackHighWaterMark return (StackType_t units).
 *
 * Marvin-owned tasks (0..5) are registered at task creation. IDLE is
 * the FreeRTOS idle task (xTaskGetIdleTaskHandle) — its run_time_counter
 * is what makes absolute CPU% computable on the host
 * (1 - idle_delta / Σ_all_delta). MCC tasks (LEGATO..APP) are looked up
 * by string name with xTaskGetHandle from the perf-drain task at startup.
 * OTHER is a pseudo-slot: not a real task, just an aggregate of "all
 * tasks not in this enum" that the drain emits each runtime cycle so
 * per-window CPU% adds to 100. HWM is meaningless for OTHER (no single
 * stack); the runtime-side producer emits TASK_RUNTIME for OTHER but
 * skips TASK_HIGHWATER. */
typedef enum
{
    PERF_TASK_VIDEO          = 0,
    PERF_TASK_CV_MARVIN_V1   = 1,
    PERF_TASK_DETECTOR_DRAIN = 2,
    PERF_TASK_TIMING         = 3,
    PERF_TASK_FRETBOARD_LINK = 4,
    PERF_TASK_PERF_DRAIN     = 5,
    PERF_TASK_IDLE           = 6,
    PERF_TASK_LEGATO         = 7,
    PERF_TASK_XLCDC          = 8,
    PERF_TASK_MAXTOUCH       = 9,
    PERF_TASK_SYS_INPUT      = 10,
    PERF_TASK_USB_DEVICE     = 11,
    PERF_TASK_USB_HOST       = 12,
    PERF_TASK_DRV_USB_UDPHS  = 13,
    PERF_TASK_DRV_USB_HOST   = 14,
    PERF_TASK_APP            = 15,
    PERF_TASK_OTHER          = 16,
    PERF_TASK_FRETBOARD_RX   = 17,
} perf_task_id_t;

typedef struct __attribute__((packed))
{
    perf_hdr_t hdr;
    uint8_t    task_id;
    uint8_t    reserved[3];
    uint32_t   words;
} perf_rec_task_highwater_t;

/* PERF_REC_STRIP — variable-size BGR888 region of interest, kind-tagged.
 * Today's producers emit SENSING (row through the sensor patches) and
 * STRIKE (row through the strum trigger zone), each 240×32 once per ISC
 * frame; SNAPSHOT carries a full frame as a top-to-bottom sequence of
 * full-width bands sharing one frame_epoch (last band flags LAST).
 * Future kinds (SCORE, MINIMAP, etc.) plug into the same record type —
 * adding one is an enum entry plus a producer; no schema bump.
 *
 * Wire payload size is HDR + 12 B body + w*h*PERF_STRIP_BPP bytes,
 * tightly packed. The queue slot is sized to PERF_STRIP_MAX_BYTES; the
 * drain task computes the on-wire length from (w, h) and frames only
 * what's used. */
typedef enum
{
    PERF_STRIP_SENSING  = 0,
    PERF_STRIP_STRIKE   = 1,
    PERF_STRIP_SNAPSHOT = 2,
} perf_strip_kind_t;

/* Strip flags byte (perf_rec_strip_t.flags). SNAPSHOT producers set LAST on
 * the final (bottom) band so the host knows the frame is complete without a
 * separate end marker. Zero for SENSING/STRIKE. */
#define PERF_STRIP_FLAG_LAST  0x01u

#define PERF_STRIP_BPP        3u

/* Wire-bound on the strip pixel-data size. Two stacked caps determine
 * the maximum payload a strip can carry:
 *
 *   1. Wire LEN field is u16 → max payload 65535 bytes
 *      → max pixel = 65535 − HDR(16) − BODY(12) = 65507
 *   2. UDPHS DMA single-transfer cap is
 *      DRV_USB_UDPHS_DMA_MAX_TRANSFER_SIZE × 64 KB
 *      (currently 2 × 64 KB = 128 KB; well above the LEN cap)
 *
 * The u16 LEN field is the binding constraint. Set at 65000 to land just
 * under it with a small margin. Producers may use any (w, h) shape as
 * long as w * h * PERF_STRIP_BPP <= PERF_STRIP_MAX_BYTES.
 *
 * Practical strip-size envelope (pixel area = w*h ≤ ~21666):
 *   720×30  640×33  480×45  320×64  290×72
 *
 * Buffer footprint scales with this:
 *   pool      = PL_STRIP_POOL_SIZE × (28 + PERF_STRIP_MAX_BYTES)
 *   sink ring = SINK_TX_RING_DEPTH × round_up_64(36 + PERF_STRIP_MAX_BYTES)
 * Together ~585 KB BSS at 65000; trivially fits in the 240 MB cached DDR.
 *
 * To go beyond u16 LEN, the wire format itself needs a schema break (LEN
 * → u32). Don't do that lightly — at 60 fps × 65 KB = ~3.9 MB/s, this cap
 * already lines up with what the wire can sustainably carry. */
#define PERF_STRIP_MAX_BYTES  65000u

#define PERF_STRIP_HDR_BYTES  (sizeof(perf_hdr_t) + 12u)   /* hdr + body */

typedef struct __attribute__((packed))
{
    perf_hdr_t hdr;
    uint16_t   x, y;          /* top-left in source frame */
    uint16_t   w, h;          /* per-record dimensions */
    uint8_t    kind;          /* perf_strip_kind_t */
    uint8_t    flags;         /* PERF_STRIP_FLAG_* */
    uint8_t    reserved[2];
    uint8_t    bgr[PERF_STRIP_MAX_BYTES];   /* sized to max in queue slot */
} perf_rec_strip_t;

/* PERF_REC_TASK_RUNTIME — per-task snapshot of state, priority, and
 * cumulative run-time counter. Drain task emits one record per known
 * task at 1 Hz alongside DROP and TASK_HIGHWATER. Host computes per-
 * window CPU% as Δrun_time_counter[task] / ΣΔrun_time_counter. */
typedef enum
{
    PERF_TASK_STATE_RUNNING   = 0,
    PERF_TASK_STATE_READY     = 1,
    PERF_TASK_STATE_BLOCKED   = 2,
    PERF_TASK_STATE_SUSPENDED = 3,
    PERF_TASK_STATE_DELETED   = 4,
    PERF_TASK_STATE_INVALID   = 5,
} perf_task_state_t;

typedef struct __attribute__((packed))
{
    perf_hdr_t hdr;
    uint8_t    task_id;            /* perf_task_id_t */
    uint8_t    state;              /* perf_task_state_t (mirrors eTaskState) */
    uint8_t    priority;           /* uxCurrentPriority */
    uint8_t    reserved;
    uint32_t   run_time_counter;   /* low 32 bits of ulRunTimeCounter */
    uint32_t   reserved2;
} perf_rec_task_runtime_t;

/* PERF_REC_DETECTOR_CONFIG — cv_marvin_v1 per-fret configuration. Static
 * today (compile-time tables); becomes runtime-tunable when M6 calibration
 * UI lands. Emitted on connect-up edge and on change so the host always
 * has a current copy. Sample coords are in capture-frame space (matching
 * STRIP record (x, y) anchors). Floats are IEEE 754 little-endian — same
 * representation on both sides. Used by the host to render sample
 * dots / threshold reference lines onto STRIP canvases. */
typedef struct __attribute__((packed))
{
    perf_hdr_t hdr;
    uint16_t   sensor_hx[FRET_COUNT];          /* hold-sensor x per fret */
    uint16_t   sensor_hy[FRET_COUNT];          /* hold-sensor y per fret */
    uint16_t   sensor_ex[FRET_COUNT];          /* edge-sensor x per fret */
    uint16_t   sensor_ey[FRET_COUNT];          /* edge-sensor y per fret */
    float      hold_thresh;                    /* CV_HOLD_THRESH (brightness) */
    float      hold_release_frac;              /* CV_HOLD_RELEASE_FRAC */
    float      edge_thresh;                    /* CV_EDGE_THRESH */
    float      color_target_b[FRET_COUNT];     /* per-fret target weights */
    float      color_target_g[FRET_COUNT];
    float      color_target_r[FRET_COUNT];
    float      color_reject_b[FRET_COUNT];     /* per-fret reject weights */
    float      color_reject_g[FRET_COUNT];
    float      color_reject_r[FRET_COUNT];
} perf_rec_detector_config_t;

/* PERF_REC_ACTUATOR — emitted on every FretboardLink_Send call (i.e. on
 * intent), one record per call. Closes the gap between TIMING.publish_mask
 * (intent on the timing pipeline side) and FBL_SEND STAMP (a byte hit the
 * USB DMA): with multiple producers (timing_pipeline, manual_control, future
 * game_state_controller per spec §4.8), the wire byte alone doesn't say
 * who asserted what.
 *
 * intended_mask: what the producer asked for this Send (low 7 bits).
 * asserted_mask: what's currently held on the link (last byte queued).
 * strum_dir: from the bit pattern (0=none, 1=down, 2=up).
 * producer_id: perf_actuator_producer_t — the arbitration signal that's
 *              invisible on the wire today.
 * last_ack_*:  most recent CDC_WRITE_COMPLETE result and timestamp,
 *              snapshotted at Send-time so the host can compute transport-
 *              side latency without joining FBL_SEND/CDC_WRITE_COMPLETE
 *              stamps (still useful, but redundant for the common case). */
typedef enum
{
    PERF_ACTUATOR_PRODUCER_NONE   = 0,
    PERF_ACTUATOR_PRODUCER_TIMING = 1,
    PERF_ACTUATOR_PRODUCER_MANUAL = 2,
} perf_actuator_producer_t;

typedef struct __attribute__((packed))
{
    perf_hdr_t hdr;
    uint8_t    intended_mask;
    uint8_t    asserted_mask;
    uint8_t    strum_dir;            /* 0=none, 1=down, 2=up */
    uint8_t    producer_id;          /* perf_actuator_producer_t */
    int32_t    last_ack_result;      /* USB_HOST_CDC_RESULT_*, signed for safety */
    uint64_t   last_ack_ts_counter;  /* SYS_TIME at most-recent CDC_WRITE_COMPLETE */
} perf_rec_actuator_t;

/* PERF_REC_FRETBOARD_RAW — one record per parsed 17-byte fretboard data
 * frame (firmware/fretboard/data_stream.c). The 5×u16 ADC values are 12-bit
 * unsigned (0–4095, lower = brighter sensor) indexed by fret_t (G/R/Y/B/O).
 * Fretboard emits at ~240 Hz; with video at 60 Hz the same frame_epoch
 * value tags ~4 consecutive records — ts_counter (stamped by hdr_fill at
 * record-emit time) disambiguates within the frame.
 *
 * fb_sample_seq and applied_mask are stamped by the fretboard itself and
 * carried on the wire: the sequence counter (one per fretboard tick) lets the
 * host reconstruct true sample order and detect frames dropped in transit,
 * and applied_mask is the actuator bitmask the fretboard was driving during
 * this very scan — so sensor and actuator state are paired atomically at the
 * source instead of joined across marvin's TX/RX clocks. Default-disabled at
 * boot like the other high-rate types. */
typedef struct __attribute__((packed))
{
    perf_hdr_t hdr;
    uint16_t   adc[FRET_COUNT];      /* 5 × 2 = 10 B */
    uint32_t   fb_sample_seq;        /* fretboard tick counter (monotonic) */
    uint8_t    applied_mask;         /* actuator bitmask driven this scan */
    uint8_t    reserved;
} perf_rec_fretboard_raw_t;

/* ─── Host→device commands ───────────────────────────────────────────────────
 *
 * Wrapped in the same SOF/LEN/CRC framer as TX records. Magic is distinct
 * from PERF_LOG_HDR_MAGIC so a misrouted record frame can't be parsed as
 * a command (or vice versa). The device reads commands on the CDC OUT
 * endpoint; the host writes them via marvin-perf's set-mask path. */

#define PERF_CMD_HDR_MAGIC   0x4D43u   /* 'M','C' little-endian */

typedef enum
{
    PERF_CMD_SET_TYPE_MASK = 0x01u,
    PERF_CMD_SNAPSHOT      = 0x02u,
    PERF_CMD_SET_OVERLAY   = 0x03u,
} perf_cmd_t;

/* Overlay sinks for the per-fret target rings, gated independently via
 * PERF_CMD_SET_OVERLAY. STRIP draws the rings onto the SENSING strip copy
 * that ships to the host viewer (never the capture buffer, so snapshots and
 * the LVDS panel stay clean). PANEL is reserved for a future LVDS demo
 * overlay and is a no-op today. */
#define PERF_OVERLAY_STRIP   0x01u
#define PERF_OVERLAY_PANEL   0x02u

typedef struct __attribute__((packed))
{
    uint16_t magic;        /* PERF_CMD_HDR_MAGIC */
    uint8_t  cmd_id;       /* perf_cmd_t */
    uint8_t  reserved;
} perf_cmd_hdr_t;

/* PERF_CMD_SET_TYPE_MASK — bit i (i = perf_rec_type_t value) gates record
 * type i. SESSION is always emitted regardless of mask so the host can
 * still derive timer_freq_hz on attach. */
typedef struct __attribute__((packed))
{
    perf_cmd_hdr_t hdr;
    uint32_t       enabled_mask;
} perf_cmd_set_mask_t;

/* PERF_CMD_SNAPSHOT — capture the current full video frame and stream it back
 * as a top-to-bottom sequence of full-width PERF_REC_STRIP records (kind
 * SNAPSHOT), all sharing one frame_epoch, the last flagged LAST. Header-only;
 * "current frame, full resolution" needs no parameters. Not gated by the
 * STRIP type mask — the command itself is the request. */

/* PERF_CMD_SET_OVERLAY — set the active overlay sinks (PERF_OVERLAY_* bits).
 * Bits not set disable that sink. */
typedef struct __attribute__((packed))
{
    perf_cmd_hdr_t hdr;
    uint32_t       flags;
} perf_cmd_set_overlay_t;

#endif /* PERF_LOG_RECORDS_H */
