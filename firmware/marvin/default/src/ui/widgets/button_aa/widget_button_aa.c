#include "ui/widgets/button_aa/widget_button_aa.h"

#include "ui/gfx/aa_corners.h"

#include "gfx/legato/core/legato_scheme.h"
#include "gfx/legato/renderer/legato_renderer.h"
#include "gfx/legato/widget/legato_widget.h"

/* One shared vtable copy + the captured original paint: every button shares the
 * same classic-skin vtable, so a single overridden copy serves all callers. */
static leButtonWidgetVTable s_aa_vt;
static void (*s_orig_paint)(leButtonWidget*);
static leBool s_vt_ready = LE_FALSE;

/* After the classic skin paints the button (background, border, text), smooth its
 * rounded corners. Fill matches the skin (pressed → BACKGROUND, released → BASE);
 * a LINE border is a 1px ring in SHADOWDARK (the colour the classic line-border
 * draw uses), so the AA reproduces backdrop → border → fill at each corner. The
 * radius is whatever the button was given — AaCorners_Render handles any value. */
static void aa_paint(leButtonWidget* btn)
{
    s_orig_paint(btn);

    if (btn->widget.status.drawState == LE_WIDGET_DRAW_STATE_DONE &&
        btn->widget.style.cornerRadius > 0u)
    {
        leRect   rect;
        leColor  fill;
        leColor  border;
        uint32_t bw;

        btn->fn->rectToScreen(btn, &rect);

        fill = leScheme_GetRenderColor(btn->widget.scheme,
                                       (btn->state != LE_BUTTON_STATE_UP)
                                           ? LE_SCHM_BACKGROUND : LE_SCHM_BASE);

        bw     = (btn->widget.style.borderType == LE_WIDGET_BORDER_LINE) ? 1u : 0u;
        border = (bw != 0u) ? leScheme_GetRenderColor(btn->widget.scheme, LE_SCHM_SHADOWDARK)
                            : fill;

        AaCorners_Render(&rect, btn->widget.style.cornerRadius, bw,
                         fill, border, leRenderer_CurrentColorMode());
    }
}

void ButtonAA_Enable(leButtonWidget* btn)
{
    if (!s_vt_ready)
    {
        s_aa_vt = *btn->fn;
        s_orig_paint = btn->fn->_paint;
        s_aa_vt._paint = aa_paint;
        s_vt_ready = LE_TRUE;
    }

    btn->fn = &s_aa_vt;
    btn->widget.fn = (const leWidgetVTable*)&s_aa_vt;
}
