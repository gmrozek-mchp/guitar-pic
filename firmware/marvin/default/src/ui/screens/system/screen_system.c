#include "ui/screens/system/screen_system.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "ui/ui_manager.h"   /* CANVAS_SYSTEM, BASE_W, BASE_H */
#include "ui/titlebar.h"
#include "ui/widgets/panel_aa/widget_panel_aa.h"
#include "ui/widgets/button_aa/widget_button_aa.h"
#include "ui/node_art.h"

#include "ui/gfx/ui_surface.h"
#include "gfx/canvas/gfx_canvas_api.h"
#include "gfx/legato/legato.h"
#include "gfx/legato/string/legato_fixedstring.h"
#include "gfx/legato/widget/legato_widget.h"
#include "gfx/legato/widget/label/legato_widget_label.h"
#include "gfx/legato/widget/button/legato_widget_button.h"
#include "gfx/legato/widget/image/legato_widget_image.h"
#include "gfx/legato/generated/le_gen_scheme.h"
#include "gfx/legato/generated/le_gen_assets.h"
#include "gfx/legato/generated/screen/le_gen_screen_Marvin.h"   /* Marvin_PANEL_SYSTEM */
#include "util/legato_utf8.h"

/* System info — a product showcase of the seven boards, built programmatically into
 * the MGS layer-7 panel (Marvin_PANEL_SYSTEM). Two views live on the one surface:
 * an overview grid of node cards, and one detail view repopulated from the tapped
 * node's record. Content is the NODE table below, transcribed from docs/screens/.
 *
 * Both views are built once into their own full-screen container panel, and
 * show_view() is the only thing that switches between them — see the note there.
 *
 * Layout mirrors the mockup's SystemScreen.tsx with Tailwind units resolved to pixels
 * (gap-3 = 12, gap-4 = 16, text-xs/sm/base/lg/2xl/3xl = 12/14/16/18/24; for the corner
 * radii, which do not follow the stock Tailwind scale, see CARD_R).
 *
 * Every label, dot, bar and frame here is marked IGNOREPICK: a Legato button paints
 * its own caption and cannot host children, so a card's text is a sibling drawn over
 * it, and leUtils_PickFromWidget keeps the LAST child whose rect contains the point.
 * Without IGNOREPICK the text would swallow every tap on the card beneath it. */

/* leImageWidget_Constructor is declared but never defined; the in-place constructor
 * leImageWidget_New uses internally has external linkage but no public declaration. */
extern void _leImageWidget_Constructor(leImageWidget *img);

#define FB_NOCACHE   __attribute__((section(".region_nocache"), aligned (32)))
static uint16_t FB_NOCACHE s_fb[BASE_W * BASE_H];

/* ── content ────────────────────────────────────────────────────────────────
 * Prose is stored hand-wrapped, one string per rendered line, because Legato labels
 * do not wrap. Lines are sized to the column they are drawn in (see PROSE_COLS) and
 * are pure ASCII: the fonts declare 0x20-0x7E plus Latin-1, so the source specs' em
 * dashes and curly apostrophes would not render. `«`, `»` and `·` used as glyphs
 * below ARE in the declared Latin-1 range, so they are safe. */

#define PROSE_MAX   8u
#define BUILT_MAX   3u
#define NODE_N      7u

typedef struct {
    uint8_t         id;             /* real T1S PLCA id (see net/t1s/t1s_link.c) */
    const char     *name;
    const char     *part;           /* NULL = not a Microchip part; see fauxmote */
    const char     *chip;           /* the networking part, or NULL */
    const char     *tag[2];         /* card tagline, wrapped to the narrow column */
    const char     *tagline;        /* detail header, one line */
    const leScheme *accent;
    bool            this_device;
    const char     *prose[PROSE_MAX];
    const char     *built[BUILT_MAX];
} node_info_t;

/* Indexed in bus order (id 0,1,3,4,5,6,7) so the BUS POSITION rail reads naturally;
 * the overview grid draws them in the mockup's visual order (see GRID_ROW1/2).
 *
 * fauxmote deliberately carries no part number: it is the one board here whose MCU
 * isn't Microchip, and docs/screens/02-fauxmote.md asks for the card to stay a notch
 * quieter rather than naming the hardware it happens to run on. It does name LAN8651,
 * because its T1S PHY is the same Microchip part as every other node's. */
