/*******************************************************************************
  PIO PLIB

  Company:
    Microchip Technology Inc.

  File Name:
    plib_pio.h

  Summary:
    PIO PLIB Header File

  Description:
    This library provides an interface to control and interact with Parallel
    Input/Output controller (PIO) module.

*******************************************************************************/

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

#ifndef PLIB_PIO_H
#define PLIB_PIO_H

#include "device.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// DOM-IGNORE-BEGIN
#ifdef __cplusplus  // Provide C++ Compatibility

    extern "C" {

#endif
// DOM-IGNORE-END

// *****************************************************************************
// *****************************************************************************
// Section: Data types and constants
// *****************************************************************************
// *****************************************************************************


/*** Macros for AC69T88A_ENABLE pin ***/
#define AC69T88A_ENABLE_Set()               (PIOC_REGS->PIO_SODR = ((uint32_t)1U<<30U))
#define AC69T88A_ENABLE_Clear()             (PIOC_REGS->PIO_CODR = ((uint32_t)1U<<30U))
#define AC69T88A_ENABLE_Toggle()            (PIOC_REGS->PIO_ODSR ^= ((uint32_t)1U<<30U))
#define AC69T88A_ENABLE_OutputEnable()      (PIOC_REGS->PIO_OER = ((uint32_t)1U<<30U))
#define AC69T88A_ENABLE_InputEnable()       (PIOC_REGS->PIO_ODR = ((uint32_t)1U<<30U))
#define AC69T88A_ENABLE_Get()               ((PIOC_REGS->PIO_PDSR >> 30U) & 0x1U)
#define AC69T88A_ENABLE_PIN                  PIO_PIN_PC30

/*** Macros for LCD_LVDS_D0_N pin ***/
#define LCD_LVDS_D0_N_Get()               ((PIOC_REGS->PIO_PDSR >> 2U) & 0x1U)
#define LCD_LVDS_D0_N_PIN                  PIO_PIN_PC2

/*** Macros for LCD_LVDS_D1_N pin ***/
#define LCD_LVDS_D1_N_Get()               ((PIOC_REGS->PIO_PDSR >> 4U) & 0x1U)
#define LCD_LVDS_D1_N_PIN                  PIO_PIN_PC4

/*** Macros for LCD_LVDS_D2_N pin ***/
#define LCD_LVDS_D2_N_Get()               ((PIOC_REGS->PIO_PDSR >> 6U) & 0x1U)
#define LCD_LVDS_D2_N_PIN                  PIO_PIN_PC6

/*** Macros for LCD_LVDS_CK_N pin ***/
#define LCD_LVDS_CK_N_Get()               ((PIOC_REGS->PIO_PDSR >> 10U) & 0x1U)
#define LCD_LVDS_CK_N_PIN                  PIO_PIN_PC10

/*** Macros for LCD_LVDS_D3_N pin ***/
#define LCD_LVDS_D3_N_Get()               ((PIOC_REGS->PIO_PDSR >> 12U) & 0x1U)
#define LCD_LVDS_D3_N_PIN                  PIO_PIN_PC12

/*** Macros for QSPI_SCK pin ***/
#define QSPI_SCK_Get()               ((PIOB_REGS->PIO_PDSR >> 19U) & 0x1U)
#define QSPI_SCK_PIN                  PIO_PIN_PB19

/*** Macros for LED_8 pin ***/
#define LED_8_Set()               (PIOB_REGS->PIO_SODR = ((uint32_t)1U<<17U))
#define LED_8_Clear()             (PIOB_REGS->PIO_CODR = ((uint32_t)1U<<17U))
#define LED_8_Toggle()            (PIOB_REGS->PIO_ODSR ^= ((uint32_t)1U<<17U))
#define LED_8_OutputEnable()      (PIOB_REGS->PIO_OER = ((uint32_t)1U<<17U))
#define LED_8_InputEnable()       (PIOB_REGS->PIO_ODR = ((uint32_t)1U<<17U))
#define LED_8_Get()               ((PIOB_REGS->PIO_PDSR >> 17U) & 0x1U)
#define LED_8_PIN                  PIO_PIN_PB17

/*** Macros for LED_5 pin ***/
#define LED_5_Set()               (PIOC_REGS->PIO_SODR = ((uint32_t)1U<<28U))
#define LED_5_Clear()             (PIOC_REGS->PIO_CODR = ((uint32_t)1U<<28U))
#define LED_5_Toggle()            (PIOC_REGS->PIO_ODSR ^= ((uint32_t)1U<<28U))
#define LED_5_OutputEnable()      (PIOC_REGS->PIO_OER = ((uint32_t)1U<<28U))
#define LED_5_InputEnable()       (PIOC_REGS->PIO_ODR = ((uint32_t)1U<<28U))
#define LED_5_Get()               ((PIOC_REGS->PIO_PDSR >> 28U) & 0x1U)
#define LED_5_PIN                  PIO_PIN_PC28

/*** Macros for LCD_LVDS_D0_P pin ***/
#define LCD_LVDS_D0_P_Get()               ((PIOC_REGS->PIO_PDSR >> 3U) & 0x1U)
#define LCD_LVDS_D0_P_PIN                  PIO_PIN_PC3

/*** Macros for LCD_LVDS_D1_P pin ***/
#define LCD_LVDS_D1_P_Get()               ((PIOC_REGS->PIO_PDSR >> 5U) & 0x1U)
#define LCD_LVDS_D1_P_PIN                  PIO_PIN_PC5

/*** Macros for LCD_LVDS_D2_P pin ***/
#define LCD_LVDS_D2_P_Get()               ((PIOC_REGS->PIO_PDSR >> 7U) & 0x1U)
#define LCD_LVDS_D2_P_PIN                  PIO_PIN_PC7

/*** Macros for LCD_LVDS_CK_P pin ***/
#define LCD_LVDS_CK_P_Get()               ((PIOC_REGS->PIO_PDSR >> 11U) & 0x1U)
#define LCD_LVDS_CK_P_PIN                  PIO_PIN_PC11

/*** Macros for LCD_LVDS_D3_P pin ***/
#define LCD_LVDS_D3_P_Get()               ((PIOC_REGS->PIO_PDSR >> 13U) & 0x1U)
#define LCD_LVDS_D3_P_PIN                  PIO_PIN_PC13

/*** Macros for QSPI_IO0 pin ***/
#define QSPI_IO0_Get()               ((PIOB_REGS->PIO_PDSR >> 21U) & 0x1U)
#define QSPI_IO0_PIN                  PIO_PIN_PB21

/*** Macros for QSPI_IO3 pin ***/
#define QSPI_IO3_Get()               ((PIOB_REGS->PIO_PDSR >> 24U) & 0x1U)
#define QSPI_IO3_PIN                  PIO_PIN_PB24

/*** Macros for QSPI_CS pin ***/
#define QSPI_CS_Get()               ((PIOB_REGS->PIO_PDSR >> 20U) & 0x1U)
#define QSPI_CS_PIN                  PIO_PIN_PB20

/*** Macros for LED_6 pin ***/
#define LED_6_Set()               (PIOC_REGS->PIO_SODR = ((uint32_t)1U<<27U))
#define LED_6_Clear()             (PIOC_REGS->PIO_CODR = ((uint32_t)1U<<27U))
#define LED_6_Toggle()            (PIOC_REGS->PIO_ODSR ^= ((uint32_t)1U<<27U))
#define LED_6_OutputEnable()      (PIOC_REGS->PIO_OER = ((uint32_t)1U<<27U))
#define LED_6_InputEnable()       (PIOC_REGS->PIO_ODR = ((uint32_t)1U<<27U))
#define LED_6_Get()               ((PIOC_REGS->PIO_PDSR >> 27U) & 0x1U)
#define LED_6_PIN                  PIO_PIN_PC27

/*** Macros for QSPI_IO2 pin ***/
#define QSPI_IO2_Get()               ((PIOB_REGS->PIO_PDSR >> 23U) & 0x1U)
#define QSPI_IO2_PIN                  PIO_PIN_PB23

/*** Macros for LED_BLUE pin ***/
#define LED_BLUE_Set()               (PIOC_REGS->PIO_SODR = ((uint32_t)1U<<20U))
#define LED_BLUE_Clear()             (PIOC_REGS->PIO_CODR = ((uint32_t)1U<<20U))
#define LED_BLUE_Toggle()            (PIOC_REGS->PIO_ODSR ^= ((uint32_t)1U<<20U))
#define LED_BLUE_OutputEnable()      (PIOC_REGS->PIO_OER = ((uint32_t)1U<<20U))
#define LED_BLUE_InputEnable()       (PIOC_REGS->PIO_ODR = ((uint32_t)1U<<20U))
#define LED_BLUE_Get()               ((PIOC_REGS->PIO_PDSR >> 20U) & 0x1U)
#define LED_BLUE_PIN                  PIO_PIN_PC20

/*** Macros for LED_GREEN pin ***/
#define LED_GREEN_Set()               (PIOC_REGS->PIO_SODR = ((uint32_t)1U<<21U))
#define LED_GREEN_Clear()             (PIOC_REGS->PIO_CODR = ((uint32_t)1U<<21U))
#define LED_GREEN_Toggle()            (PIOC_REGS->PIO_ODSR ^= ((uint32_t)1U<<21U))
#define LED_GREEN_OutputEnable()      (PIOC_REGS->PIO_OER = ((uint32_t)1U<<21U))
#define LED_GREEN_InputEnable()       (PIOC_REGS->PIO_ODR = ((uint32_t)1U<<21U))
#define LED_GREEN_Get()               ((PIOC_REGS->PIO_PDSR >> 21U) & 0x1U)
#define LED_GREEN_PIN                  PIO_PIN_PC21

/*** Macros for LED_7 pin ***/
#define LED_7_Set()               (PIOC_REGS->PIO_SODR = ((uint32_t)1U<<29U))
#define LED_7_Clear()             (PIOC_REGS->PIO_CODR = ((uint32_t)1U<<29U))
#define LED_7_Toggle()            (PIOC_REGS->PIO_ODSR ^= ((uint32_t)1U<<29U))
#define LED_7_OutputEnable()      (PIOC_REGS->PIO_OER = ((uint32_t)1U<<29U))
#define LED_7_InputEnable()       (PIOC_REGS->PIO_ODR = ((uint32_t)1U<<29U))
#define LED_7_Get()               ((PIOC_REGS->PIO_PDSR >> 29U) & 0x1U)
#define LED_7_PIN                  PIO_PIN_PC29

/*** Macros for QSPI_IO1 pin ***/
#define QSPI_IO1_Get()               ((PIOB_REGS->PIO_PDSR >> 22U) & 0x1U)
#define QSPI_IO1_PIN                  PIO_PIN_PB22

/*** Macros for NAND_D4 pin ***/
#define NAND_D4_Get()               ((PIOD_REGS->PIO_PDSR >> 10U) & 0x1U)
#define NAND_D4_PIN                  PIO_PIN_PD10

/*** Macros for AC69T88A_BACKLIGHT_EN pin ***/
#define AC69T88A_BACKLIGHT_EN_Set()               (PIOC_REGS->PIO_SODR = ((uint32_t)1U<<18U))
#define AC69T88A_BACKLIGHT_EN_Clear()             (PIOC_REGS->PIO_CODR = ((uint32_t)1U<<18U))
#define AC69T88A_BACKLIGHT_EN_Toggle()            (PIOC_REGS->PIO_ODSR ^= ((uint32_t)1U<<18U))
#define AC69T88A_BACKLIGHT_EN_OutputEnable()      (PIOC_REGS->PIO_OER = ((uint32_t)1U<<18U))
#define AC69T88A_BACKLIGHT_EN_InputEnable()       (PIOC_REGS->PIO_ODR = ((uint32_t)1U<<18U))
#define AC69T88A_BACKLIGHT_EN_Get()               ((PIOC_REGS->PIO_PDSR >> 18U) & 0x1U)
#define AC69T88A_BACKLIGHT_EN_PIN                  PIO_PIN_PC18

/*** Macros for LED_4 pin ***/
#define LED_4_Set()               (PIOC_REGS->PIO_SODR = ((uint32_t)1U<<15U))
#define LED_4_Clear()             (PIOC_REGS->PIO_CODR = ((uint32_t)1U<<15U))
#define LED_4_Toggle()            (PIOC_REGS->PIO_ODSR ^= ((uint32_t)1U<<15U))
#define LED_4_OutputEnable()      (PIOC_REGS->PIO_OER = ((uint32_t)1U<<15U))
#define LED_4_InputEnable()       (PIOC_REGS->PIO_ODR = ((uint32_t)1U<<15U))
#define LED_4_Get()               ((PIOC_REGS->PIO_PDSR >> 15U) & 0x1U)
#define LED_4_PIN                  PIO_PIN_PC15

/*** Macros for NAND_RDY pin ***/
#define NAND_RDY_Get()               ((PIOD_REGS->PIO_PDSR >> 14U) & 0x1U)
#define NAND_RDY_PIN                  PIO_PIN_PD14

/*** Macros for LED_RED pin ***/
#define LED_RED_Set()               (PIOC_REGS->PIO_SODR = ((uint32_t)1U<<14U))
#define LED_RED_Clear()             (PIOC_REGS->PIO_CODR = ((uint32_t)1U<<14U))
#define LED_RED_Toggle()            (PIOC_REGS->PIO_ODSR ^= ((uint32_t)1U<<14U))
#define LED_RED_OutputEnable()      (PIOC_REGS->PIO_OER = ((uint32_t)1U<<14U))
#define LED_RED_InputEnable()       (PIOC_REGS->PIO_ODR = ((uint32_t)1U<<14U))
#define LED_RED_Get()               ((PIOC_REGS->PIO_PDSR >> 14U) & 0x1U)
#define LED_RED_PIN                  PIO_PIN_PC14

/*** Macros for USB_VBUS_SENSE pin ***/
#define USB_VBUS_SENSE_Set()               (PIOC_REGS->PIO_SODR = ((uint32_t)1U<<8U))
#define USB_VBUS_SENSE_Clear()             (PIOC_REGS->PIO_CODR = ((uint32_t)1U<<8U))
#define USB_VBUS_SENSE_Toggle()            (PIOC_REGS->PIO_ODSR ^= ((uint32_t)1U<<8U))
#define USB_VBUS_SENSE_OutputEnable()      (PIOC_REGS->PIO_OER = ((uint32_t)1U<<8U))
#define USB_VBUS_SENSE_InputEnable()       (PIOC_REGS->PIO_ODR = ((uint32_t)1U<<8U))
#define USB_VBUS_SENSE_Get()               ((PIOC_REGS->PIO_PDSR >> 8U) & 0x1U)
#define USB_VBUS_SENSE_PIN                  PIO_PIN_PC8

/*** Macros for NAND_CLE pin ***/
#define NAND_CLE_Get()               ((PIOD_REGS->PIO_PDSR >> 3U) & 0x1U)
#define NAND_CLE_PIN                  PIO_PIN_PD3

/*** Macros for LED_2 pin ***/
#define LED_2_Set()               (PIOB_REGS->PIO_SODR = ((uint32_t)1U<<0U))
#define LED_2_Clear()             (PIOB_REGS->PIO_CODR = ((uint32_t)1U<<0U))
#define LED_2_Toggle()            (PIOB_REGS->PIO_ODSR ^= ((uint32_t)1U<<0U))
#define LED_2_OutputEnable()      (PIOB_REGS->PIO_OER = ((uint32_t)1U<<0U))
#define LED_2_InputEnable()       (PIOB_REGS->PIO_ODR = ((uint32_t)1U<<0U))
#define LED_2_Get()               ((PIOB_REGS->PIO_PDSR >> 0U) & 0x1U)
#define LED_2_PIN                  PIO_PIN_PB0

/*** Macros for BUTTON_5 pin ***/
#define BUTTON_5_Set()               (PIOA_REGS->PIO_SODR = ((uint32_t)1U<<8U))
#define BUTTON_5_Clear()             (PIOA_REGS->PIO_CODR = ((uint32_t)1U<<8U))
#define BUTTON_5_Toggle()            (PIOA_REGS->PIO_ODSR ^= ((uint32_t)1U<<8U))
#define BUTTON_5_OutputEnable()      (PIOA_REGS->PIO_OER = ((uint32_t)1U<<8U))
#define BUTTON_5_InputEnable()       (PIOA_REGS->PIO_ODR = ((uint32_t)1U<<8U))
#define BUTTON_5_Get()               ((PIOA_REGS->PIO_PDSR >> 8U) & 0x1U)
#define BUTTON_5_PIN                  PIO_PIN_PA8

/*** Macros for NAND_WE pin ***/
#define NAND_WE_Get()               ((PIOD_REGS->PIO_PDSR >> 1U) & 0x1U)
#define NAND_WE_PIN                  PIO_PIN_PD1

/*** Macros for NAND_D2 pin ***/
#define NAND_D2_Get()               ((PIOD_REGS->PIO_PDSR >> 8U) & 0x1U)
#define NAND_D2_PIN                  PIO_PIN_PD8

/*** Macros for BUTTON_6 pin ***/
#define BUTTON_6_Set()               (PIOB_REGS->PIO_SODR = ((uint32_t)1U<<2U))
#define BUTTON_6_Clear()             (PIOB_REGS->PIO_CODR = ((uint32_t)1U<<2U))
#define BUTTON_6_Toggle()            (PIOB_REGS->PIO_ODSR ^= ((uint32_t)1U<<2U))
#define BUTTON_6_OutputEnable()      (PIOB_REGS->PIO_OER = ((uint32_t)1U<<2U))
#define BUTTON_6_InputEnable()       (PIOB_REGS->PIO_ODR = ((uint32_t)1U<<2U))
#define BUTTON_6_Get()               ((PIOB_REGS->PIO_PDSR >> 2U) & 0x1U)
#define BUTTON_6_PIN                  PIO_PIN_PB2

/*** Macros for SDMMC0_DAT0 pin ***/
#define SDMMC0_DAT0_Get()               ((PIOA_REGS->PIO_PDSR >> 0U) & 0x1U)
#define SDMMC0_DAT0_PIN                  PIO_PIN_PA0

/*** Macros for LCD_MIPI_SDA pin ***/
#define LCD_MIPI_SDA_Get()               ((PIOB_REGS->PIO_PDSR >> 4U) & 0x1U)
#define LCD_MIPI_SDA_PIN                  PIO_PIN_PB4

/*** Macros for SDMMC0_DAT2 pin ***/
#define SDMMC0_DAT2_Get()               ((PIOA_REGS->PIO_PDSR >> 4U) & 0x1U)
#define SDMMC0_DAT2_PIN                  PIO_PIN_PA4

/*** Macros for NAND_D6 pin ***/
#define NAND_D6_Get()               ((PIOD_REGS->PIO_PDSR >> 12U) & 0x1U)
#define NAND_D6_PIN                  PIO_PIN_PD12

/*** Macros for SDMMC0_CK pin ***/
#define SDMMC0_CK_Get()               ((PIOA_REGS->PIO_PDSR >> 2U) & 0x1U)
#define SDMMC0_CK_PIN                  PIO_PIN_PA2

/*** Macros for SDMMC0_DAT3 pin ***/
#define SDMMC0_DAT3_Get()               ((PIOA_REGS->PIO_PDSR >> 5U) & 0x1U)
#define SDMMC0_DAT3_PIN                  PIO_PIN_PA5

/*** Macros for BUTTON_2 pin ***/
#define BUTTON_2_Set()               (PIOB_REGS->PIO_SODR = ((uint32_t)1U<<1U))
#define BUTTON_2_Clear()             (PIOB_REGS->PIO_CODR = ((uint32_t)1U<<1U))
#define BUTTON_2_Toggle()            (PIOB_REGS->PIO_ODSR ^= ((uint32_t)1U<<1U))
#define BUTTON_2_OutputEnable()      (PIOB_REGS->PIO_OER = ((uint32_t)1U<<1U))
#define BUTTON_2_InputEnable()       (PIOB_REGS->PIO_ODR = ((uint32_t)1U<<1U))
#define BUTTON_2_Get()               ((PIOB_REGS->PIO_PDSR >> 1U) & 0x1U)
#define BUTTON_2_PIN                  PIO_PIN_PB1

/*** Macros for BUTTON_3 pin ***/
#define BUTTON_3_Set()               (PIOA_REGS->PIO_SODR = ((uint32_t)1U<<6U))
#define BUTTON_3_Clear()             (PIOA_REGS->PIO_CODR = ((uint32_t)1U<<6U))
#define BUTTON_3_Toggle()            (PIOA_REGS->PIO_ODSR ^= ((uint32_t)1U<<6U))
#define BUTTON_3_OutputEnable()      (PIOA_REGS->PIO_OER = ((uint32_t)1U<<6U))
#define BUTTON_3_InputEnable()       (PIOA_REGS->PIO_ODR = ((uint32_t)1U<<6U))
#define BUTTON_3_Get()               ((PIOA_REGS->PIO_PDSR >> 6U) & 0x1U)
#define BUTTON_3_PIN                  PIO_PIN_PA6

/*** Macros for DBGU_RX pin ***/
#define DBGU_RX_Get()               ((PIOA_REGS->PIO_PDSR >> 26U) & 0x1U)
#define DBGU_RX_PIN                  PIO_PIN_PA26

/*** Macros for NAND_RE pin ***/
#define NAND_RE_Get()               ((PIOD_REGS->PIO_PDSR >> 0U) & 0x1U)
#define NAND_RE_PIN                  PIO_PIN_PD0

/*** Macros for NAND_D5 pin ***/
#define NAND_D5_Get()               ((PIOD_REGS->PIO_PDSR >> 11U) & 0x1U)
#define NAND_D5_PIN                  PIO_PIN_PD11

/*** Macros for LCD_MIPI_SCL pin ***/
#define LCD_MIPI_SCL_Get()               ((PIOB_REGS->PIO_PDSR >> 5U) & 0x1U)
#define LCD_MIPI_SCL_PIN                  PIO_PIN_PB5

/*** Macros for BUTTON_7 pin ***/
#define BUTTON_7_Set()               (PIOB_REGS->PIO_SODR = ((uint32_t)1U<<13U))
#define BUTTON_7_Clear()             (PIOB_REGS->PIO_CODR = ((uint32_t)1U<<13U))
#define BUTTON_7_Toggle()            (PIOB_REGS->PIO_ODSR ^= ((uint32_t)1U<<13U))
#define BUTTON_7_OutputEnable()      (PIOB_REGS->PIO_OER = ((uint32_t)1U<<13U))
#define BUTTON_7_InputEnable()       (PIOB_REGS->PIO_ODR = ((uint32_t)1U<<13U))
#define BUTTON_7_Get()               ((PIOB_REGS->PIO_PDSR >> 13U) & 0x1U)
#define BUTTON_7_PIN                  PIO_PIN_PB13

/*** Macros for CLASSD_L1 pin ***/
#define CLASSD_L1_Get()               ((PIOA_REGS->PIO_PDSR >> 19U) & 0x1U)
#define CLASSD_L1_PIN                  PIO_PIN_PA19

/*** Macros for NAND_D7 pin ***/
#define NAND_D7_Get()               ((PIOD_REGS->PIO_PDSR >> 13U) & 0x1U)
#define NAND_D7_PIN                  PIO_PIN_PD13

/*** Macros for NAND_CS pin ***/
#define NAND_CS_Get()               ((PIOD_REGS->PIO_PDSR >> 4U) & 0x1U)
#define NAND_CS_PIN                  PIO_PIN_PD4

/*** Macros for NAND_ALE pin ***/
#define NAND_ALE_Get()               ((PIOD_REGS->PIO_PDSR >> 2U) & 0x1U)
#define NAND_ALE_PIN                  PIO_PIN_PD2

/*** Macros for NAND_D0 pin ***/
#define NAND_D0_Get()               ((PIOD_REGS->PIO_PDSR >> 6U) & 0x1U)
#define NAND_D0_PIN                  PIO_PIN_PD6

/*** Macros for NAND_D1 pin ***/
#define NAND_D1_Get()               ((PIOD_REGS->PIO_PDSR >> 7U) & 0x1U)
#define NAND_D1_PIN                  PIO_PIN_PD7

/*** Macros for SDMMC0_DAT1 pin ***/
#define SDMMC0_DAT1_Get()               ((PIOA_REGS->PIO_PDSR >> 3U) & 0x1U)
#define SDMMC0_DAT1_PIN                  PIO_PIN_PA3

/*** Macros for NAND_D3 pin ***/
#define NAND_D3_Get()               ((PIOD_REGS->PIO_PDSR >> 9U) & 0x1U)
#define NAND_D3_PIN                  PIO_PIN_PD9

/*** Macros for BUTTON_4 pin ***/
#define BUTTON_4_Set()               (PIOB_REGS->PIO_SODR = ((uint32_t)1U<<7U))
#define BUTTON_4_Clear()             (PIOB_REGS->PIO_CODR = ((uint32_t)1U<<7U))
#define BUTTON_4_Toggle()            (PIOB_REGS->PIO_ODSR ^= ((uint32_t)1U<<7U))
#define BUTTON_4_OutputEnable()      (PIOB_REGS->PIO_OER = ((uint32_t)1U<<7U))
#define BUTTON_4_InputEnable()       (PIOB_REGS->PIO_ODR = ((uint32_t)1U<<7U))
#define BUTTON_4_Get()               ((PIOB_REGS->PIO_PDSR >> 7U) & 0x1U)
#define BUTTON_4_PIN                  PIO_PIN_PB7

/*** Macros for BUTTON_8 pin ***/
#define BUTTON_8_Set()               (PIOB_REGS->PIO_SODR = ((uint32_t)1U<<9U))
#define BUTTON_8_Clear()             (PIOB_REGS->PIO_CODR = ((uint32_t)1U<<9U))
#define BUTTON_8_Toggle()            (PIOB_REGS->PIO_ODSR ^= ((uint32_t)1U<<9U))
#define BUTTON_8_OutputEnable()      (PIOB_REGS->PIO_OER = ((uint32_t)1U<<9U))
#define BUTTON_8_InputEnable()       (PIOB_REGS->PIO_ODR = ((uint32_t)1U<<9U))
#define BUTTON_8_Get()               ((PIOB_REGS->PIO_PDSR >> 9U) & 0x1U)
#define BUTTON_8_PIN                  PIO_PIN_PB9

/*** Macros for BUTTON_1 pin ***/
#define BUTTON_1_Set()               (PIOA_REGS->PIO_SODR = ((uint32_t)1U<<21U))
#define BUTTON_1_Clear()             (PIOA_REGS->PIO_CODR = ((uint32_t)1U<<21U))
#define BUTTON_1_Toggle()            (PIOA_REGS->PIO_ODSR ^= ((uint32_t)1U<<21U))
#define BUTTON_1_OutputEnable()      (PIOA_REGS->PIO_OER = ((uint32_t)1U<<21U))
#define BUTTON_1_InputEnable()       (PIOA_REGS->PIO_ODR = ((uint32_t)1U<<21U))
#define BUTTON_1_Get()               ((PIOA_REGS->PIO_PDSR >> 21U) & 0x1U)
#define BUTTON_1_PIN                  PIO_PIN_PA21

/*** Macros for CLASSD_L0 pin ***/
#define CLASSD_L0_Get()               ((PIOA_REGS->PIO_PDSR >> 18U) & 0x1U)
#define CLASSD_L0_PIN                  PIO_PIN_PA18

/*** Macros for DBGU_TX pin ***/
#define DBGU_TX_Get()               ((PIOA_REGS->PIO_PDSR >> 27U) & 0x1U)
#define DBGU_TX_PIN                  PIO_PIN_PA27

/*** Macros for GUITAR_RX pin ***/
#define GUITAR_RX_Get()               ((PIOA_REGS->PIO_PDSR >> 14U) & 0x1U)
#define GUITAR_RX_PIN                  PIO_PIN_PA14

/*** Macros for LED_3 pin ***/
#define LED_3_Set()               (PIOA_REGS->PIO_SODR = ((uint32_t)1U<<7U))
#define LED_3_Clear()             (PIOA_REGS->PIO_CODR = ((uint32_t)1U<<7U))
#define LED_3_Toggle()            (PIOA_REGS->PIO_ODSR ^= ((uint32_t)1U<<7U))
#define LED_3_OutputEnable()      (PIOA_REGS->PIO_OER = ((uint32_t)1U<<7U))
#define LED_3_InputEnable()       (PIOA_REGS->PIO_ODR = ((uint32_t)1U<<7U))
#define LED_3_Get()               ((PIOA_REGS->PIO_PDSR >> 7U) & 0x1U)
#define LED_3_PIN                  PIO_PIN_PA7

/*** Macros for SDMMC0_CMD pin ***/
#define SDMMC0_CMD_Get()               ((PIOA_REGS->PIO_PDSR >> 1U) & 0x1U)
#define SDMMC0_CMD_PIN                  PIO_PIN_PA1

/*** Macros for BSP_MAXTOUCH_CHG pin ***/
#define BSP_MAXTOUCH_CHG_Set()               (PIOA_REGS->PIO_SODR = ((uint32_t)1U<<20U))
#define BSP_MAXTOUCH_CHG_Clear()             (PIOA_REGS->PIO_CODR = ((uint32_t)1U<<20U))
#define BSP_MAXTOUCH_CHG_Toggle()            (PIOA_REGS->PIO_ODSR ^= ((uint32_t)1U<<20U))
#define BSP_MAXTOUCH_CHG_OutputEnable()      (PIOA_REGS->PIO_OER = ((uint32_t)1U<<20U))
#define BSP_MAXTOUCH_CHG_InputEnable()       (PIOA_REGS->PIO_ODR = ((uint32_t)1U<<20U))
#define BSP_MAXTOUCH_CHG_Get()               ((PIOA_REGS->PIO_PDSR >> 20U) & 0x1U)
#define BSP_MAXTOUCH_CHG_PIN                  PIO_PIN_PA20

/*** Macros for SDMMC0_CD pin ***/
#define SDMMC0_CD_Set()               (PIOA_REGS->PIO_SODR = ((uint32_t)1U<<23U))
#define SDMMC0_CD_Clear()             (PIOA_REGS->PIO_CODR = ((uint32_t)1U<<23U))
#define SDMMC0_CD_Toggle()            (PIOA_REGS->PIO_ODSR ^= ((uint32_t)1U<<23U))
#define SDMMC0_CD_OutputEnable()      (PIOA_REGS->PIO_OER = ((uint32_t)1U<<23U))
#define SDMMC0_CD_InputEnable()       (PIOA_REGS->PIO_ODR = ((uint32_t)1U<<23U))
#define SDMMC0_CD_Get()               ((PIOA_REGS->PIO_PDSR >> 23U) & 0x1U)
#define SDMMC0_CD_PIN                  PIO_PIN_PA23

/*** Macros for LED_1 pin ***/
#define LED_1_Set()               (PIOA_REGS->PIO_SODR = ((uint32_t)1U<<22U))
#define LED_1_Clear()             (PIOA_REGS->PIO_CODR = ((uint32_t)1U<<22U))
#define LED_1_Toggle()            (PIOA_REGS->PIO_ODSR ^= ((uint32_t)1U<<22U))
#define LED_1_OutputEnable()      (PIOA_REGS->PIO_OER = ((uint32_t)1U<<22U))
#define LED_1_InputEnable()       (PIOA_REGS->PIO_ODR = ((uint32_t)1U<<22U))
#define LED_1_Get()               ((PIOA_REGS->PIO_PDSR >> 22U) & 0x1U)
#define LED_1_PIN                  PIO_PIN_PA22

/*** Macros for GUITAR_TX pin ***/
#define GUITAR_TX_Get()               ((PIOA_REGS->PIO_PDSR >> 13U) & 0x1U)
#define GUITAR_TX_PIN                  PIO_PIN_PA13


// *****************************************************************************
/* PIO Port

  Summary:
    Identifies the available PIO Ports.

  Description:
    This enumeration identifies the available PIO Ports.

  Remarks:
    The caller should not rely on the specific numbers assigned to any of
    these values as they may change from one processor to the next.

    Not all ports are available on all devices.  Refer to the specific
    device data sheet to determine which ports are supported.
*/


#define    PIO_PORT_A       (PIOA_BASE_ADDRESS)
#define    PIO_PORT_B       (PIOB_BASE_ADDRESS)
#define     PIO_PORT_C      (PIOC_BASE_ADDRESS)
#define     PIO_PORT_D      (PIOD_BASE_ADDRESS)
typedef uint32_t PIO_PORT;

// *****************************************************************************
/* PIO Port Pins

  Summary:
    Identifies the available PIO port pins.

  Description:
    This enumeration identifies the available PIO port pins.

  Remarks:
    The caller should not rely on the specific numbers assigned to any of
    these values as they may change from one processor to the next.

    Not all pins are available on all devices.  Refer to the specific
    device data sheet to determine which pins are supported.
*/

#define    PIO_PIN_PA0     (0U)
#define    PIO_PIN_PA1     (1U)
#define    PIO_PIN_PA2     (2U)
#define    PIO_PIN_PA3     (3U)
#define    PIO_PIN_PA4     (4U)
#define    PIO_PIN_PA5     (5U)
#define    PIO_PIN_PA6     (6U)
#define    PIO_PIN_PA7     (7U)
#define    PIO_PIN_PA8     (8U)
#define    PIO_PIN_PA9     (9U)
#define    PIO_PIN_PA10     (10U)
#define    PIO_PIN_PA11     (11U)
#define    PIO_PIN_PA12     (12U)
#define    PIO_PIN_PA13     (13U)
#define    PIO_PIN_PA14     (14U)
#define    PIO_PIN_PA15     (15U)
#define    PIO_PIN_PA16     (16U)
#define    PIO_PIN_PA17     (17U)
#define    PIO_PIN_PA18     (18U)
#define    PIO_PIN_PA19     (19U)
#define    PIO_PIN_PA20     (20U)
#define    PIO_PIN_PA21     (21U)
#define    PIO_PIN_PA22     (22U)
#define    PIO_PIN_PA23     (23U)
#define    PIO_PIN_PA24     (24U)
#define    PIO_PIN_PA25     (25U)
#define    PIO_PIN_PA26     (26U)
#define    PIO_PIN_PA27     (27U)
#define    PIO_PIN_PA28     (28U)
#define    PIO_PIN_PA29     (29U)
#define    PIO_PIN_PA30     (30U)
#define    PIO_PIN_PA31     (31U)
#define    PIO_PIN_PB0     (32U)
#define    PIO_PIN_PB1     (33U)
#define    PIO_PIN_PB2     (34U)
#define    PIO_PIN_PB3     (35U)
#define    PIO_PIN_PB4     (36U)
#define    PIO_PIN_PB5     (37U)
#define    PIO_PIN_PB6     (38U)
#define    PIO_PIN_PB7     (39U)
#define    PIO_PIN_PB8     (40U)
#define    PIO_PIN_PB9     (41U)
#define    PIO_PIN_PB10     (42U)
#define    PIO_PIN_PB11     (43U)
#define    PIO_PIN_PB12     (44U)
#define    PIO_PIN_PB13     (45U)
#define    PIO_PIN_PB14     (46U)
#define    PIO_PIN_PB15     (47U)
#define    PIO_PIN_PB16     (48U)
#define    PIO_PIN_PB17     (49U)
#define    PIO_PIN_PB18     (50U)
#define    PIO_PIN_PB19     (51U)
#define    PIO_PIN_PB20     (52U)
#define    PIO_PIN_PB21     (53U)
#define    PIO_PIN_PB22     (54U)
#define    PIO_PIN_PB23     (55U)
#define    PIO_PIN_PB24     (56U)
#define    PIO_PIN_PB25     (57U)
#define    PIO_PIN_PB26     (58U)
#define    PIO_PIN_PC0     (64U)
#define    PIO_PIN_PC1     (65U)
#define    PIO_PIN_PC2     (66U)
#define    PIO_PIN_PC3     (67U)
#define    PIO_PIN_PC4     (68U)
#define    PIO_PIN_PC5     (69U)
#define    PIO_PIN_PC6     (70U)
#define    PIO_PIN_PC7     (71U)
#define    PIO_PIN_PC8     (72U)
#define    PIO_PIN_PC9     (73U)
#define    PIO_PIN_PC10     (74U)
#define    PIO_PIN_PC11     (75U)
#define    PIO_PIN_PC12     (76U)
#define    PIO_PIN_PC13     (77U)
#define    PIO_PIN_PC14     (78U)
#define    PIO_PIN_PC15     (79U)
#define    PIO_PIN_PC16     (80U)
#define    PIO_PIN_PC17     (81U)
#define    PIO_PIN_PC18     (82U)
#define    PIO_PIN_PC19     (83U)
#define    PIO_PIN_PC20     (84U)
#define    PIO_PIN_PC21     (85U)
#define    PIO_PIN_PC22     (86U)
#define    PIO_PIN_PC23     (87U)
#define    PIO_PIN_PC24     (88U)
#define    PIO_PIN_PC25     (89U)
#define    PIO_PIN_PC26     (90U)
#define    PIO_PIN_PC27     (91U)
#define    PIO_PIN_PC28     (92U)
#define    PIO_PIN_PC29     (93U)
#define    PIO_PIN_PC30     (94U)
#define    PIO_PIN_PC31     (95U)
#define    PIO_PIN_PD0     (96U)
#define    PIO_PIN_PD1     (97U)
#define    PIO_PIN_PD2     (98U)
#define    PIO_PIN_PD3     (99U)
#define    PIO_PIN_PD4     (100U)
#define    PIO_PIN_PD5     (101U)
#define    PIO_PIN_PD6     (102U)
#define    PIO_PIN_PD7     (103U)
#define    PIO_PIN_PD8     (104U)
#define    PIO_PIN_PD9     (105U)
#define    PIO_PIN_PD10     (106U)
#define    PIO_PIN_PD11     (107U)
#define    PIO_PIN_PD12     (108U)
#define    PIO_PIN_PD13     (109U)
#define    PIO_PIN_PD14     (110U)

    /* This element should not be used in any of the PIO APIs.
       It will be used by other modules or application to denote that none of the PIO Pin is used */
#define    PIO_PIN_NONE         ( -1)

typedef uint32_t PIO_PIN;


void PIO_Initialize(void);

// *****************************************************************************
// *****************************************************************************
// Section: PIO Functions which operates on multiple pins of a port
// *****************************************************************************
// *****************************************************************************

uint32_t PIO_PortRead(PIO_PORT port);

void PIO_PortWrite(PIO_PORT port, uint32_t mask, uint32_t value);

uint32_t PIO_PortLatchRead ( PIO_PORT port );

void PIO_PortSet(PIO_PORT port, uint32_t mask);

void PIO_PortClear(PIO_PORT port, uint32_t mask);

void PIO_PortToggle(PIO_PORT port, uint32_t mask);

void PIO_PortInputEnable(PIO_PORT port, uint32_t mask);

void PIO_PortOutputEnable(PIO_PORT port, uint32_t mask);

// *****************************************************************************
// *****************************************************************************
// Section: PIO Functions which operates on one pin at a time
// *****************************************************************************
// *****************************************************************************

static inline void PIO_PinWrite(PIO_PIN pin, bool value)
{
    PIO_PortWrite((PIO_PORT)(PIOA_BASE_ADDRESS + (0x200U * (pin>>5U))), (uint32_t)(0x1) << (pin & 0x1fU), (uint32_t)(value) << (pin & 0x1fU));
}

static inline bool PIO_PinRead(PIO_PIN pin)
{
    return (bool)((PIO_PortRead((PIO_PORT)(PIOA_BASE_ADDRESS + (0x200U * (pin>>5U)))) >> (pin & 0x1FU)) & 0x1U);
}

static inline bool PIO_PinLatchRead(PIO_PIN pin)
{
    return (bool)((PIO_PortLatchRead((PIO_PORT)(PIOA_BASE_ADDRESS + (0x200U * (pin>>5U)))) >> (pin & 0x1FU)) & 0x1U);
}

static inline void PIO_PinToggle(PIO_PIN pin)
{
    PIO_PortToggle((PIO_PORT)(PIOA_BASE_ADDRESS + (0x200U * (pin>>5U))), 0x1UL << (pin & 0x1FU));
}

static inline void PIO_PinSet(PIO_PIN pin)
{
    PIO_PortSet((PIO_PORT)(PIOA_BASE_ADDRESS + (0x200U * (pin>>5))), 0x1UL << (pin & 0x1FU));
}

static inline void PIO_PinClear(PIO_PIN pin)
{
    PIO_PortClear((PIO_PORT)(PIOA_BASE_ADDRESS + (0x200U * (pin>>5U))), 0x1UL << (pin & 0x1FU));
}

static inline void PIO_PinInputEnable(PIO_PIN pin)
{
    PIO_PortInputEnable((PIO_PORT)(PIOA_BASE_ADDRESS + (0x200U * (pin>>5U))), 0x1UL << (pin & 0x1FU));
}

static inline void PIO_PinOutputEnable(PIO_PIN pin)
{
    PIO_PortOutputEnable((PIO_PORT)(PIOA_BASE_ADDRESS + (0x200U * (pin>>5U))), 0x1UL << (pin & 0x1FU));
}


// DOM-IGNORE-BEGIN
#ifdef __cplusplus  // Provide C++ Compatibility

    }

#endif
// DOM-IGNORE-END
#endif // PLIB_PIO_H
