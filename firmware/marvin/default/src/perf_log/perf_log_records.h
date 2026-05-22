#ifndef PERF_LOG_RECORDS_H
#define PERF_LOG_RECORDS_H

#include <stdint.h>

#include "game/fret.h"

/* Perf-log wire format. Mirror this header byte-for-byte in the host
 * decoder (tools/perf-log-decoder). Bump PERF_LOG_SCHEMA_VERSION on
 * every layout change; the host refuses streams that don't match.
 *
 * All fields little-endian, naturally aligned, fixed width. Records
 * are framed on the wire by the drain task (SOF magic + length + CRC);
 * the structs below are the framed payload only. */

#define PERF_LOG_SCHEMA_VERSION   1u

#define PERF_LOG_HDR_MAGIC        0x4D56u   /* 'M','V' little-endian */

/* SOF bytes: 0x55 0x4D 0x52 0x56 ("UMRV"). Distinct from the 16-bit
 * header magic so a mid-stream reader can resync on a 4-byte pattern. */
#define PERF_LOG_SOF_0            0x55u
#define PERF_LOG_SOF_1            0x4Du
#define PERF_LOG_SOF_2            0x52u
#define PERF_LOG_SOF_3            0x56u

typedef enum
{
    PERF_REC_SESSION        = 0x01,
    PERF_REC_STAMP          = 0x02,
    PERF_REC_DETECTOR       = 0x03,
    PERF_REC_TIMING         = 0x04,
    PERF_REC_PATCH          = 0x05,
    PERF_REC_DROP           = 0x06,
    PERF_REC_TASK_HIGHWATER = 0x07,
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

/* PERF_REC_TIMING — timing_pipeline state at the per-frame tick. */
typedef struct __attribute__((packed))
{
    perf_hdr_t hdr;
    uint8_t    publish_mask;       /* low 7 bits = chord on the wire */
    uint8_t    chord_window_fill;  /* records currently held in chord window */
    uint8_t    fifo_depth;         /* delay FIFO depth */
    uint8_t    strum_dir;          /* 0 = none, 1 = down, 2 = up */
    uint32_t   reserved;
    uint32_t   reserved2;
    uint32_t   reserved3;
} perf_rec_timing_t;

/* PERF_REC_DROP — drain task emits at 1 Hz. Counters are cumulative
 * since session start; host computes deltas. */
typedef struct __attribute__((packed))
{
    perf_hdr_t hdr;
    uint32_t   dropped_state;
    uint32_t   dropped_patch;
    uint32_t   dropped_sink;
    uint32_t   reserved;
} perf_rec_drop_t;

/* PERF_REC_TASK_HIGHWATER — periodic stack high-water dump. task_id is
 * a stable enum mirrored in the host decoder; words is the
 * uxTaskGetStackHighWaterMark return (StackType_t units). */
typedef enum
{
    PERF_TASK_VIDEO          = 0,
    PERF_TASK_CV_MARVIN_V1   = 1,
    PERF_TASK_DETECTOR_DRAIN = 2,
    PERF_TASK_TIMING         = 3,
    PERF_TASK_FRETBOARD_LINK = 4,
    PERF_TASK_PERF_DRAIN     = 5,
} perf_task_id_t;

typedef struct __attribute__((packed))
{
    perf_hdr_t hdr;
    uint8_t    task_id;
    uint8_t    reserved[3];
    uint32_t   words;
} perf_rec_task_highwater_t;

/* PERF_REC_PATCH — Tier 2 ROI dump. Two 5×5 BGR patches per fret
 * (hold sensor + edge sensor). Coords are the (hx,hy) / (ex,ey) the
 * detector actually sampled — host can replay the detector math
 * pixel-for-pixel. */
#define PERF_PATCH_DIM        5u    /* matches CV_PATCH_RADIUS = 2 */
#define PERF_PATCH_PIXELS     (PERF_PATCH_DIM * PERF_PATCH_DIM)
#define PERF_PATCH_BYTES      (PERF_PATCH_PIXELS * 3u)   /* 75 B per patch */

typedef struct __attribute__((packed))
{
    uint16_t hx, hy;
    uint16_t ex, ey;
    uint8_t  hold_bgr[PERF_PATCH_BYTES];
    uint8_t  edge_bgr[PERF_PATCH_BYTES];
} perf_patch_fret_t;        /* 8 + 75 + 75 = 158 B */

typedef struct __attribute__((packed))
{
    perf_hdr_t        hdr;
    uint16_t          frame_w;
    uint16_t          frame_h;
    uint32_t          reserved;
    perf_patch_fret_t fret[FRET_COUNT];   /* 5 × 158 = 790 B */
} perf_rec_patch_t;          /* 16 + 8 + 790 = 814 B */

#endif /* PERF_LOG_RECORDS_H */