static const node_info_t NODE[NODE_N] = {
    {
        .id = 0u, .name = "marvin", .part = "SAM9X75", .chip = "LAN8651",
        .tag = { "The brain - watches the", "game and calls the shots" },
        .tagline = "The brain - watches the game and calls the shots",
        .accent = &SCHEME_NODE_MARVIN, .this_device = true,
        .prose = {
            "marvin is the one running this screen right now, and the",
            "brains of the whole operation. It watches the game over HDMI,",
            "recognizes notes in real time, decides exactly when to press",
            "each button, and coordinates every other board in the system.",
            "It's also driving the 10.1-inch touchscreen display you're",
            "looking at.",
        },
        .built = {
            "MPLAB X IDE",
            "Curiosity development platform",
            "10.1-inch touchscreen driven directly by the SAM9X75",
        },
    },
    {
        .id = 1u, .name = "fauxmote", .part = NULL, .chip = "LAN8651",
        .tag = { "An alternate way to talk", "to the game console" },
        .tagline = "An alternate way to talk to the game console",
        .accent = &SCHEME_NODE_FAUXMOTE,
        .prose = {
            "fauxmote explores a different way to play: instead of",
            "physically pressing buttons on a real guitar controller, it",
            "wirelessly pretends to be one, talking straight to the game",
            "console over Bluetooth. It's a side experiment alongside the",
            "main physical approach.",
        },
        .built = { "Bluetooth Classic wireless link" },
    },
    {
        .id = 3u, .name = "guitar", .part = "PIC32CM PL10", .chip = "LAN8651",
        .tag = { "The hands - presses the", "buttons in perfect time" },
        .tagline = "The hands - presses the buttons in perfect time",
        .accent = &SCHEME_NODE_GUITAR,
        .prose = {
            "This board is the one that actually presses the buttons -",
            "receiving marvin's call and instantly lighting up the right",
            "frets and strum, in perfect time with the music. A small,",
            "dedicated board with one job, done fast and reliably.",
        },
        .built = {
            "MPLAB X IDE",
            "MCC (MPLAB Code Configurator)",
            "Microchip LAN8651 networking",
        },
    },
    {
        .id = 4u, .name = "fretboard", .part = "PIC32CM6408", .chip = "LAN8651",
        .tag = { "The eyes - senses every note", "the instant it appears" },
        .tagline = "The eyes - senses every note the instant it appears",
        .accent = &SCHEME_NODE_FRETBOARD,
        .prose = {
            "fretboard watches the game screen where notes appear and",
            "senses them the instant they arrive - then decides, right",
            "there on the board, which button that note calls for.",
            "Real-time sensing and on-device decision-making, without",
            "waiting on anything else.",
        },
        .built = {
            "MPLAB X IDE",
            "MCC (MPLAB Code Configurator)",
            "Microchip LAN8651 networking",
        },
    },
    {
        .id = 5u, .name = "beatbox", .part = "dsPIC33AK512MPS512", .chip = "LAN8651",
        .tag = { "The ears - listens to the", "music and finds the beat" },
        .tagline = "The ears - listens to the music and finds the beat",
        .accent = &SCHEME_NODE_BEATBOX,
        .prose = {
            "beatbox listens to the music itself and figures out where the",
            "beat falls, in real time - then shares that rhythm with the",
            "rest of the system so the puppet can nod its head and the",
            "lights can pulse in time with the song.",
        },
        .built = {
            "MPLAB X IDE",
            "XC-DSC compiler",
            "Curiosity Platform Development Board",
        },
    },
    {
        .id = 6u, .name = "lemmy", .part = "PIC32CM6408", .chip = "LAN8651",
        .tag = { "The body - head-bangs a puppet", "in time with the music" },
        .tagline = "The body - head-bangs a puppet in time with the music",
        .accent = &SCHEME_NODE_LEMMY,
        .prose = {
            "lemmy is an animated puppet that brings the performance to",
            "life - nodding its head and moving its jaw right along with",
            "the beat that beatbox hears. It takes the rhythm it's given",
            "and turns it into motion.",
        },
        .built = {
            "MPLAB X IDE",
            "MCC (MPLAB Code Configurator)",
            "Microchip LAN8651 networking",
        },
    },
    {
        .id = 7u, .name = "lightshow", .part = "PIC32CM6408", .chip = "LAN8651",
        .tag = { "The lights - brings the", "stage to life" },
        .tagline = "The lights - brings the stage to life",
        .accent = &SCHEME_NODE_LIGHTSHOW,
        .prose = {
            "lightshow turns the same beat that moves lemmy's head into a",
            "stage light show - driving colorful LED strips that pulse and",
            "animate along with the music.",
        },
        .built = {
            "MPLAB X IDE",
            "MCC (MPLAB Code Configurator)",
            "Microchip LAN8651 networking",
        },
    },
};

#define HEADLINE  "Seven boards. One cable. Every one Microchip inside."

/* Visual order of the grid: marvin / fretboard / beatbox above,
 * lemmy / guitar / lightshow / fauxmote below. Values are NODE[] indices. */
static const uint8_t GRID_ROW1[3] = { 0u, 3u, 4u };
static const uint8_t GRID_ROW2[4] = { 5u, 2u, 6u, 1u };

/* ── layout ──────────────────────────────────────────────────────────────────
 * Content spans x 16..1264 below the shared titlebar (top ~65px). */
#define MARGIN      16
#define GAP         12

/* Three radii, from the mockup's Tailwind classes. Note its theme.css overrides
 * --radius-lg/xl off a 0.625rem base but leaves --radius-2xl at the default, so
 * rounded-2xl is 16 while rounded-xl is 14 — they are not one scale apart.
 * AaCorners_Render computes coverage per pixel, so any radius is free. */
#define CARD_R      16    /* rounded-2xl: node cards, section cards, photo frame */
#define BTN_R       14    /* rounded-xl:  the detail view's back button          */
#define BOX_R        4    /* rounded:     the bus-position id boxes only         */
#define CONTENT_X   MARGIN
#define CONTENT_W   (BASE_W - 2 * MARGIN)          /* 1248 */

/* overview. The mockup carries no caption under the headline — the two card rows are
 * `flex-1` straight below it — so the grid takes the space a subhead would have had. */
#define HEAD_Y      76
#define HEAD_H      30
#define GRID_Y      (HEAD_Y + HEAD_H + GAP)        /* 118 */
#define GRID_H      (BASE_H - MARGIN - GRID_Y)     /* 666 */
#define CARD_H      ((GRID_H - GAP) / 2)           /* 327 */
#define ROW2_Y      (GRID_Y + CARD_H + GAP)        /* 457 */
#define R1_W        ((CONTENT_W - 2 * GAP) / 3)    /* 408 */
#define R2_W        ((CONTENT_W - 3 * GAP) / 4)    /* 303 */

