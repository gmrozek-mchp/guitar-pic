#include "ui/screens/splash/screen_splash.h"

#include <stddef.h>

#include "gfx/legato/legato.h"
#include "gfx/legato/widget/legato_widget.h"
#include "gfx/legato/widget/image/legato_widget_image.h"
#include "gfx/legato/image/legato_image.h"
#include "gfx/legato/generated/screen/le_gen_screen_Splash.h"   /* Splash_Panel_0 */

/* The image widget we add onto the MGS Splash panel. Allocated from the Legato
 * widget pool. The Splash screen is persistent (built once, never torn down) and
 * stays attached to its layer after the dashboard is revealed — the overlay is
 * just hidden — so this widget persists too. */
static leImageWidget *s_image;

/* Runtime image descriptor pointing at the loader's in-memory JPEG. Layout
 * mirrors a generated leImage (see le_gen_images.c): the stream descriptor and
 * the pixel buffer both carry the compressed byte length; pixel_count is the
 * decoded pixel count; buffer.mode is the decode output mode (888 for full
 * color — the BASE canvas is 32bpp for the splash and converts on blit). */
static leImage        s_img;

void Splash_AttachImage(void)
{
    if (Splash_Panel_0 == NULL) { return; }   /* screenInit_Splash not called */

    s_image = leImageWidget_New();
    s_image->fn->setPosition(s_image, 0, 0);
    s_image->fn->setSize(s_image, SPLASH_W, SPLASH_H);
    s_image->fn->setBackgroundType(s_image, LE_WIDGET_BACKGROUND_NONE);
    Splash_Panel_0->fn->addChild(Splash_Panel_0, (leWidget *)s_image);
}

bool Splash_SetImageJpeg(void *data, uint32_t len)
{
    if (data == NULL || len == 0u || s_image == NULL) { return false; }

    s_img = (leImage){
        {                                       /* leStreamDescriptor header */
            LE_STREAM_LOCATION_ID_INTERNAL,     /* data is in MCU memory      */
            data,
            len,
        },
        LE_IMAGE_FORMAT_JPEG,
        {                                       /* lePixelBuffer buffer       */
            LE_COLOR_MODE_RGB_888,
            { (int32_t)SPLASH_W, (int32_t)SPLASH_H },
            SPLASH_W * SPLASH_H,                /* pixel_count (decoded)      */
            len,                                /* buffer_length (compressed) */
            data,
            0,
        },
        0,                                      /* image flags */
        { 0 },                                  /* mask (color) */
        NULL,                                   /* alpha mask   */
        NULL,                                   /* palette      */
    };

    return s_image->fn->setImage(s_image, &s_img) == LE_SUCCESS;
}
