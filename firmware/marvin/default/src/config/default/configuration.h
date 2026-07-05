/*******************************************************************************
  System Configuration Header

  File Name:
    configuration.h

  Summary:
    Build-time configuration header for the system defined by this project.

  Description:
    An MPLAB Project may have multiple configurations.  This file defines the
    build-time options for a single configuration.

  Remarks:
    This configuration header must not define any prototypes or data
    definitions (or include any files that do).  It only provides macro
    definitions for build-time configuration options

*******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
* Copyright (C) 2018 Microchip Technology Inc. and its subsidiaries.
*
* Subject to your compliance with these terms, you may use Microchip software
* and any derivatives exclusively with Microchip products. It is your
* responsibility to comply with third party license terms applicable to your
* use of third party software (including open source software) that may
* accompany Microchip software.
*
* THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
* EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
* WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
* PARTICULAR PURPOSE.
*
* IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
* INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
* WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
* BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
* FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
* ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
* THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
*******************************************************************************/
// DOM-IGNORE-END

#ifndef CONFIGURATION_H
#define CONFIGURATION_H

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************
/*  This section Includes other configuration headers necessary to completely
    define this configuration.
*/

#include "user.h"
#include "device.h"

// DOM-IGNORE-BEGIN
#ifdef __cplusplus  // Provide C++ Compatibility