/* card interior, relative to the card's own origin */
#define BAR_W        6                             /* the mockup's borderLeftWidth */
#define TXT_X        (BAR_W + 14)                  /* 20 */
#define C_NAME_Y     16
#define C_PART_Y     50
#define C_TAG_Y      82
#define C_TAG_PITCH  20
#define CHIP_H       20
#define CHIP_DOT_D    6

/* detail */
#define BACK_X      MARGIN
#define BACK_Y      92
#define BACK_W     132
#define BACK_H      44
#define DIV_X      160
#define ACC_X      176
#define ACC_W        6
#define ACC_Y       90
#define ACC_H       48
#define TITLE_X    194
#define D_PART_Y    82
#define D_TAG_Y    118

#define BODY_Y     164
#define BODY_H      (BASE_H - MARGIN - BODY_Y)     /* 620 */
#define PHOTO_W    288
#define C2_X        (CONTENT_X + PHOTO_W + GAP)    /* 316 */
#define C2_W       680
#define C3_X        (C2_X + C2_W + GAP)            /* 1008 */
#define C3_W       256
#define CPAD        24
#define SEC_H       18                             /* section caption */
#define WHAT_H     380
#define BUILT_Y     (BODY_Y + WHAT_H + GAP)        /* 556 */
#define BUILT_H     (BASE_H - MARGIN - BUILT_Y)    /* 228 */
#define QR_H       232
#define RAIL_Y      (BODY_Y + QR_H + GAP)          /* 408 */
#define RAIL_H      (BASE_H - MARGIN - RAIL_Y)     /* 376 */
#define SEC_BODY_Y  52                             /* first row below a caption */
#define PROSE_PITCH 26
#define BUILT_PITCH 32
#define RAIL_PITCH  40
#define BOX_W       32
#define BOX_H       26
#define BOX_EDGE     2                             /* the mockup's 2px accent ring */

/* Prose wraps to this many columns: the text box is C2_W - 2*CPAD = 632px and
 * DejaVuSansMono_16 advances 10px/glyph (every DejaVu Mono here is monospace, one
 * advance for all 191 glyphs), so 63 fit. The NODE table's lines are wrapped to 62. */
#define PROSE_COLS  62

/* DejaVuSansMonoBold_24's glyph advance, used to lay the node name out after a
 * variable-length part number in the detail header. */
#define MONO24_ADV  14

/* ── static widget storage ───────────────────────────────────────────────────
 * Widgets live in BSS (no Legato pool / LE_MALLOC): the in-place Constructors build
 * them exactly as leX_New would after LE_MALLOC. Pools are sized to the built screen;
 * configASSERT catches undersizing at bring-up. */
/* Longest string is marvin's detail tagline with the THIS DEVICE suffix appended — 65
 * bytes, since show_detail builds it in a char[CAP] and `·` costs two. Prose lines are
 * 61 at most. Kept generously above both so an edit to either doesn't silently clip. */
#define CAP       104u
#define LBL_MAX    96u
#define WGT_MAX    64u
#define BTN_MAX    12u    /* 7 node cards + back, with slack */
#define IMG_MAX     3u    /* the reserved QR slot, with slack */

static leChar        s_buf[LBL_MAX][CAP];
static leFixedString s_fs[LBL_MAX];
static leLabelWidget s_lbl[LBL_MAX];
static unsigned      s_nlbl;
static leWidget      s_wgt[WGT_MAX];
static unsigned      s_nwgt;
static leButtonWidget s_btn[BTN_MAX];
static unsigned      s_nbtn;
static leImageWidget s_img[IMG_MAX];
static unsigned      s_nimg;

/* The two views. Each is built into its own full-screen container so switching is a
 * visibility toggle on one widget rather than a walk over every child — and so a move
 * to a second pre-rendered canvas only has to re-parent these two (see show_view). */
typedef enum { VIEW_OVERVIEW, VIEW_DETAIL, VIEW_COUNT } view_t;

static leWidget *s_root[VIEW_COUNT];
static view_t    s_view = VIEW_COUNT;   /* VIEW_COUNT = nothing shown yet */

/* Handles the detail view repopulates on selection. */
static leButtonWidget *s_card[NODE_N];
static leWidget       *s_d_accent;
static leLabelWidget  *s_d_part, *s_d_name, *s_d_tag;
static leLabelWidget  *s_d_prose[PROSE_MAX];
static leLabelWidget  *s_d_built[BUILT_MAX];
static leWidget       *s_d_built_dot[BUILT_MAX];
static leWidget       *s_d_frame;
static const void     *s_photo_px;   /* selected node's photo pixels, NULL if none */
static bool            s_shown;      /* this screen owns the panel (and so OVR1)   */

/* Deferred photo reveal (photo_task). s_photo_gen is bumped on every change to what
 * should be on the layer, so a reveal that is still waiting can tell it is stale;
 * s_photo_frame is the renderer's frame count when that change queued its repaint. */
static volatile uint32_t s_photo_gen;
static volatile size_t   s_photo_frame;
static TaskHandle_t      s_photo_task;
static StackType_t       s_photo_stack[512];
static StaticTask_t      s_photo_tcb;
static leWidget       *s_d_qr_card;
static leWidget       *s_d_rail_box[NODE_N];
static leLabelWidget  *s_d_rail_id[NODE_N], *s_d_rail_name[NODE_N];

/* ── build helpers ──────────────────────────────────────────────────────────
 * All take their parent, so the same builders work whether both views share one
 * panel or each gets its own. */

