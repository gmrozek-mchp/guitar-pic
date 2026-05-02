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

/* Base address of the capture framebuffer pool. Returns buffer 0; buffer 1
 * is at +(width * height * 4). ISC DMA alternates between the two. For a
 * tear-tolerant read-only display, pointing at buffer 0 alone is fine. */
uint32_t ISC_Capture_GetBufferAddress(void);

#ifdef __cplusplus
}
#endif

#endif
