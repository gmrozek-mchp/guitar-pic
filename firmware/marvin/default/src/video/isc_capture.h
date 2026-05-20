#ifndef ISC_CAPTURE_H
#define ISC_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void     ISC_Capture_Initialize(void);
bool     ISC_Capture_Configure(uint32_t width, uint32_t height);
bool     ISC_Capture_Start(void);
void     ISC_Capture_Stop(void);
uint32_t ISC_Capture_FrameCount(void);
bool     ISC_Capture_IsRunning(void);

/* Base address of the capture framebuffer pool. Subsequent buffers are at
 * +N × frame_size. Most callers want the per-frame "just completed" address
 * delivered through the frame callback; use this only for one-time setup
 * where any valid buffer in the pool is acceptable. */
uint32_t ISC_Capture_GetBufferAddress(void);

/* Frame-done callback fires from the ISC DMA-done IRQ each time a frame is
 * written to DDR. The buffer_addr argument points at the buffer that just
 * completed (one of the N descriptor-ring slots). Pass NULL cb to unregister.
 * Single subscriber only; intended caller is the video module. Runs in IRQ
 * context. */
typedef void (*ISC_Capture_FrameCallback)(uint32_t frame_count,
                                          uint32_t buffer_addr,
                                          uintptr_t ctx);
void ISC_Capture_SetFrameCallback(ISC_Capture_FrameCallback cb, uintptr_t ctx);

#ifdef __cplusplus
}
#endif

#endif