static leWidget *add_panel(leWidget *parent, int x, int y, int w, int h,
                           const leScheme *scheme, leBool fill)
{
    configASSERT(s_nwgt < WGT_MAX);

    leWidget *p = &s_wgt[s_nwgt++];
    leWidget_Constructor(p);
    p->fn->setPosition(p, x, y);
    p->fn->setSize(p, w, h);
    if (scheme != NULL) { p->fn->setScheme(p, scheme); }
    p->fn->setBackgroundType(p, fill ? LE_WIDGET_BACKGROUND_FILL
                                     : LE_WIDGET_BACKGROUND_NONE);
    p->fn->setBorderType(p, LE_WIDGET_BORDER_NONE);
    p->flags |= LE_WIDGET_IGNOREPICK;
    parent->fn->addChild(parent, p);
    return p;
}

/* A section card: zinc-900 fill, 1px border, AA-rounded. SCHEME_FILL_ZINC_900's
 * shadowDark is #404040 ≈ zinc-700, so the stock border colour already matches the
 * mockup's border-zinc-700. */
static leWidget *add_card(leWidget *parent, int x, int y, int w, int h)
{
    leWidget *p = add_panel(parent, x, y, w, h, &SCHEME_FILL_ZINC_900, LE_TRUE);
    p->fn->setBorderType(p, LE_WIDGET_BORDER_LINE);
    p->fn->setCornerRadius(p, CARD_R);
    PanelAA_Enable(p);
    return p;
}

static leWidget *add_dot(leWidget *parent, int x, int y, int d, const leScheme *scheme)
{
    leWidget *p = add_panel(parent, x, y, d, d, scheme, LE_TRUE);
    PanelAA_EnableDot(p);
    return p;
}

/* An AA-rounded filled rect (the accent bars). PanelAA_EnableDot draws a capsule for
 * a long thin rect, matching the mockup's rounded-full, and owns the fill — so this
 * never sets cornerRadius, whose stock paint hangs at radius >= half the widget. */
static leWidget *add_capsule(leWidget *parent, int x, int y, int w, int h,
                             const leScheme *scheme)
{
    leWidget *p = add_panel(parent, x, y, w, h, scheme, LE_TRUE);
    PanelAA_EnableDot(p);
    return p;
}

static leLabelWidget *add_label(leWidget *parent, int x, int y, int w, int h,
                                const leFont *font, const leScheme *scheme,
                                leHAlignment ha)
{
    configASSERT(s_nlbl < LBL_MAX);

    leLabelWidget *l = &s_lbl[s_nlbl];
    leLabelWidget_Constructor(l);
    l->fn->setPosition(l, x, y);
    l->fn->setSize(l, w, h);
    l->fn->setScheme(l, scheme);
    l->fn->setBackgroundType(l, LE_WIDGET_BACKGROUND_NONE);
    l->fn->setHAlignment(l, ha);
    l->fn->setVAlignment(l, LE_VALIGN_MIDDLE);
    ((leWidget *)l)->flags |= LE_WIDGET_IGNOREPICK;

    leFixedString *fs = &s_fs[s_nlbl];
    leFixedString_Constructor(fs, s_buf[s_nlbl], CAP);
    ((leString *)fs)->fn->setFont((leString *)fs, (leFont *)font);
    l->fn->setString(l, (leString *)fs);
    s_nlbl++;

    parent->fn->addChild(parent, (leWidget *)l);
    return l;
}

static void set_text(leLabelWidget *l, const char *s)
{
    if (l != NULL)
    {
        (void)lestring_set_utf8(l->fn->getString(l), (s != NULL) ? s : "");
    }
}

static leLabelWidget *add_text(leWidget *parent, int x, int y, int w, int h,
                               const leFont *font, const leScheme *scheme,
                               leHAlignment ha, const char *text)
{
    leLabelWidget *l = add_label(parent, x, y, w, h, font, scheme, ha);
    set_text(l, text);
    return l;
}

static leImageWidget *add_image(leWidget *parent, int x, int y, int w, int h)
{
    configASSERT(s_nimg < IMG_MAX);

    leImageWidget *img = &s_img[s_nimg++];
    _leImageWidget_Constructor(img);
    img->fn->setPosition(img, x, y);
    img->fn->setSize(img, w, h);
    img->fn->setBackgroundType(img, LE_WIDGET_BACKGROUND_NONE);
    img->fn->setBorderType(img, LE_WIDGET_BORDER_NONE);
    ((leWidget *)img)->flags |= LE_WIDGET_IGNOREPICK;
    parent->fn->addChild(parent, (leWidget *)img);
    return img;
}

/* The empty rounded frame for the photo column, shown only when that node has no
 * photo. A photo that *is* present is scanned out on OVR1 above this canvas with its
 * own matching frame baked in (tools/node-photos), so nothing on BASE can draw over
 * it — which is also why the runtime round-image pass is not used here. */
static leWidget *add_image_frame(leWidget *parent, int x, int y, int w, int h)
{
    leWidget *p = add_panel(parent, x, y, w, h, &SCHEME_BACKGROUND, LE_FALSE);
    p->fn->setBorderType(p, LE_WIDGET_BORDER_LINE);
    p->fn->setCornerRadius(p, CARD_R);
    PanelAA_Enable(p);
    return p;
}