extern "C" {

#endif
// DOM-IGNORE-END

// *****************************************************************************
// *****************************************************************************
// Section: System Configuration
// *****************************************************************************
// *****************************************************************************



// *****************************************************************************
// *****************************************************************************
// Section: System Service Configuration
// *****************************************************************************
// *****************************************************************************
/* TIME System Service Configuration Options */
#define SYS_TIME_INDEX_0                            (0)
#define SYS_TIME_MAX_TIMERS                         (5)
#define SYS_TIME_HW_COUNTER_WIDTH                   (32)
#define SYS_TIME_HW_COUNTER_PERIOD                  (4294967295U)
#define SYS_TIME_HW_COUNTER_HALF_PERIOD             (SYS_TIME_HW_COUNTER_PERIOD>>1)
#define SYS_TIME_CPU_CLOCK_FREQUENCY                (800000000)
#define SYS_TIME_COMPARE_UPDATE_EXECUTION_CYCLES    (1210)


/* File System Service Configuration */

#define SYS_FS_MEDIA_NUMBER               (1U)
#define SYS_FS_VOLUME_NUMBER              (1U)

#define SYS_FS_AUTOMOUNT_ENABLE           false
#define SYS_FS_MAX_FILES                  (1U)
#define SYS_FS_MAX_FILE_SYSTEM_TYPE       (1U)
#define SYS_FS_MEDIA_MAX_BLOCK_SIZE       (512U)
#define SYS_FS_MEDIA_MANAGER_BUFFER_SIZE  (2048U)
#define SYS_FS_USE_LFN                    (1)
#define SYS_FS_FILE_NAME_LEN              (255U)
#define SYS_FS_CWD_STRING_LEN             (1024)

/* File System RTOS Configurations*/
#define SYS_FS_STACK_SIZE                 1024
#define SYS_FS_PRIORITY                   1

#define SYS_FS_FAT_VERSION                "v0.15"
#define SYS_FS_FAT_READONLY               false
#define SYS_FS_FAT_CODE_PAGE              437
#define SYS_FS_FAT_MAX_SS                 SYS_FS_MEDIA_MAX_BLOCK_SIZE
#define SYS_FS_FAT_ALIGNED_BUFFER_LEN     512








// *****************************************************************************
// *****************************************************************************
// Section: Driver Configuration
// *****************************************************************************
// *****************************************************************************
/* I2C Driver Instance 0 Configuration Options */
#define DRV_I2C_INDEX_0                       0
#define DRV_I2C_CLIENTS_NUMBER_IDX0           2
#define DRV_I2C_CLOCK_SPEED_IDX0              400000

/* I2C Driver Common Configuration Options */
#define DRV_I2C_INSTANCES_NUMBER              (1U)



/*** CSI2DC Driver Configuration ***/
#define CSI2DC_BUS_TYPE		CSI2DC_BUS_CSI2_DPHY
#define CSI2DC_VIDEO_PIPE_FORMAT_TYPE		CSI2DC_DATA_FORMAT_RGB888
#define CSI2DC_VIDEO_PIPE_CHANNEL_ID		0
#define CSI2DC_DATA_PIPE_CHANNEL_ID			0
#define CSI2DC_DATA_PIPE_FORMAT_TYPE		CSI2DC_DATA_FORMAT_RGB888
#define CSI2DC_DATA_PIPE_DMA_CHUCK_SIZE		CSI2DC_DMA_CHUCK_SIZE_16
#define CSI2DC_DATA_PIPE_DMA_COUNT			320
#define CSI2DC_ENABLE_MIPI_CLOCK_FREE_RUN		false
#define CSI2DC_POST_ALIGNED		true
#define CSI2DC_ENABLE_DATA_PIPE		false
#define CSI2DC_DATA_PIPE_ENABLE_DMA	false



/*** MXT336T Driver Configuration ***/
#define DRV_MAXTOUCH_I2C_MODULE_INDEX   0

/* SDMMC Driver Global Configuration Options */
#define DRV_SDMMC_INSTANCES_NUMBER                       (1U)

/* SST26 Driver Instance Configuration */
#define DRV_SST26_INDEX                 (0U)
#define DRV_SST26_CLIENTS_NUMBER        (1U)
#define DRV_SST26_START_ADDRESS         (0x0U)
#define DRV_SST26_PAGE_SIZE             (256U)
#define DRV_SST26_ERASE_BUFFER_SIZE     (4096U)


/*** CSI Driver Configuration ***/
#define CSI_DATA_FORMAT_TYPE		CSI2_DATA_FORMAT_RGB888
#define CSI_NUM_LANES				CSI_DATA_LANES_2


/*** ISC Image Sensor Configuration ***/
#define ISC_INPUT_FORMAT_TYPE			DRV_IMAGE_SENSOR_RGB
#define ISC_INPUT_BIT_WIDTH				DRV_IMAGE_SENSOR_8_BIT
#define ISC_OUTPUT_FORMAT_TYPE			ISC_RLP_CFG_MODE_ARGB32
#define ISC_OUTPUT_LAYOUT_TYPE			ISC_LAYOUT_PACKED32
#define ISC_BAYER_PATTERN_TYPE			ISC_CFA_CFG_BAYCFG_RGRG_Val
#define ISC_ENABLE_DPC			true
#define ISC_ENABLE_BLC			true
#define ISC_ENABLE_GDC			true
#define ISC_DPC_ENABLE_EITPOL		true
#define ISC_DPC_ENABLE_TM			true
#define ISC_DPC_ENABLE_TC			true
#define ISC_DPC_ENABLE_TA			true
#define ISC_DPC_ENABLE_ND_MODE		true
#define ISC_DPC_RE_MODE					ISC_DPC_CFG_RE_MODE_1_Val
#define ISC_DPC_GDCCLP					64
#define ISC_DCP_BLOFST					64
#define ISC_DCP_THRESHM					512
#define ISC_DCP_THRESHC					512
#define ISC_DCP_THRESHA					512
#define ISC_ENABLE_GAMMA				true
#define ISC_GAMMA_RED_ENTRIES			true
#define ISC_GAMMA_BLUE_ENTRIES			true
#define ISC_GAMMA_GREEN_ENTRIES			true
#define ISC_ENABLE_WHITE_BALANCE		true
#define ISC_WB_R_OFFSET					7928
#define ISC_WB_GR_OFFSET				7928
#define ISC_WB_B_OFFSET					7936
#define ISC_WB_GB_OFFSET				7928
#define ISC_WB_R_GAIN					1944
#define ISC_WB_GR_GAIN					1103
#define ISC_WB_B_GAIN					3403
#define ISC_WB_GB_GAIN					1619
#define ISC_ENABLE_HISTOGRAM			false
#define ISC_ENABLE_MIPI_INTERFACE		true
#define ISC_ENABLE_VIDEO_MODE			true
#define ISC_ENABLE_BRIGHTNESS_CONTRAST	true
#define ISC_CBC_BRIGHTNESS_VAL			5
#define ISC_CBC_CONTRAST_VAL			18
#define ISC_CBHS_HUE_VAL				0
#define ISC_CBHS_SATURATION_VAL			32
#define ISC_ENABLE_PROGRESSIVE_MODE		true
#define ISC_ENABLE_SCALING		false
#define ISC_SCALE_OUTPUT_WIDTH				0
#define ISC_SCALE_OUTPUT_HEIGHT				0


/*** ISC PLib Configuration ***/
#define PLIB_ISC_MCK_SEL_VAL		0
#define PLIB_ISC_MCK_DIV_VAL		0
#define ISC_HSYNC_POLARITY_VAL		0
#define ISC_VSYNC_POLARITY_VAL		0


/*** SDMMC Driver Instance 0 Configuration ***/
#define DRV_SDMMC_INDEX_0                                0
#define DRV_SDMMC_IDX0_CLIENTS_NUMBER                    1
#define DRV_SDMMC_IDX0_QUEUE_SIZE                        2
#define DRV_SDMMC_IDX0_PROTOCOL_SUPPORT                  DRV_SDMMC_PROTOCOL_SD
#define DRV_SDMMC_IDX0_CONFIG_SPEED_MODE                 DRV_SDMMC_SPEED_MODE_DEFAULT
#define DRV_SDMMC_IDX0_CONFIG_BUS_WIDTH                  DRV_SDMMC_BUS_WIDTH_4_BIT
#define DRV_SDMMC_IDX0_CARD_DETECTION_METHOD             DRV_SDMMC_CD_METHOD_POLLING

/* SDMMC Driver Instance 0 RTOS Configurations*/
#define DRV_SDMMC_STACK_SIZE_IDX0                         1024
#define DRV_SDMMC_PRIORITY_IDX0                           1
#define DRV_SDMMC_RTOS_DELAY_IDX0                         1U




// *****************************************************************************
// *****************************************************************************
// Section: Middleware & Other Library Configuration
// *****************************************************************************
// *****************************************************************************
/* Number of Endpoints used */
#define DRV_USB_UDPHS_ENDPOINTS_NUMBER                    4U

/* The USB Device Layer will not initialize the USB Driver */
#define USB_DEVICE_DRIVER_INITIALIZE_EXPLICIT

/* Maximum device layer instances */
#define USB_DEVICE_INSTANCES_NUMBER                         1U

/* EP0 size in bytes */
#define USB_DEVICE_EP0_BUFFER_SIZE                          64U


/* Maximum instances of CDC function driver */
#define USB_DEVICE_CDC_INSTANCES_NUMBER                     1U


/* CDC Transfer Queue Size for both read and
   write. Applicable to all instances of the
   function driver */
#define USB_DEVICE_CDC_QUEUE_DEPTH_COMBINED                 5U

/*** USB Driver Configuration ***/

/* Maximum USB driver instances */
#define DRV_USB_UDPHS_INSTANCES_NUMBER                        1U

#ifndef USB_ALIGN
#define USB_ALIGN __ALIGNED(4096)
#endif 

/* Set maximum size for a DMA transfer, multiple of 64KB */
#define DRV_USB_UDPHS_DMA_MAX_TRANSFER_SIZE                 2




// *****************************************************************************
// *****************************************************************************
// Section: Application Configuration
// *****************************************************************************
// *****************************************************************************


//DOM-IGNORE-BEGIN
#ifdef __cplusplus
}
#endif
//DOM-IGNORE-END

#endif // CONFIGURATION_H
/*******************************************************************************
 End of File
*/
