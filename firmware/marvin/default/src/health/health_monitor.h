#ifndef MARVIN_HEALTH_MONITOR_H
#define MARVIN_HEALTH_MONITOR_H

#include <stdint.h>

/* Liveness + resource monitor.
 *
 * A low-priority task samples the system on a fixed cadence and records it to
 * the SD card and the DBGU log, so an unattended freeze can be characterized
 * after the fact:
 *
 *  - Heartbeat (HM_HEARTBEAT_MS): one appended line to health/heartbeat.csv,
 *    open/write/close each time so the last line always survives a hang. The
 *    final timestamp bounds when the system stopped; the frame counter shows
 *    whether capture had already stalled; the heap and worst-stack columns
 *    surface a slow leak or a task creeping toward stack overflow.
 *  - Stack dump (HM_STACKDUMP_MS): the full per-task stack + runtime table,
 *    overwritten in health/stacks.txt so it stays small and always current.
 *
 * The task runs just above idle, so it is the first thing starved when a
 * higher-priority task spins — the write cadence stopping is itself the signal.
 * Every sample is mirrored to the DBGU log, which survives even if the SD /
 * FatFs path is the thing that is wedged. */
void HealthMonitor_Initialize(void);

/* Arm the monitor. Both tasks stay completely idle (no card I/O, no task-list
 * walks) until this is called — health-monitor activity during the fragile
 * capture/reveal boot window perturbs it. Call once the UI is revealed and
 * capture is armed (end of the boot sequence). */
void HealthMonitor_NotifyReady(void);

/* Aggregate CPU load over the supervisor's most recent 1 s sample, in permille of one
 * core — everything that is not the idle task, so interrupt time counts as load. 0 until
 * the monitor is armed and has two samples to diff. Safe to call from any task (a single
 * word, published by the supervisor and read without a lock). Drives the titlebar's CPU
 * sparkline; the supervisor computes it anyway for its runaway check. */
uint32_t HealthMonitor_CpuPermille(void);

/* Render the current per-task table (header, one row per task, heap footer)
 * through a caller-supplied line sink. Used by the `health` console command;
 * safe to call from any task. */
typedef void (*health_print_fn)(void *ctx, const char *line);
void HealthMonitor_Report(health_print_fn out, void *ctx);

#endif