static leButtonWidget *add_button(leWidget *parent, int x, int y, int w, int h,
                                  int radius, const leScheme *scheme)
{
    configASSERT(s_nbtn < BTN_MAX);

    leButtonWidget *b = &s_btn[s_nbtn++];
    leButtonWidget_Constructor(b);
    b->fn->setPosition(b, x, y);
    b->fn->setSize(b, w, h);
    b->fn->setScheme(b, scheme);
    b->fn->setBackgroundType(b, LE_WIDGET_BACKGROUND_FILL);
    b->fn->setBorderType(b, LE_WIDGET_BORDER_LINE);
    b->fn->setCornerRadius(b, (uint32_t)radius);
    b->fn->setPressedOffset(b, 0);
    ButtonAA_Enable(b);
    parent->fn->addChild(parent, (leWidget *)b);
    return b;
}

/* ── view switching ─────────────────────────────────────────────────────────
 * The single seam between "both views on one canvas" and "one pre-rendered canvas
 * each". Today a view becomes visible by toggling its container: the paint walk
 * (invalidateWidget) and the pick walk (leUtils_PickFromWidget) both skip a widget
 * without LE_WIDGET_VISIBLE and its whole subtree, so one flag covers drawing and
 * touch. The panel-wide invalidate is needed because these labels are transparent and
 * do not repaint their own backdrop.
 *
 * If the overview/detail transition ever costs too much, the move is to give each
 * container its own canvas and make this a layer bind — Legato paints a canvas surface
 * whether or not it is shown (see ui_manager's paint_all_screens_once), so both would
 * stay complete and switching would cost no drawing at all. Nothing outside this
 * function needs to know which of the two it is. */
/* Put the selected node's photo on OVR1, or take it down. The photo is the one thing
 * on this screen that is not drawn into the canvas: it rides its own hardware layer so
 * it keeps 8 bits per channel instead of being quantized to the canvas's RGB565. Hence
 * "visible" for it means a layer bind, not a widget flag, and it has to be reconciled
 * anywhere the view or the selection changes — which is the same set of places
 * show_view already owns.
 *
 * The BASE frame under it is the fallback for a node with no photo: a present photo
 * covers that rect completely and carries its own frame, so the two are exclusive. */
static void photo_apply(void)
{
    /* OVR1 belongs to whichever screen is on the panel: the splash owns it for the
     * whole of boot, and the video frame overlay owns it under the dashboard. Touching
     * it while this screen is not shown would take the layer out from under them —
     * build-time show_view(VIEW_OVERVIEW) runs while the splash is still up. */
    if (!s_shown) { return; }

    bool on = (s_view == VIEW_DETAIL) && (s_photo_px != NULL);

    /* Taking the photo down is immediate; putting it up is deferred (photo_task).
     * Enabling a hardware layer is instant, while the rest of the view is still being
     * painted into the canvas, so an immediate show puts the photo on the panel ahead
     * of the page it belongs to. s_photo_gen invalidates a deferred show that the user
     * has already navigated past.
     *
     * The frame count is sampled here rather than in the task: this runs where the
     * repaint is queued and with the renderer idle (Legato dispatches the tap between
     * frames), which is what makes "the count moved" mean "our damage was drawn". */
    s_photo_gen++;
    s_photo_frame = UiManager_FrameCount();

    if (!on)                       { UiManager_NodePhotoHide(); }
    else if (s_photo_task != NULL) { (void)xTaskNotifyGive(s_photo_task); }

    if (s_d_frame != NULL)
    {
        s_d_frame->fn->setVisible(s_d_frame, on ? LE_FALSE : LE_TRUE);
    }
}

/* Reveal the photo once the canvas has caught up. Its own task because the wait has to
 * happen outside LEGATO_Tasks — the tap that gets us here is dispatched from inside
 * leUpdate, so waiting there would be waiting on ourselves. */
static void photo_task(void *param)
{
    (void)param;

    for (;;)
    {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint32_t gen = s_photo_gen;
        UiManager_WaitFrameAfter(s_photo_frame);

        /* Re-read the live state: the view may have moved on during the wait, in which
         * case that change queued its own notification and owns the decision. */
        if (gen == s_photo_gen && s_shown &&
            s_view == VIEW_DETAIL && s_photo_px != NULL)
        {
            UiManager_NodePhotoShow(s_photo_px, CONTENT_X, BODY_Y, PHOTO_W, BODY_H);
        }
    }
}

static void show_view(view_t v)
{
    if (v == s_view) { return; }

    for (unsigned i = 0u; i < (unsigned)VIEW_COUNT; i++)
    {
        if (s_root[i] != NULL)
        {
            s_root[i]->fn->setVisible(s_root[i], (i == (unsigned)v) ? LE_TRUE : LE_FALSE);
        }
    }
    s_view = v;

    photo_apply();
    Marvin_PANEL_SYSTEM->fn->invalidate(Marvin_PANEL_SYSTEM);
}

/* Point the one detail tree at a node: rewrite its strings, recolour everything that
 * carries the accent, and swap the photo. Cheaper and far less code than seven trees,
 * and the C analogue of the mockup's <Detail node={selected}>. */
