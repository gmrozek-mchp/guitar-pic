/*
� [2026] Microchip Technology Inc. and its subsidiaries.

    Subject to your compliance with these terms, you may use Microchip 
    software and any derivatives exclusively with Microchip products. 
    You are responsible for complying with 3rd party license terms  
    applicable to your use of 3rd party software (including open source  
    software) that may accompany Microchip software. SOFTWARE IS ?AS IS.? 
    NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS 
    SOFTWARE, INCLUDING ANY IMPLIED WARRANTIES OF NON-INFRINGEMENT,  
    MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT 
    WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, 
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY 
    KIND WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF 
    MICROCHIP HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE 
    FORESEEABLE. TO THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP?S 
    TOTAL LIABILITY ON ALL CLAIMS RELATED TO THE SOFTWARE WILL NOT 
    EXCEED AMOUNT OF FEES, IF ANY, YOU PAID DIRECTLY TO MICROCHIP FOR 
    THIS SOFTWARE.
*/
#include "mcc_generated_files/system/system.h"
#include "src/audio.h"
#include "src/beat_engine.h"
#include "src/cli.h"
#include "src/rgb_led.h"
#include "src/t1s_follower.h"
/*
    Main application
*/

/* Onboard RGB LED as a live beat indicator: kick=white, bass=red, mid/high=blue;
   strong beats full brightness, others dimmer. Each new frame decays the prior
   flash, so hits show as a short colored pulse. */
static void beat_indicator(const BeatFrame *f)
{
    static uint8_t r, g, b;

    r -= r >> 2;
    g -= g >> 2;
    b -= b >> 2;

    if (f->kick_beat > 0u)
    {
        r = g = b = (f->kick_beat > 1u) ? 255u : 160u;
    }
    else if (f->bass_beat > 0u)
    {
        r = (f->bass_beat > 1u) ? 255u : 160u;
    }
    else if (f->full_beat > 0u)
    {
        b = (f->full_beat > 1u) ? 255u : 160u;
    }

    RGB_LED_Set(r, g, b);
}

int main(void)
{
    SYSTEM_Initialize();
    RGB_LED_Initialize();
    Audio_Initialize();
    Beat_Initialize();
    CLI_Initialize();
    T1SFollower_Initialize();

    while(1)
    {
        CLI_Tasks();
        Beat_Tasks();
        if (Beat_HasFrame())
        {
            BeatFrame f;
            Beat_GetFrame(&f);
            beat_indicator(&f);
        }
        T1SFollower_Tasks();
    }
}