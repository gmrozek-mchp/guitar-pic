#ifndef MARVIN_QSPI_SMOKE_H
#define MARVIN_QSPI_SMOKE_H

#include <stdbool.h>
#include <stdint.h>

/* One-shot QSPI / SST26 NOR bring-up smoke test. Opens the driver, reads the
 * JEDEC ID and geometry, then erases / page-programs / read-back-verifies a
 * scratch page in the LAST erase block (so it never touches the low offsets
 * reserved for the splash blob and UI assets). Each step is logged via
 * LOG_INFO / LOG_ERROR. Returns true only if read-back matches.
 *
 * Call from a task (it yields while polling transfer status). */
bool QspiSmoke_Run(void);

/* Read-throughput benchmark. Reads `mb` MiB (default 4, clamped to the 8 MiB
 * device) from QSPI offset 0 into a non-cached DDR buffer (framebuffer-like
 * destination), timing two paths and logging MB/s for each:
 *   - DRV_SST26_Read (the driver), and
 *   - a direct memcpy from the SMM-mapped XIP region at QSPIMEM_ADDR.
 * Non-destructive (reads only). Re-run after raising SCK. */
void QspiSmoke_Bench(uint32_t mb);

/* Read-integrity stress test. Erases a `kb`-KiB region at the TOP of the device
 * (default 256, away from low offsets reserved for splash/assets), writes a
 * position-dependent pattern (word = f(flash address) — catches misaddressing,
 * not just stuck bits), then reads the whole region back `passes` times (default
 * 4) via BOTH DRV_SST26_Read and XDMAC, counting per-path mismatches. Returns
 * true only if both paths match every word on every pass. Destructive to the
 * top `kb` KiB. Confirms high-speed (100 MHz QSCK) read reliability at volume. */
bool QspiSmoke_Verify(uint32_t kb, uint32_t passes);

#endif