static void show_detail(unsigned n)
{
    configASSERT(n < NODE_N);
    const node_info_t *d = &NODE[n];

    s_d_accent->fn->setScheme(s_d_accent, d->accent);

    /* No part number for fauxmote, so the name slides left into its place rather than
     * leaving a hole where a badge would be. */
    set_text(s_d_part, (d->part != NULL) ? d->part : "");
    s_d_name->fn->setPosition(s_d_name,
                              (d->part != NULL)
                                  ? TITLE_X + (int)(strlen(d->part) + 2u) * MONO24_ADV
                                  : TITLE_X,
                              D_PART_Y + 4);
    s_d_name->fn->setScheme(s_d_name, (d->part != NULL) ? &SCHEME_TEXT_ZINC_400
                                                        : d->accent);
    set_text(s_d_name, d->name);

    if (d->this_device)
    {
        char t[CAP];
        (void)snprintf(t, sizeof t, "%s  \xC2\xB7  THIS DEVICE", d->tagline);
        set_text(s_d_tag, t);
    }
    else
    {
        set_text(s_d_tag, d->tagline);
    }

    for (unsigned i = 0u; i < PROSE_MAX; i++)
    {
        set_text(s_d_prose[i], d->prose[i]);
    }
    for (unsigned i = 0u; i < BUILT_MAX; i++)
    {
        set_text(s_d_built[i], d->built[i]);
        s_d_built_dot[i]->fn->setScheme(s_d_built_dot[i], d->accent);
        s_d_built_dot[i]->fn->setVisible(s_d_built_dot[i],
                                         (d->built[i] != NULL) ? LE_TRUE : LE_FALSE);
    }

    s_photo_px = NodeArt_Pixels(d->id);

    /* The QR slot is reserved, not wired: only the nodes with a Microchip product page
     * get one, so the whole card stays hidden for the others rather than showing an
     * empty box. */
    s_d_qr_card->fn->setVisible(s_d_qr_card,
                                (d->part != NULL) ? LE_TRUE : LE_FALSE);

    for (unsigned i = 0u; i < NODE_N; i++)
    {
        bool sel = (i == n);
        s_d_rail_box[i]->fn->setScheme(s_d_rail_box[i],
                                       sel ? NODE[i].accent : &SCHEME_FILL_ZINC_700);
        s_d_rail_id[i]->fn->setScheme(s_d_rail_id[i],
                                      sel ? NODE[i].accent : &SCHEME_TEXT_ZINC_600);
        s_d_rail_name[i]->fn->setScheme(s_d_rail_name[i],
                                        sel ? NODE[i].accent : &SCHEME_TEXT_ZINC_600);
    }

    /* Repaint even when the detail view is already up: show_view() is a no-op on a
     * same-view call, and the strings above have just changed underneath it. Only
     * reachable from the overview today, but a detail-to-detail move (prev/next
     * through the nodes) would otherwise show the previous node's text. */
    if (s_view == VIEW_DETAIL)
    {
        photo_apply();   /* show_view is the no-op here, so rebind the photo directly */
        Marvin_PANEL_SYSTEM->fn->invalidate(Marvin_PANEL_SYSTEM);
    }
    else
    {
        show_view(VIEW_DETAIL);
    }
}

/* ── interactions ───────────────────────────────────────────────────────────*/

static void card_on_release(leButtonWidget *btn)
{
    for (unsigned i = 0u; i < NODE_N; i++)
    {
        if (s_card[i] == btn) { show_detail(i); return; }
    }
}

static void back_on_release(leButtonWidget *btn)
{
    (void)btn;
    show_view(VIEW_OVERVIEW);
}

/* ── overview ───────────────────────────────────────────────────────────────*/

/* One node card: the tap target is the card itself (a button), with the accent edge,
 * text and chip bullet drawn over it as IGNOREPICK siblings. */
