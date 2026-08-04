#include "ui/titlebar.h"

#include "ui/screens/navigation/screen_navigation.h"   /* ScreenNavigation_ToggleDrawer */

#include "gfx/legato/legato.h"
#include "gfx/legato/widget/legato_widget.h"
#include "gfx/legato/widget/button/legato_widget_button.h"
#include "gfx/legato/widget/image/legato_widget_image.h"
#include "gfx/legato/generated/le_gen_assets.h"    /* BUTTON_ICON_HAMBURGER, LOGO_* */
#include "gfx/legato/generated/le_gen_scheme.h"     /* SCHEME_BACKGROUND */

/* Legato quirk: the image widget exposes only its *internal* in-place constructor
 * (`_leImageWidget_Constructor`, external linkage but undeclared in the public
 * header); the public `leImageWidget_Constructor` is declared but never defined,
 * unlike the button/widget/label ones. `leImageWidget_New` uses this same symbol
 * internally, so calling it directly for static allocation is safe. */
extern void _leImageWidget_Constructor(leImageWidget *img);

/* One statically-allocated titlebar instance per base-view screen that uses it
 * (dashboard, wiimotes, bus, …). No Legato pool — widgets live in BSS via the
 * in-place Constructors. Geometry mirrors the MGS-authored dashboard titlebar. */
#define TITLEBAR_MAX  4

typedef struct {
    leWidget       bar;
    leButtonWidget nav;
    leImageWidget  guitar, pic, chip;
} titlebar_t;

static titlebar_t s_bar[TITLEBAR_MAX];
static unsigned   s_n;

static void nav_pressed(leButtonWidget *btn)
{
    (void)btn;
    ScreenNavigation_ToggleDrawer();
}

leWidget *Titlebar_Add(leWidget *parent)
{
    if (parent == NULL || s_n >= TITLEBAR_MAX) { return NULL; }
    titlebar_t *t = &s_bar[s_n++];

    leWidget *bar = &t->bar;
    leWidget_Constructor(bar);
    bar->fn->setPosition(bar, 12, 12);
    bar->fn->setSize(bar, 1256, 53);
    bar->fn->setBackgroundType(bar, LE_WIDGET_BACKGROUND_NONE);
    parent->fn->addChild(parent, bar);

    leButtonWidget *nav = &t->nav;
    leButtonWidget_Constructor(nav);
    nav->fn->setPosition(nav, 1, 4);
    nav->fn->setSize(nav, 40, 40);
    nav->fn->setBackgroundType(nav, LE_WIDGET_BACKGROUND_NONE);
    nav->fn->setBorderType(nav, LE_WIDGET_BORDER_NONE);
    nav->fn->setPressedImage(nav, (leImage *)&BUTTON_ICON_HAMBURGER);
    nav->fn->setReleasedImage(nav, (leImage *)&BUTTON_ICON_HAMBURGER);
    nav->fn->setPressedEventCallback(nav, nav_pressed);
    bar->fn->addChild(bar, (leWidget *)nav);

    leImageWidget *g = &t->guitar;
    _leImageWidget_Constructor(g);
    g->fn->setPosition(g, 52, 1);
    g->fn->setSize(g, 225, 45);
    g->fn->setScheme(g, &SCHEME_BACKGROUND);
    g->fn->setBorderType(g, LE_WIDGET_BORDER_NONE);
    g->fn->setImage(g, (leImage *)&LOGO_GUITAR);
    bar->fn->addChild(bar, (leWidget *)g);

    leImageWidget *p = &t->pic;
    _leImageWidget_Constructor(p);
    p->fn->setPosition(p, 294, 1);
    p->fn->setSize(p, 121, 44);
    p->fn->setScheme(p, &SCHEME_BACKGROUND);
    p->fn->setBorderType(p, LE_WIDGET_BORDER_NONE);
    p->fn->setImage(p, (leImage *)&LOGO_PIC);
    bar->fn->addChild(bar, (leWidget *)p);

    leImageWidget *c = &t->chip;
    _leImageWidget_Constructor(c);
    c->fn->setPosition(c, 1052, 1);
    c->fn->setSize(c, 194, 45);
    c->fn->setBackgroundType(c, LE_WIDGET_BACKGROUND_NONE);
    c->fn->setBorderType(c, LE_WIDGET_BORDER_NONE);
    c->fn->setImage(c, (leImage *)&LOGO_MICROCHIP);
    bar->fn->addChild(bar, (leWidget *)c);

    return bar;
}
