/*******************************************************************************
  RTC Peripheral Library

  Company:
    Microchip Technology Inc.

  File Name:
    plib_rtc.c

  Summary:
    RTC Source File

  Description:
    None

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

#include "device.h"
#include "plib_rtc.h"
#include "interrupts.h"


__STATIC_INLINE uint32_t decimaltobcd( uint32_t aDecValue )
{
    uint32_t  decValueDiv10 = aDecValue / 10U;

    return( (decValueDiv10 << 4U) + ( aDecValue - (decValueDiv10 * 10U) ) );
}

__STATIC_INLINE uint32_t bcdtodecimal( uint32_t aBcdValue )
{
    return( (10U * ((aBcdValue & 0xF0U) >> 4U)) + (aBcdValue & 0x0FU) );
}

void RTC_Initialize( void )
{
    RTC_REGS->RTC_MR = RTC_MR_OUT1_NO_WAVE | RTC_MR_OUT0_NO_WAVE | RTC_MR_TPERIOD_P_1S | RTC_MR_THIGH_H_31MS | RTC_MR_UTC(0) | RTC_MR_HRMOD(0U);

    RTC_REGS->RTC_CR = RTC_CR_TIMEVSEL_MINUTE | RTC_CR_CALEVSEL_WEEK;

    // disable all interrupts
    RTC_REGS->RTC_IDR = RTC_IDR_Msk;

    // clear any stale interrupts
    RTC_REGS->RTC_SCCR = RTC_SCCR_Msk;
}

bool RTC_TimeSet( struct tm * sysTime )
{
    bool retval = true;
    sysTime->tm_year += 1900;
    uint32_t data_cal = (decimaltobcd((uint32_t)sysTime->tm_mday ) << RTC_CALR_DATE_Pos)
                        | (decimaltobcd(((uint32_t)sysTime->tm_wday + 1U )) << RTC_CALR_DAY_Pos)
                        | (decimaltobcd(((uint32_t)sysTime->tm_mon + 1U )) << RTC_CALR_MONTH_Pos)
                        | (decimaltobcd(((uint32_t)sysTime->tm_year - (((uint32_t)sysTime->tm_year/100U) * 100U ))) << RTC_CALR_YEAR_Pos)
                        | (decimaltobcd(((uint32_t)sysTime->tm_year/100U) ) << RTC_CALR_CENT_Pos);

    uint32_t data_time = (decimaltobcd((uint32_t)sysTime->tm_hour) << RTC_TIMR_HOUR_Pos )
                        | (decimaltobcd((uint32_t)sysTime->tm_min) << RTC_TIMR_MIN_Pos)
                        | (decimaltobcd((uint32_t)sysTime->tm_sec) << RTC_TIMR_SEC_Pos);

    RTC_REGS->RTC_CR &= ~(RTC_CR_UPDCAL_Msk | RTC_CR_UPDTIM_Msk);

    RTC_REGS->RTC_SCCR = RTC_SCCR_CALCLR_Msk;

    while( (RTC_REGS->RTC_SR & RTC_SR_SEC_Msk) != RTC_SR_SEC_Msk )
    {
        ;   // spin lock
    }

    // request RTC Configuration
    RTC_REGS->RTC_CR |= RTC_CR_UPDCAL_Msk | RTC_CR_UPDTIM_Msk;

    // Wait for ack
    while( (RTC_REGS->RTC_SR & RTC_SR_ACKUPD_Msk) == 0U )
    {
        ;   // spin lock
    }

    // clear ack flag
    RTC_REGS->RTC_SCCR |= RTC_SCCR_ACKCLR_Msk | RTC_SCCR_ALRCLR_Msk | RTC_SCCR_TIMCLR_Msk;
    RTC_REGS->RTC_CALR = data_cal;
    RTC_REGS->RTC_TIMR = data_time;
    RTC_REGS->RTC_CR &= ~(RTC_CR_UPDCAL_Msk | RTC_CR_UPDTIM_Msk);

    if( (RTC_REGS->RTC_VER & (RTC_VER_NVTIM_Msk | RTC_VER_NVCAL_Msk)) != 0U )
    {
        retval = false;     // valid entry register indicates a problem
    }

    return retval;
}

void RTC_TimeGet( struct tm * sysTime )
{
    uint32_t temp;
    // two sequential read should be the same to insure syncrhonization
    uint32_t data_time = RTC_REGS->RTC_TIMR;

    while( data_time != RTC_REGS->RTC_TIMR )
    {
        data_time = RTC_REGS->RTC_TIMR;
    }

    // two sequential read should be the same to insure synchronization
    uint32_t data_cal = RTC_REGS->RTC_CALR;

    while( data_cal != RTC_REGS->RTC_CALR )
    {
        data_cal = RTC_REGS->RTC_CALR;
    }
    temp = bcdtodecimal( (data_time & RTC_TIMR_HOUR_Msk) >> RTC_TIMR_HOUR_Pos );
    sysTime->tm_hour = (int32_t)temp;
    temp = bcdtodecimal( data_time & RTC_TIMR_SEC_Msk );
    sysTime->tm_sec = (int32_t)temp;
    temp = bcdtodecimal( (data_time & RTC_TIMR_MIN_Msk) >> RTC_TIMR_MIN_Pos );
    sysTime->tm_min = (int32_t)temp;
    temp = bcdtodecimal( (data_cal & RTC_CALR_DATE_Msk) >> RTC_CALR_DATE_Pos );
    sysTime->tm_mday = (int32_t)temp;
    temp = bcdtodecimal( (data_cal & RTC_CALR_DAY_Msk) >> RTC_CALR_DAY_Pos ) - 1U;
    sysTime->tm_wday = (int32_t)temp;
    temp = bcdtodecimal( (data_cal & RTC_CALR_MONTH_Msk) >> RTC_CALR_MONTH_Pos ) - 1U;
    sysTime->tm_mon = (int32_t)temp;
    temp = (( (100U * bcdtodecimal( data_cal & RTC_CALR_CENT_Msk ))
                        + bcdtodecimal( (data_cal & RTC_CALR_YEAR_Msk) >> RTC_CALR_YEAR_Pos )
                    ) - 1900U);
    sysTime->tm_year = (int32_t)temp;
}

