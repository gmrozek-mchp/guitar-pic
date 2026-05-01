#ifndef DRV_IMAGE_SENSOR_H
#define DRV_IMAGE_SENSOR_H

/* Type stubs retained after the image-sensor driver was removed from MCC.
 * drv_isc.c uses these enum values for its inputFormat / inputBits switch
 * cases, and configuration.h uses them for ISC_INPUT_* defaults. Numeric
 * values match the original enum so encodings derived from them (e.g.
 * drv_isc.c's "bits_per_pixel = 4 - inputBits" formula) stay correct. */

enum {
    DRV_IMAGE_SENSOR_RAW_BAYER = 0,
    DRV_IMAGE_SENSOR_YUV_422   = 1,
    DRV_IMAGE_SENSOR_RGB       = 2,
    DRV_IMAGE_SENSOR_CCIR656   = 3,
    DRV_IMAGE_SENSOR_MONO      = 4,
    DRV_IMAGE_SENSOR_JPEG      = 5,
};

enum {
    DRV_IMAGE_SENSOR_8_BIT  = 0,
    DRV_IMAGE_SENSOR_9_BIT  = 1,
    DRV_IMAGE_SENSOR_10_BIT = 2,
    DRV_IMAGE_SENSOR_11_BIT = 3,
    DRV_IMAGE_SENSOR_12_BIT = 4,
    DRV_IMAGE_SENSOR_40_BIT = 5,
};

#endif
