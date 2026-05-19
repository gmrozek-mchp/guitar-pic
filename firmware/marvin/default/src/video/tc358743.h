#ifndef TC358743_H
#define TC358743_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void TC358743_Initialize(void);
void TC358743_Tasks(void);

bool TC358743_IsLocked(void);
bool TC358743_GetDetectedFormat(uint16_t *width, uint16_t *height);
bool TC358743_EnableStream(bool enable);

#ifdef __cplusplus
}
#endif

#endif
