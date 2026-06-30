#include "ui/widgets/panel_aa/widget_panel_aa.h"

#include "ui/gfx/aa_corners.h"

#include "gfx/legato/core/legato_scheme.h"
#include "gfx/legato/renderer/legato_renderer.h"
#include "gfx/legato/widget/legato_widget.h"

/* One shared vtable copy + the captured original paint. Plain panels all share the
 * base leWidget vtable, so a single overridden copy serves every caller. */
static leWidgetVTable s_aa_vt;
static void (*s_orig_paint)(leWidget*);
static leBool s_vt_ready = LE_FALSE;

/* After the classic skin paints the panel (background fill + optional border),
 * smooth its rounded corners: fill is the panel's BASE colour, a LINE border is a
 * 1px SHADOWDARK ring. Backdrop is sampled from the panel's own corner pixel — the
 * opaque parent behind it. */
static void aa_paint(leWidget* wgt)
{
    s_orig_paint(wgt);

    if (wgt->status.drawState == LE_WIDGET_DRAW_STATE_DONE &&
        wgt->style.cornerRadius > 0u)
    {
        leRect   rect;
        leColor  fill;
        leColor  border;
        uint32_t bw;

        wgt->fn->rectToScreen(wgt, &rect);

        fill   = leScheme_GetRenderColor(wgt->scheme, LE_SCHM_BASE);
        bw     = (wgt->style.borderType == LE_WIDGET_BORDER_LINE) ? 1u : 0u;
        border = (bw != 0u) ? leScheme_GetRenderColor(wgt->scheme, LE_SCHM_SHADOWDARK)
                            : fill;

        AaCorners_Render(&rect, wgt->style.cornerRadius, bw,
                         fill, border, leRenderer_CurrentColorMode());
    }
}

void PanelAA_Enable(leWidget* panel)
{
    if (!s_vt_ready)
    {
        s_aa_vt = *panel->fn;
        s_orig_paint = panel->fn->_paint;
        s_aa_vt._paint = aa_paint;
        s_vt_ready = LE_TRUE;
    }

    panel->fn = &s_aa_vt;
}