static void build_card(leWidget *parent, unsigned n, int x, int y, int w)
{
    const node_info_t *d = &NODE[n];

    s_card[n] = add_button(parent, x, y, w, CARD_H, CARD_R, &SCHEME_FILL_ZINC_900);
    /* The card is the one bordered button in the design, and the mockup lightens its
     * border as well as its fill on press. */
    ButtonAA_EnablePressedBorder(s_card[n]);
    s_card[n]->fn->setReleasedEventCallback(s_card[n], card_on_release);

    /* The mockup's 6px left edge. A transparent overlay over the whole card rather than
     * a bar, so the accent can follow the corners the way the mockup's borderLeftWidth
     * does — tapering into the 1px border instead of ending square at the radius. Must
     * stay a later sibling than the card: it blends over what the card painted. */
    {
        leWidget *edge = add_panel(parent, x, y, w, CARD_H, d->accent, LE_FALSE);
        edge->fn->setCornerRadius(edge, CARD_R);
        PanelAA_EnableLeftAccent(edge, BAR_W, 1u);
    }

    int tx = x + TXT_X;
    int tw = w - TXT_X - 14;

    (void)add_text(parent, tx, y + C_NAME_Y, tw, HEAD_H,
                   (const leFont *)&DejaVuSansMonoBold_24, d->accent,
                   LE_HALIGN_LEFT, d->name);

    /* The part number is the marketing payload of this screen, so it gets its own
     * callout line rather than being one bullet among others. */
    if (d->part != NULL)
    {
        (void)add_text(parent, tx, y + C_PART_Y, tw, 24,
                       (const leFont *)&DejaVuSansMonoBold_18, d->accent,
                       LE_HALIGN_LEFT, d->part);
    }

    for (unsigned i = 0u; i < 2u; i++)
    {
        (void)add_text(parent, tx, y + C_TAG_Y + (int)i * C_TAG_PITCH, tw, C_TAG_PITCH,
                       (const leFont *)&DejaVuSansMono_14, &SCHEME_TEXT_ZINC_300,
                       LE_HALIGN_LEFT, d->tag[i]);
    }

    /* Bottom-anchored chip bullet — the shared LAN8651 link is the hero fact of this
     * screen, so every node names it: fauxmote's MCU is the one non-Microchip part in
     * the lineup, but its T1S PHY is a LAN8651 like everyone else's. */
    if (d->chip != NULL)
    {
        int cy = y + CARD_H - 16 - CHIP_H;
        (void)add_dot(parent, tx, cy + (CHIP_H - CHIP_DOT_D) / 2, CHIP_DOT_D, d->accent);
        (void)add_text(parent, tx + CHIP_DOT_D + 8, cy, tw - CHIP_DOT_D - 8, CHIP_H,
                       (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_400,
                       LE_HALIGN_LEFT, d->chip);
    }
}

static void build_overview(leWidget *parent)
{
    (void)add_text(parent, CONTENT_X, HEAD_Y, CONTENT_W, HEAD_H,
                   (const leFont *)&DejaVuSansMonoBold_24, &SCHEME_TEXT_WHITE,
                   LE_HALIGN_LEFT, HEADLINE);

    for (unsigned i = 0u; i < 3u; i++)
    {
        build_card(parent, GRID_ROW1[i], CONTENT_X + (int)i * (R1_W + GAP), GRID_Y, R1_W);
    }
    for (unsigned i = 0u; i < 4u; i++)
    {
        build_card(parent, GRID_ROW2[i], CONTENT_X + (int)i * (R2_W + GAP), ROW2_Y, R2_W);
    }
}

/* ── detail ─────────────────────────────────────────────────────────────────*/

static void build_detail(leWidget *parent)
{
    /* header: back, divider, accent bar, part + name + tagline */
    leButtonWidget *back = add_button(parent, BACK_X, BACK_Y, BACK_W, BACK_H, BTN_R,
                                      &SCHEME_FILL_ZINC_800);
    /* Borderless in the mockup — a filled pill, unlike the cards. add_button's default is
     * a LINE border, so drop it here; the AA pass then blends backdrop straight to fill. */
    ((leWidget *)back)->fn->setBorderType((leWidget *)back, LE_WIDGET_BORDER_NONE);
    back->fn->setReleasedEventCallback(back, back_on_release);
    (void)add_text(parent, BACK_X, BACK_Y, BACK_W, BACK_H,
                   (const leFont *)&DejaVuSansMono_14, &SCHEME_TEXT_ZINC_300,
                   LE_HALIGN_CENTER, "\xC2\xAB SYSTEM");

    (void)add_panel(parent, DIV_X, BACK_Y, 1, BACK_H, &SCHEME_FILL_ZINC_700, LE_TRUE);
    s_d_accent = add_capsule(parent, ACC_X, ACC_Y, ACC_W, ACC_H, &SCHEME_NODE_MARVIN);

    s_d_part = add_label(parent, TITLE_X, D_PART_Y, 320, HEAD_H,
                         (const leFont *)&DejaVuSansMonoBold_24, &SCHEME_NODE_MARVIN,
                         LE_HALIGN_LEFT);
    s_d_name = add_label(parent, TITLE_X, D_PART_Y + 4, 260, 24,
                         (const leFont *)&DejaVuSansMonoBold_20, &SCHEME_TEXT_ZINC_400,
                         LE_HALIGN_LEFT);
    s_d_tag  = add_label(parent, TITLE_X, D_TAG_Y, BASE_W - TITLE_X - MARGIN, 20,
                         (const leFont *)&DejaVuSansMono_14, &SCHEME_TEXT_ZINC_400,
                         LE_HALIGN_LEFT);

    /* col 1: the board photo's empty frame. The photo itself is not a widget — it is
     * scanned out on OVR1 over this rect (see photo_apply). */
    s_d_frame = add_image_frame(parent, CONTENT_X, BODY_Y, PHOTO_W, BODY_H);

    /* col 2: what it does, then built with */
    (void)add_card(parent, C2_X, BODY_Y, C2_W, WHAT_H);
    (void)add_text(parent, C2_X + CPAD, BODY_Y + CPAD, C2_W - 2 * CPAD, SEC_H,
                   (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500,
                   LE_HALIGN_LEFT, "WHAT IT DOES");
    for (unsigned i = 0u; i < PROSE_MAX; i++)
    {
        s_d_prose[i] = add_label(parent, C2_X + CPAD,
                                 BODY_Y + SEC_BODY_Y + (int)i * PROSE_PITCH,
                                 C2_W - 2 * CPAD, PROSE_PITCH,
                                 (const leFont *)&DejaVuSansMono_16,
                                 &SCHEME_TEXT_ZINC_200, LE_HALIGN_LEFT);
    }

    (void)add_card(parent, C2_X, BUILT_Y, C2_W, BUILT_H);
    (void)add_text(parent, C2_X + CPAD, BUILT_Y + CPAD, C2_W - 2 * CPAD, SEC_H,
                   (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500,
                   LE_HALIGN_LEFT, "BUILT WITH");
    for (unsigned i = 0u; i < BUILT_MAX; i++)
    {
        int by = BUILT_Y + SEC_BODY_Y + (int)i * BUILT_PITCH;
        s_d_built_dot[i] = add_dot(parent, C2_X + CPAD, by + (24 - CHIP_DOT_D) / 2,
                                   CHIP_DOT_D, &SCHEME_NODE_MARVIN);
        s_d_built[i] = add_label(parent, C2_X + CPAD + CHIP_DOT_D + 12, by,
                                 C2_W - 2 * CPAD - CHIP_DOT_D - 12, 24,
                                 (const leFont *)&DejaVuSansMono_14,
                                 &SCHEME_TEXT_ZINC_200, LE_HALIGN_LEFT);
    }

    /* col 3: the reserved QR slot, then the bus rail. The image and caption are
     * children of the card, so hiding the card hides the whole slot in one call — and
     * dropping the real QR image in later needs no other change. */
    s_d_qr_card = add_card(parent, C3_X, BODY_Y, C3_W, QR_H);
    (void)add_image(s_d_qr_card, (C3_W - 192) / 2, 20, 192, 192);
    (void)add_text(s_d_qr_card, 0, QR_H - 32, C3_W, SEC_H,
                   (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500,
                   LE_HALIGN_CENTER, "SCAN FOR PRODUCT PAGE");

    (void)add_card(parent, C3_X, RAIL_Y, C3_W, RAIL_H);
    (void)add_text(parent, C3_X + CPAD, RAIL_Y + CPAD, C3_W - 2 * CPAD, SEC_H,
                   (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500,
                   LE_HALIGN_LEFT, "BUS POSITION");
    for (unsigned i = 0u; i < NODE_N; i++)
    {
        int ry = RAIL_Y + SEC_BODY_Y + (int)i * RAIL_PITCH;
        char t[8];

        /* The mockup's 2px accent ring: an outer fill in the ring colour with the
         * card's own fill inset inside it, since one scheme cannot carry both a fill
         * and a differently-coloured border. */
        s_d_rail_box[i] = add_panel(parent, C3_X + CPAD, ry, BOX_W, BOX_H,
                                    &SCHEME_FILL_ZINC_700, LE_TRUE);
        s_d_rail_box[i]->fn->setCornerRadius(s_d_rail_box[i], BOX_R);
        PanelAA_Enable(s_d_rail_box[i]);

        leWidget *inner = add_panel(parent, C3_X + CPAD + BOX_EDGE, ry + BOX_EDGE,
                                    BOX_W - 2 * BOX_EDGE, BOX_H - 2 * BOX_EDGE,
                                    &SCHEME_FILL_ZINC_900, LE_TRUE);
        inner->fn->setCornerRadius(inner, BOX_R - BOX_EDGE);
        PanelAA_Enable(inner);

        (void)snprintf(t, sizeof t, "%u", (unsigned)NODE[i].id);
        s_d_rail_id[i] = add_text(parent, C3_X + CPAD, ry, BOX_W, BOX_H,
                                  (const leFont *)&DejaVuSansMonoBold_12,
                                  &SCHEME_TEXT_ZINC_600, LE_HALIGN_CENTER, t);
        s_d_rail_name[i] = add_text(parent, C3_X + CPAD + BOX_W + 12, ry,
                                    C3_W - 2 * CPAD - BOX_W - 12, BOX_H,
                                    (const leFont *)&DejaVuSansMono_14,
                                    &SCHEME_TEXT_ZINC_600, LE_HALIGN_LEFT,
                                    NODE[i].name);
    }
}

/* ── lifecycle ──────────────────────────────────────────────────────────────*/

void ScreenSystem_InitSurface(void)
{
    UiSurface_Set(CANVAS_SYSTEM, BASE_W, BASE_H, GFX_COLOR_MODE_RGB_565, s_fb);
}

void ScreenSystem_Setup(void)
{
    gfxcSetWindowPosition(CANVAS_SYSTEM, 0, 0);
    gfxcSetWindowSize(CANVAS_SYSTEM, BASE_W, BASE_H);
    Marvin_PANEL_SYSTEM->fn->setBackgroundType(Marvin_PANEL_SYSTEM,
                                               LE_WIDGET_BACKGROUND_FILL);

    /* Shared titlebar (hamburger + logos), same as the other base views. Added first
     * so the view containers paint over the page, never over the chrome. */
    Titlebar_Add(Marvin_PANEL_SYSTEM);
    ScreenSystem_SetInput(false);

    /* Full-screen containers, so both views address the panel's own coordinates. They
     * are IGNOREPICK (see add_panel) which also keeps a tap on empty page from being
     * captured here instead of falling through to the titlebar beneath. */
    s_root[VIEW_OVERVIEW] = add_panel(Marvin_PANEL_SYSTEM, 0, 0, BASE_W, BASE_H,
                                      NULL, LE_FALSE);
    s_root[VIEW_DETAIL]   = add_panel(Marvin_PANEL_SYSTEM, 0, 0, BASE_W, BASE_H,
                                      NULL, LE_FALSE);

    build_overview(s_root[VIEW_OVERVIEW]);
    build_detail(s_root[VIEW_DETAIL]);

    show_detail(0u);          /* seed every detail string/scheme once */
    show_view(VIEW_OVERVIEW);

    s_photo_task = xTaskCreateStatic(photo_task, "NodePhoto",
                                     (uint32_t)(sizeof s_photo_stack / sizeof s_photo_stack[0]),
                                     NULL, 2u, s_photo_stack, &s_photo_tcb);
}

void ScreenSystem_SetInput(bool on)
{
    if (on) { Marvin_PANEL_SYSTEM->flags |=  LE_WIDGET_ENABLED; }
    else    { Marvin_PANEL_SYSTEM->flags &= ~LE_WIDGET_ENABLED; }
}

void ScreenSystem_SetShown(bool shown)
{
    /* Re-entering lands on the grid: leaving from a node detail via the drawer and
     * coming back to that same detail would be a confusing place to arrive. No-op (and
     * so no repaint) when the overview is already the current view.
     *
     * Leaving must drop the photo layer explicitly: OVR1 belongs to whoever is on
     * screen, and the next base view hands it to the video frame overlay. */
    s_shown = shown;

    if (shown) { show_view(VIEW_OVERVIEW); }
    else       { UiManager_NodePhotoHide(); }
}
