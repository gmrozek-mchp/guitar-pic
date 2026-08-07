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
#include "ui/qr_art.h"

#include "ui/gfx/qr_raster.h"        /* QR_RASTER_W — the QR slot is sized from it */
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

/* System info — a product showcase of the seven boards plus the project that contains
 * them, built programmatically into the MGS layer-7 panel (Marvin_PANEL_SYSTEM). Two views
 * live on the one surface: an overview grid of eight cards, and one detail view repopulated
 * from the tapped card's record. The NODE table below is the content: nothing here reads
 * from a doc.
 *
 * Both views are built once into their own full-screen container panel, and
 * bind_view() is the only thing that puts one on the panel — see the note there.
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
/* One surface per view. Both are painted once at boot and stay painted, so switching
 * between them is a layer bind with no drawing — see bind_view. */
static uint16_t FB_NOCACHE s_fb[BASE_W * BASE_H];
static uint16_t FB_NOCACHE s_fb_detail[BASE_W * BASE_H];

/* ── content ────────────────────────────────────────────────────────────────
 * Prose is stored hand-wrapped, one string per rendered line, because Legato labels
 * do not wrap. Lines are sized to the column they are drawn in (see PROSE_COLS) and
 * are pure ASCII: the fonts declare 0x20-0x7E plus Latin-1, so the source specs' em
 * dashes and curly apostrophes would not render. `«`, `»` and `·` used as glyphs
 * below ARE in the declared Latin-1 range, so they are safe. */

#define PROSE_MAX   8u
#define BUILT_MAX   8u

/* An entry with `story` set gets a different column 2: one full-height card instead of
 * the what-it-does / parts-list pair. Only the project card uses it — the boards are
 * described by what they do and what is on them, the project by how it was made.
 *
 * Its shape is three parts: a lead paragraph in `story` (same font and pitch as `prose`,
 * so the wrap limit is PROSE_COLS), the `story_kicker` line that introduces the figures,
 * and the figures themselves as a dotted list in `story_list`. Each item is a headline
 * against a bullet dot with a dimmer, smaller gloss line under it, which is what lets a
 * number carry a few words of meaning without the headline wrapping. The two-row item is
 * also what fills the card: five one-line bullets would leave a 620px column half empty.
 *
 * The kicker is the one line set in bold and in the card's accent, so the eye lands on it
 * on the way from the paragraph into the list. It is a field of its own rather than the
 * last `story` row because it takes a different font and scheme from the rows around it. */
#define STORY_MAX    5u
#define STORY_ITEMS  5u

typedef struct {
    const char *head;    /* the figure, set against the bullet dot */
    const char *gloss;   /* the dimmer line under it; NULL = a one-row item */
} story_item_t;

/* NODE_BUS_N entries are the boards on the T1S bus, held in bus order. NODE_N adds the
 * whole-project card after them — it is a card and a detail page like the others, but not
 * a node: it has no bus id, so it never appears on the BUS POSITION rail, and PROJECT is
 * the index the few places that special-case it test against. */
#define NODE_BUS_N  7u
#define NODE_N      8u
#define PROJECT     (NODE_N - 1u)

/* Photo key for the project card. NodeArt is keyed by T1S id, and this stands in for one
 * where there is no node — deliberately outside the id space rather than the next id up. */
#define ID_PROJECT  0xFFu

typedef struct {
    uint8_t         id;             /* real T1S PLCA id (see net/t1s/t1s_link.c) */
    const char     *name;
    const char     *part;           /* the accent callout line; NULL = none, see fauxmote */
    const char     *chip;           /* the networking part, or NULL */
    const char     *tag[2];         /* card tagline, wrapped to the narrow column */
    const char     *tagline;        /* detail header, one line */
    const leScheme *accent;
    bool            this_device;
    /* The QR slot's whole content. The URL is the asset — it is encoded to a tile at
     * setup rather than pre-rendered anywhere (see ui/qr_art.h) — and repeats across
     * entries share one tile, so the four PIC32CM nodes cost one. NULL hides the slot.
     * Spelled out in full per entry, like the repeated `built` lines: tools/qr-verify
     * reads these literals straight out of this file to decode-check them. */
    const char     *qr_url;
    const char     *qr_caption;     /* NULL = QR_CAPTION */
    const char     *prose[PROSE_MAX];
    const char     *built[BUILT_MAX];
    const char     *story[STORY_MAX];   /* set = one big card instead of those two */
    const char     *story_kicker;
    story_item_t    story_list[STORY_ITEMS];
} node_info_t;

/* What the QR points at, for the six nodes whose callout is an orderable part number. */
#define QR_CAPTION  "SCAN FOR PRODUCT PAGE"

/* The first NODE_BUS_N entries are indexed in bus order (id 0,1,3,4,5,6,7) so the BUS
 * POSITION rail reads naturally, and the project card follows them at PROJECT; the overview
 * grid draws all of them in visual order instead (see GRID_ROW1/2).
 *
 * `built` is a Microchip parts list — silicon, board, support parts, then the tools —
 * which is why every entry that can name an orderable part number does. fauxmote
 * carries no part number of its own: its MCU is the one non-Microchip part in the
 * lineup, so its card names the Feather form factor and the radio class and stops
 * there. Its T1S PHY is a LAN8651 like every other node's. */
static const node_info_t NODE[NODE_N] = {
    {
        .id = 0u, .name = "marvin", .part = "SAM9X75D2G", .chip = "LAN8651",
        .tag = { "The brain - watches the", "game, calls every shot" },
        .tagline = "The brain - watches the game and calls every shot",
        .accent = &SCHEME_NODE_MARVIN, .this_device = true,
        .qr_url = "https://www.microchip.com/en-us/product/SAM9X75D2G",
        .prose = {
            "marvin is the board running this screen, and the",
            "brain of the whole rig. It captures the game's video",
            "over HDMI at 60 frames a second, finds the notes in",
            "each frame, works out the exact moment every button",
            "must be pressed, then calls it over one pair of wires.",
            "",
            "One 800 MHz processor does all of it at once: capture,",
            "computer vision, note timing, and this touch UI.",
        },
        .built = {
            "SAM9X75D2G - 800 MHz Arm926 MPU, 2 Gbit DDR3L in package",
            "SAM9X75 Curiosity Development Board (EV31H43A)",
            "MCP16502 power management IC",
            "SST26VF064B 64 Mbit SQI flash - settings storage",
            "10.1-inch 1280x800 LVDS panel with maXTouch touch",
            "LAN8651B1 10BASE-T1S MAC-PHY (Two-Wire ETH 3 Click)",
            "MPLAB extensions for VS Code, MCC Harmony 3, XC32",
            "MPLAB Graphics Suite (Legato) + FreeRTOS drive this UI",
        },
    },
    {
        .id = 1u, .name = "fauxmote", .part = NULL, .chip = "LAN8651",
        .tag = { "The stand-in - becomes", "the controller itself" },
        .tagline = "The stand-in - becomes the controller, wirelessly",
        .accent = &SCHEME_NODE_FAUXMOTE,
        /* No part number of its own, so its QR names the one Microchip part it does
         * carry — the T1S PHY shared with every other node. */
        .qr_url = "https://www.microchip.com/en-us/product/LAN8651",
        .qr_caption = "SCAN FOR LAN8651 PAGE",
        .prose = {
            "fauxmote skips the guitar altogether. Instead of",
            "pressing buttons on a real controller, it pretends to",
            "be one - speaking the console's own wireless protocol,",
            "down to the encryption the game expects.",
            "",
            "It is the one board here that isn't a Microchip MCU:",
            "the side experiment. It still joins the same",
            "single-pair bus, and takes the same calls as the rest.",
        },
        .built = {
            "LAN8651B1 10BASE-T1S MAC-PHY (Two-Wire ETH 3 Click)",
            "Adafruit Feather board with a Bluetooth Classic radio",
            "Same T1S frames as every Microchip node on the bus",
        },
    },
    {
        .id = 3u, .name = "guitar", .part = "PIC32CM6408PL10048", .chip = "LAN8651",
        .tag = { "The hands - presses the", "buttons in perfect time" },
        .tagline = "The hands - takes the call and presses the buttons",
        .accent = &SCHEME_NODE_GUITAR,
        .qr_url = "https://www.microchip.com/en-us/product/PIC32CM6408PL10048",
        .prose = {
            "guitar is the board that actually plays. One byte",
            "arrives over the bus - which frets are held, and",
            "whether to strum - and its outputs follow, a couple of",
            "milliseconds behind the call.",
            "",
            "It holds no game logic at all: no camera, no timing,",
            "no notes. That is deliberate. Sensing, timing and",
            "pressing each live on their own node, swappable.",
        },
        .built = {
            "PIC32CM6408PL10048 - 5 V Cortex-M0+ MCU, 64 KB, 24 MHz",
            "PIC32CM PL10 Curiosity Nano (EV10P22A)",
            "LAN8651B1 10BASE-T1S MAC-PHY (Two-Wire ETH 3 Click)",
            "5 V I/O drives the guitar's buttons with no level shift",
            "Bare metal - no RTOS, one main loop, static allocation",
            "MPLAB extensions for VS Code, MCC Harmony, XC32",
        },
    },
    {
        .id = 4u, .name = "fretboard", .part = "PIC32CM6408PL10048", .chip = "LAN8651",
        .tag = { "The eyes - reads notes", "off the screen itself" },
        .tagline = "The eyes - reads the notes straight off the screen",
        .accent = &SCHEME_NODE_FRETBOARD,
        .qr_url = "https://www.microchip.com/en-us/product/PIC32CM6408PL10048",
        .prose = {
            "fretboard watches the TV itself. Five phototransistors",
            "sit over the spot where notes reach the strike line,",
            "and the MCU samples all five 240 times a second.",
            "",
            "A small neural network - trained on marvin's own play",
            "and squeezed into 64 KB of flash - runs on the board",
            "and turns those five light readings into the buttons",
            "to press, then sends them straight out over the bus.",
        },
        .built = {
            "PIC32CM6408PL10048 - 5 V Cortex-M0+ MCU, 64 KB, 24 MHz",
            "PIC32CM PL10 Curiosity Nano (EV10P22A)",
            "Custom signal-conditioning board: 5 phototransistors",
            "MCP6002 dual op-amps - phototransistor front end",
            "12-bit ADC - five channels sampled at 240 Hz",
            "LAN8651B1 10BASE-T1S MAC-PHY (Two-Wire ETH 3 Click)",
            "MPLAB extensions for VS Code, MCC Harmony, XC32",
        },
    },
    {
        .id = 5u, .name = "beatbox", .part = "dsPIC33AK512MPS512", .chip = "LAN8651",
        .tag = { "The ears - listens and", "finds the beat" },
        .tagline = "The ears - listens to the music and finds the beat",
        .accent = &SCHEME_NODE_BEATBOX,
        .qr_url = "https://www.microchip.com/en-us/product/dsPIC33AK512MPS512",
        .prose = {
            "beatbox listens. Stereo line-level audio comes in",
            "through the on-chip converter at 48,000 samples a",
            "second, and 512-point FFTs run over it 23 times a",
            "second.",
            "",
            "From how the energy jumps between one FFT and the next",
            "it picks out kick drums and bass hits - the beat - and",
            "broadcasts it to the bus. lemmy and lightshow follow.",
        },
        .built = {
            "dsPIC33AK512MPS512 - 200 MHz DSC with hardware FPU",
            "dsPIC33AK512MPS512 GP DIM (EV80L65A)",
            "Curiosity Platform Development Board (EV74H48A)",
            "On-chip ADC - stereo line in at 48 kHz",
            "PWM outputs as a monitor DAC",
            "LAN8651B1 10BASE-T1S MAC-PHY (Two-Wire ETH 3 Click)",
            "MPLAB extensions for VS Code, MCC Melody, XC-DSC",
        },
    },
    {
        .id = 6u, .name = "lemmy", .part = "PIC32CM6408PL10048", .chip = "LAN8651",
        .tag = { "The body - head-bangs a", "puppet on the beat" },
        .tagline = "The body - head-bangs a puppet in time with the music",
        .accent = &SCHEME_NODE_LEMMY,
        .qr_url = "https://www.microchip.com/en-us/product/PIC32CM6408PL10048",
        .prose = {
            "lemmy is the puppet. Two hobby servos - one in the",
            "neck, one in the jaw - run off a timer on the MCU, and",
            "the head nods to the beat beatbox broadcasts.",
            "",
            "It hears the same beat frame lightshow does, keeps its",
            "own small oscillator locked to it, and rides straight",
            "through quiet patches instead of stopping dead.",
        },
        .built = {
            "PIC32CM6408PL10048 - 5 V Cortex-M0+ MCU, 64 KB, 24 MHz",
            "PIC32CM PL10 Curiosity Nano (EV10P22A)",
            "TCC timer - 50 Hz servo pulses, no CPU per frame",
            "Two R/C hobby servos: neck nod and jaw",
            "LAN8651B1 10BASE-T1S MAC-PHY (Two-Wire ETH 3 Click)",
            "MPLAB extensions for VS Code, MCC Harmony, XC32",
        },
    },
    {
        .id = 7u, .name = "lightshow", .part = "PIC32CM6408PL10048", .chip = "LAN8651",
        .tag = { "The lights - turns the", "beat into a stage show" },
        .tagline = "The lights - turns the beat into a stage show",
        .accent = &SCHEME_NODE_LIGHTSHOW,
        .qr_url = "https://www.microchip.com/en-us/product/PIC32CM6408PL10048",
        .prose = {
            "lightshow drives two strands of addressable LEDs from",
            "the same beat frame lemmy nods to - pulsing on the",
            "kick, washing colour with the bass.",
            "",
            "Each LED wants a 5 volt data stream shaped to about a",
            "microsecond per bit. This MCU's 5 V outputs drive the",
            "strips directly - no level shifter - while a timer and",
            "DMA clock out both strands and leave the CPU free.",
        },
        .built = {
            "PIC32CM6408PL10048 - 5 V Cortex-M0+ MCU, 64 KB, 24 MHz",
            "PIC32CM PL10 Curiosity Nano (EV10P22A)",
            "5 V I/O on VDDIO2 - drives LED data with no shifter",
            "TC timer + DMA - 800 kHz bit stream, zero CPU",
            "Two 33-pixel addressable LED strands",
            "LAN8651B1 10BASE-T1S MAC-PHY (Two-Wire ETH 3 Click)",
            "MPLAB extensions for VS Code, MCC Harmony, XC32",
        },
    },
    /* The whole project, not a node — see PROJECT. No bus id, and `part` carries a
     * category line rather than an orderable number, so its QR points at the project
     * itself rather than a product page. It sets `story` instead of prose + built: the
     * boards each say what they do and what is on them, and this card says how the whole
     * thing got written, which is the part visitors ask about. */
    {
        .id = ID_PROJECT, .name = "guitar-pic", .part = "DIGITAL MUSIC INTEGRATION",
        .chip = "10BASE-T1S single-pair bus",
        .tag = { "A robot band that plays", "Guitar Hero by itself" },
        .tagline = "The whole rig - a robot band that plays Guitar Hero by itself",
        .accent = &SCHEME_NODE_PROJECT,
        .qr_url = "https://github.com/gmrozek-mchp/guitar-pic",
        .qr_caption = "SCAN FOR PROJECT SOURCE",
        .story = {
            "Every board in this rig was programmed with an AI coding",
            "agent working next to a developer: Claude, in a terminal",
            "beside MPLAB. It reads the datasheet, writes the driver,",
            "and explains what it changed and why.",
        },
        .story_kicker = "This is what it wrote:",
        .story_list = {
            { "41,000 lines of application C",
              "seven boards, four silicon families, three compilers" },
            { "19,000 lines of Python",
              "seven host-side tools it wrote to check its own work" },
            { "3,400 lines of tooling it built for itself",
              "17 scripts, to edit a UI design it cannot click through" },
            { "23,000 lines of specs and journals",
              "the memory that lets a session resume, not restart" },
            { "626 commits in six months",
              "across 110 recorded sessions with the agent" },
        },
    },
};

#define HEADLINE  "Seven boards. One pair of wires. Microchip in every one."

/* Visual order of the grid: guitar-pic / marvin / fretboard / beatbox above,
 * lemmy / guitar / lightshow / fauxmote below. Values are NODE[] indices — the whole-project
 * card leads, so the row reads parent-then-brain. Both rows are four wide, so one card
 * width serves both (see CARD_W). */
#define GRID_COLS   4u
static const uint8_t GRID_ROW1[GRID_COLS] = { PROJECT, 0u, 3u, 4u };
static const uint8_t GRID_ROW2[GRID_COLS] = { 5u, 2u, 6u, 1u };

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
#define CARD_W      ((CONTENT_W - (int)(GRID_COLS - 1u) * GAP) / (int)GRID_COLS)   /* 303 */

/* card interior, relative to the card's own origin */
#define BAR_W        6                             /* the mockup's borderLeftWidth */
#define TXT_X        (BAR_W + 14)                  /* 20 */
#define C_NAME_Y     16
#define C_PART_Y     50
#define C_TAG_Y      82
#define C_TAG_PITCH  22    /* DejaVuSansMono_16 is 20px tall, so 20 would touch */
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

/* The node name, set beside the part number. Its box has to be taller than the glyph —
 * DejaVuSansMonoBold_20 is 25px — or a descender is clipped, and since a label centres
 * its text at `y + h/2 - fontHeight/2` (integer division, leUtils_ArrangeRectangleRelative)
 * the y compensates for the extra height so the baseline still lands on the part
 * number's. Both must change together. */
#define D_NAME_Y    (D_PART_Y + 2)
#define D_NAME_H    28

#define BODY_Y     164
#define BODY_H      (BASE_H - MARGIN - BODY_Y)     /* 620 */
#define PHOTO_W    288
#define C2_X        (CONTENT_X + PHOTO_W + GAP)    /* 316 */
#define C2_W       680
#define C3_X        (C2_X + C2_W + GAP)            /* 1008 */
#define C3_W       256
#define CPAD        24
#define SEC_H       18                             /* section caption */
/* WHAT_H is sized to its content — SEC_BODY_Y + PROSE_MAX * PROSE_PITCH + a bottom pad
 * — rather than splitting the column evenly, so the surplus goes to the parts list
 * below, which is the section that ran out of room. */
#define WHAT_H     296
#define BUILT_Y     (BODY_Y + WHAT_H + GAP)        /* 472 */
#define BUILT_H     (BASE_H - MARGIN - BUILT_Y)    /* 312 */
#define QR_H       232
/* The QR tile is not scaled: its size comes from the rasterizer, which lays out whole
 * pixels per module (164 = 41 modules x 4 px), and resampling a QR is how you make one
 * that will not scan. So the card holds the tile at 1:1, centred in the space above the
 * caption, rather than the tile being fitted to a box chosen here. */
#define QR_IMG      ((int)QR_RASTER_W)              /* 164 */
#define QR_CAP_Y    (QR_H - 32)                     /* 200 */
#define QR_IMG_X    ((C3_W - QR_IMG) / 2)           /* 46  */
#define QR_IMG_Y    ((QR_CAP_Y - QR_IMG) / 2)       /* 18  */
#define RAIL_Y      (BODY_Y + QR_H + GAP)          /* 408 */
#define RAIL_H      (BASE_H - MARGIN - RAIL_Y)     /* 376 */
#define SEC_BODY_Y  52                             /* first row below a caption */
#define PROSE_PITCH 28
#define BUILT_PITCH 30
#define RAIL_PITCH  40
#define BOX_W       32
#define BOX_H       26
#define BOX_EDGE     2                             /* the mockup's 2px accent ring */

/* Prose wraps to this many columns: the text box is C2_W - 2*CPAD = 632px and
 * DejaVuSansMono_18 advances 11px/glyph (every DejaVu Mono here is monospace, one
 * advance for all its glyphs), so 57 fit. The NODE table's lines are wrapped to that.
 *
 * A parts row is narrower — it sits after the bullet dot — at C2_W - 2*CPAD - CHIP_DOT_D
 * - 12 = 614px, and DejaVuSansMono_16 advances 10px, so 61 fit. */
#define PROSE_COLS  57
#define BUILT_COLS  61

/* The story card, top to bottom: STORY_MAX lead rows at PROSE_PITCH from SEC_BODY_Y, the
 * kicker, then STORY_ITEMS two-row items. The lead paragraph uses four of its five rows,
 * so the spare row is the paragraph break above the kicker.
 *
 * Pinned so the last gloss lands on the same 36px bottom pad the rest of the column uses:
 * STORY_LIST_Y + 4*STORY_ITEM_PITCH + STORY_HEAD_PITCH + STORY_GLOSS_H = 584 against a
 * 620px card. Change any of them and the list drifts off the card bottom silently — a
 * Legato child is clipped to its parent, not reported.
 *
 * The gap inside an item is zero (the rows abut, so the gloss reads as part of the
 * headline); the 16px gap between items is the remainder of STORY_ITEM_PITCH. Both rows
 * are indented past the dot like a parts row, so a headline wraps to STORY_HEAD_COLS
 * (614px at 12px/glyph for DejaVuSansMono_20) and a gloss to BUILT_COLS. */
#define STORY_KICKER_Y    (SEC_BODY_Y + (int)STORY_MAX * PROSE_PITCH)   /* 192 */
#define STORY_LIST_Y      (STORY_KICKER_Y + PROSE_PITCH + 20)           /* 240 */
#define STORY_ITEM_PITCH   72
#define STORY_HEAD_PITCH   30    /* DejaVuSansMono_20 is 25px tall */
#define STORY_GLOSS_H      26    /* DejaVuSansMono_16 is 20px tall */
#define STORY_HEAD_COLS    51

/* The bullet is bigger than a parts-list dot because it answers a 20px headline rather than
 * a 16px row, and it is placed off the headline's own metrics rather than centred in the row
 * box. Centre it on the box, or on the cap height, and it reads high: the visual mass of a
 * line of mixed-case text sits at the x-height, not between baseline and cap.
 *
 * DejaVuSansMono_20 in a STORY_HEAD_PITCH box puts its baseline at +22 with an x-height of
 * 11, so the x-height middle is +16.5 and an 8px dot centres on it at y = 12.5. Rounded down
 * a half-pixel the other way, since the descender space below the baseline pulls the eye
 * down slightly further. Metrics decoded from le_gen_fonts.c — recipe in the
 * mgs-legato-design skill's REFERENCE.md, under vertical alignment. */
#define STORY_DOT_D         8
#define STORY_DOT_Y        13

/* Glyph advances, decoded from le_gen_fonts.c. MONO24_ADV lays the node name out after a
 * variable-length callout in the detail header; MONO_B18_ADV decides whether that callout
 * fits a card at its full size (see build_card). */
#define MONO24_ADV     14
#define MONO_B18_ADV   11

/* The detail header's callout label, wide enough for the longest string in the table —
 * "DIGITAL MUSIC INTEGRATION", 25 glyphs at MONO24_ADV. */
#define D_PART_W    (26 * MONO24_ADV)               /* 364 */

/* ── static widget storage ───────────────────────────────────────────────────
 * Widgets live in BSS (no Legato pool / LE_MALLOC): the in-place Constructors build
 * them exactly as leX_New would after LE_MALLOC. Pools are sized to the built screen;
 * configASSERT catches undersizing at bring-up. */
/* Longest string is marvin's detail tagline with the THIS DEVICE suffix appended — 66
 * bytes, since show_detail builds it in a char[CAP] and `·` costs two. Prose lines run
 * to PROSE_COLS, parts rows to BUILT_COLS, and story rows to PROSE_COLS / STORY_HEAD_COLS
 * / BUILT_COLS. Kept generously above all of them so an edit doesn't silently clip. */
#define CAP       104u
#define LBL_MAX   128u    /* ~79 in use before the project card's 15 */
#define WGT_MAX    64u
#define BTN_MAX    12u    /* 7 node cards + back, with slack */
#define IMG_MAX     3u    /* the QR tile, with slack */

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
 * each owns a canvas and a Legato layer of its own (see bind_view). */
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
static leLabelWidget  *s_d_story[STORY_MAX], *s_d_kicker;
static leLabelWidget  *s_d_item_head[STORY_ITEMS], *s_d_item_gloss[STORY_ITEMS];
static leWidget       *s_d_item_dot[STORY_ITEMS];
/* The three column-2 cards. Handles only so show_detail can pick a shape: the pair, or
 * the story card. Every other card here is fire-and-forget. */
static leWidget       *s_d_what_card, *s_d_built_card, *s_d_story_card;
static leWidget       *s_d_frame;
static const void     *s_photo_px;   /* selected node's photo pixels, NULL if none */
static bool            s_shown;      /* this screen owns the panel (and so OVR1)   */

/* Deferred bind of the detail canvas (view_task): s_pending_frame is the renderer's frame
 * count when the repaint for the selected node was queued, and s_view_gen is bumped on
 * every request so a bind the user has navigated past can tell it is stale. */
static volatile size_t   s_pending_frame;
static volatile uint32_t s_view_gen;
static TaskHandle_t      s_view_task;
static StackType_t       s_view_stack[512];
static StaticTask_t      s_view_tcb;

static leWidget       *s_d_qr_card;
static leImageWidget  *s_d_qr_img;
static leLabelWidget  *s_d_qr_cap;
/* One encoded tile per card, resolved once at setup; NULL where the URL would not
 * encode, which hides that card's slot. */
static const leImage  *s_qr[NODE_N];
static leWidget       *s_d_rail_box[NODE_BUS_N];
static leLabelWidget  *s_d_rail_id[NODE_BUS_N], *s_d_rail_name[NODE_BUS_N];

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

/* A label box shorter than its font clips a row off the text — top and bottom, so the
 * visible symptom is a cut descender. Legato centres the text at `y + h/2 - fontH/2`
 * (leUtils_ArrangeRectangleRelative) and clips to the widget rect, so growing the box to
 * the font's height and shifting y by the same halved amount fits the glyphs without
 * moving them: the caller's numbers still decide where the text sits. */
static void fit_font(const leFont *font, int *y, int *h)
{
    if ((font == NULL) || (font->type != LE_RASTER_FONT)) { return; }

    int fh = (int)((const leRasterFont *)font)->height;

    if (*h < fh)
    {
        *y += (*h / 2) - (fh / 2);
        *h  = fh;
    }
}

static leLabelWidget *add_label(leWidget *parent, int x, int y, int w, int h,
                                const leFont *font, const leScheme *scheme,
                                leHAlignment ha)
{
    configASSERT(s_nlbl < LBL_MAX);

    fit_font(font, &y, &h);

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
 * bind_view already owns.
 *
 * The BASE frame under it is the fallback for a node with no photo: a present photo
 * covers that rect completely and carries its own frame, so the two are exclusive. */
static void photo_apply(void)
{
    /* OVR1 belongs to whichever screen is on the panel: the splash owns it for the
     * whole of boot, and the video frame overlay owns it under the dashboard. Touching
     * it while this screen is not shown would take the layer out from under them —
     * build-time bind_view(VIEW_OVERVIEW) runs while the splash is still up. */
    if (!s_shown) { return; }

    bool on = (s_view == VIEW_DETAIL) && (s_photo_px != NULL);

    if (on) { UiManager_NodePhotoShow(s_photo_px, CONTENT_X, BODY_Y, PHOTO_W, BODY_H); }
    else    { UiManager_NodePhotoHide(); }

    if (s_d_frame != NULL)
    {
        s_d_frame->fn->setVisible(s_d_frame, on ? LE_FALSE : LE_TRUE);
    }
}

/* Switch views. Each view owns a canvas, both stay painted, so this is a layer bind and
 * nothing is drawn — which is the point: the photo layer flips in the same breath as the
 * bind, so the page and the photo arrive together in one frame. There is no repaint to
 * outrun, and so nothing to defer.
 *
 * Input is moved with the bind rather than following visibility. Both roots are attached
 * to their Legato layers permanently and leInput walks every attached layer, so the
 * hidden view would still answer taps; the pick walk requires LE_WIDGET_ENABLED, and
 * ScreenSystem_SetInput puts that on the current view alone. */
/* Put a view on the panel: bind its canvas, move input to it, settle the photo layer.
 *
 * Unconditional on purpose — there is no "already showing it" shortcut, because s_view
 * says which view is *current*, not whether its canvas is bound to BASE. Those differ
 * whenever another base view has taken the panel since, including at boot: Setup runs
 * bind_view once with nothing bound, and a guard here meant the first real entry did
 * nothing at all. Re-binding the same canvas is a handful of register writes. */
static void bind_view(view_t v)
{
    /* Any view that actually reaches the panel invalidates a deferred bind still waiting:
     * tap a card, leave the screen, come back to the grid, and the pending detail bind
     * would otherwise land on top of it — s_shown is true again by then, so that check
     * alone does not catch it. */
    s_view_gen++;
    s_view = v;

    UiManager_BindSystemView(v == VIEW_DETAIL);
    ScreenSystem_SetInput(s_shown);
    photo_apply();
}

/* Bind the detail canvas once it has been repainted for the node just selected.
 *
 * The grid canvas is static, so entering it is a bare bind. The detail canvas is not: it
 * is one tree re-pointed at whichever node was tapped, so binding it before its repaint
 * lands shows the *previous* node. Waiting keeps the grid on screen until the detail view
 * is complete, which is also what makes the arrival read as one step.
 *
 * Its own task because the wait cannot run in LEGATO_Tasks — the tap that gets here is
 * dispatched from inside leUpdate. s_view_gen drops a request the user has already
 * navigated past (tap a card, then leave the screen before the paint finishes). */
static void view_task(void *param)
{
    (void)param;

    for (;;)
    {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint32_t gen = s_view_gen;
        UiManager_WaitFrameAfter(s_pending_frame);

        if (gen == s_view_gen && s_shown) { bind_view(VIEW_DETAIL); }
    }
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
                              D_NAME_Y);
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

    /* Pick column 2's shape. Both sets of text are written either way — the unused one
     * lands in hidden labels, which costs a few string copies and keeps this free of a
     * second branch — but only one card is visible, and each card owns its own caption
     * and body, so that one flag is the whole switch. */
    leBool story = (d->story[0] != NULL) ? LE_TRUE : LE_FALSE;

    s_d_what_card->fn->setVisible(s_d_what_card,   (story == LE_TRUE) ? LE_FALSE : LE_TRUE);
    s_d_built_card->fn->setVisible(s_d_built_card, (story == LE_TRUE) ? LE_FALSE : LE_TRUE);
    s_d_story_card->fn->setVisible(s_d_story_card, story);

    for (unsigned i = 0u; i < PROSE_MAX; i++)
    {
        set_text(s_d_prose[i], d->prose[i]);
    }
    for (unsigned i = 0u; i < STORY_MAX; i++)
    {
        set_text(s_d_story[i], d->story[i]);
    }
    set_text(s_d_kicker, d->story_kicker);
    s_d_kicker->fn->setScheme(s_d_kicker, d->accent);
    for (unsigned i = 0u; i < STORY_ITEMS; i++)
    {
        set_text(s_d_item_head[i],  d->story_list[i].head);
        set_text(s_d_item_gloss[i], d->story_list[i].gloss);
        s_d_item_dot[i]->fn->setScheme(s_d_item_dot[i], d->accent);
        s_d_item_dot[i]->fn->setVisible(s_d_item_dot[i],
                                        (d->story_list[i].head != NULL) ? LE_TRUE : LE_FALSE);
    }
    for (unsigned i = 0u; i < BUILT_MAX; i++)
    {
        set_text(s_d_built[i], d->built[i]);
        s_d_built_dot[i]->fn->setScheme(s_d_built_dot[i], d->accent);
        s_d_built_dot[i]->fn->setVisible(s_d_built_dot[i],
                                         (d->built[i] != NULL) ? LE_TRUE : LE_FALSE);
    }

    s_photo_px = NodeArt_Pixels(d->id);

    /* An entry with no URL, or one that failed to encode, hides the whole card rather
     * than showing an empty box or a tile from the previously selected node. */
    if (s_qr[n] != NULL)
    {
        s_d_qr_img->fn->setImage(s_d_qr_img, (leImage *)s_qr[n]);
        set_text(s_d_qr_cap, (d->qr_caption != NULL) ? d->qr_caption : QR_CAPTION);
        s_d_qr_card->fn->setVisible(s_d_qr_card, LE_TRUE);
    }
    else
    {
        s_d_qr_card->fn->setVisible(s_d_qr_card, LE_FALSE);
    }

    /* The project card lights the whole rail — it *is* the bus — which doubles as the
     * legend for the accent colours the other seven cards carry. */
    for (unsigned i = 0u; i < NODE_BUS_N; i++)
    {
        bool sel = (n == PROJECT) || (i == n);
        s_d_rail_box[i]->fn->setScheme(s_d_rail_box[i],
                                       sel ? NODE[i].accent : &SCHEME_FILL_ZINC_700);
        s_d_rail_id[i]->fn->setScheme(s_d_rail_id[i],
                                      sel ? NODE[i].accent : &SCHEME_TEXT_ZINC_600);
        s_d_rail_name[i]->fn->setScheme(s_d_rail_name[i],
                                        sel ? NODE[i].accent : &SCHEME_TEXT_ZINC_600);
    }

    /* The strings above were written straight into their leFixedStrings, which does not
     * damage the widgets, so the canvas has to be invalidated by hand — this is the only
     * thing that gets the new node's text painted. */
    Marvin_PANEL_SYSTEM_DETAIL->fn->invalidate(Marvin_PANEL_SYSTEM_DETAIL);

    s_view_gen++;

    if (s_view == VIEW_DETAIL)
    {
        /* Already bound, so the repaint lands in place; only the photo needs re-pointing.
         * (Not reachable from the UI today — it is the detail-to-detail move a prev/next
         * rail would make.) */
        photo_apply();
    }
    else if (s_view_task != NULL)
    {
        /* Sampled here, where the damage is queued and the renderer is between frames. */
        s_pending_frame = UiManager_FrameCount();
        (void)xTaskNotifyGive(s_view_task);
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
    bind_view(VIEW_OVERVIEW);
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
     * callout line rather than being one bullet among others. A part number fits the card
     * at Bold_18 (18 chars × 11px against a 269px column); the project card's longer
     * category line does not, so the callout drops a size rather than clipping. */
    if (d->part != NULL)
    {
        bool fits = ((int)strlen(d->part) * MONO_B18_ADV) <= tw;

        (void)add_text(parent, tx, y + C_PART_Y, tw, 24,
                       fits ? (const leFont *)&DejaVuSansMonoBold_18
                            : (const leFont *)&DejaVuSansMonoBold_16,
                       d->accent, LE_HALIGN_LEFT, d->part);
    }

    for (unsigned i = 0u; i < 2u; i++)
    {
        (void)add_text(parent, tx, y + C_TAG_Y + (int)i * C_TAG_PITCH, tw, C_TAG_PITCH,
                       (const leFont *)&DejaVuSansMono_16, &SCHEME_TEXT_ZINC_300,
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

    for (unsigned i = 0u; i < GRID_COLS; i++)
    {
        build_card(parent, GRID_ROW1[i], CONTENT_X + (int)i * (CARD_W + GAP), GRID_Y, CARD_W);
        build_card(parent, GRID_ROW2[i], CONTENT_X + (int)i * (CARD_W + GAP), ROW2_Y, CARD_W);
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

    s_d_part = add_label(parent, TITLE_X, D_PART_Y, D_PART_W, HEAD_H,
                         (const leFont *)&DejaVuSansMonoBold_24, &SCHEME_NODE_MARVIN,
                         LE_HALIGN_LEFT);
    s_d_name = add_label(parent, TITLE_X, D_NAME_Y, 260, D_NAME_H,
                         (const leFont *)&DejaVuSansMonoBold_20, &SCHEME_TEXT_ZINC_400,
                         LE_HALIGN_LEFT);
    s_d_tag  = add_label(parent, TITLE_X, D_TAG_Y, BASE_W - TITLE_X - MARGIN, 20,
                         (const leFont *)&DejaVuSansMono_14, &SCHEME_TEXT_ZINC_400,
                         LE_HALIGN_LEFT);

    /* col 1: the board photo's empty frame. The photo itself is not a widget — it is
     * scanned out on OVR1 over this rect (see photo_apply). */
    s_d_frame = add_image_frame(parent, CONTENT_X, BODY_Y, PHOTO_W, BODY_H);

    /* col 2, in two mutually exclusive shapes — the what-it-does + parts-list pair for a
     * board, or one full-height story card for the project (see STORY_MAX). Each card's
     * caption and body are its own CHILDREN, in card-relative coordinates, so a shape is
     * shown or hidden with a single setVisible on the card rather than by walking its
     * text. That is why these three are the only cards here that keep a handle. */
    s_d_what_card = add_card(parent, C2_X, BODY_Y, C2_W, WHAT_H);
    (void)add_text(s_d_what_card, CPAD, CPAD, C2_W - 2 * CPAD, SEC_H,
                   (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500,
                   LE_HALIGN_LEFT, "WHAT IT DOES");
    for (unsigned i = 0u; i < PROSE_MAX; i++)
    {
        s_d_prose[i] = add_label(s_d_what_card, CPAD,
                                 SEC_BODY_Y + (int)i * PROSE_PITCH,
                                 C2_W - 2 * CPAD, PROSE_PITCH,
                                 (const leFont *)&DejaVuSansMono_18,
                                 &SCHEME_TEXT_ZINC_200, LE_HALIGN_LEFT);
    }

    s_d_built_card = add_card(parent, C2_X, BUILT_Y, C2_W, BUILT_H);
    (void)add_text(s_d_built_card, CPAD, CPAD, C2_W - 2 * CPAD, SEC_H,
                   (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500,
                   LE_HALIGN_LEFT, "MICROCHIP INSIDE");
    for (unsigned i = 0u; i < BUILT_MAX; i++)
    {
        int by = SEC_BODY_Y + (int)i * BUILT_PITCH;
        s_d_built_dot[i] = add_dot(s_d_built_card, CPAD, by + (24 - CHIP_DOT_D) / 2,
                                   CHIP_DOT_D, &SCHEME_NODE_MARVIN);
        s_d_built[i] = add_label(s_d_built_card, CPAD + CHIP_DOT_D + 12, by,
                                 C2_W - 2 * CPAD - CHIP_DOT_D - 12, 24,
                                 (const leFont *)&DejaVuSansMono_16,
                                 &SCHEME_TEXT_ZINC_200, LE_HALIGN_LEFT);
    }

    /* The project's shape: the same column, full height, one caption, a lead paragraph,
     * then the figures as a dotted list. The dots and the indent match a parts row, so the
     * project page reads as the same kind of page as a node's. */
    s_d_story_card = add_card(parent, C2_X, BODY_Y, C2_W, BODY_H);
    (void)add_text(s_d_story_card, CPAD, CPAD, C2_W - 2 * CPAD, SEC_H,
                   (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500,
                   LE_HALIGN_LEFT, "BUILT WITH AI");
    for (unsigned i = 0u; i < STORY_MAX; i++)
    {
        s_d_story[i] = add_label(s_d_story_card, CPAD,
                                 SEC_BODY_Y + (int)i * PROSE_PITCH,
                                 C2_W - 2 * CPAD, PROSE_PITCH,
                                 (const leFont *)&DejaVuSansMono_18,
                                 &SCHEME_TEXT_ZINC_200, LE_HALIGN_LEFT);
    }
    s_d_kicker = add_label(s_d_story_card, CPAD, STORY_KICKER_Y, C2_W - 2 * CPAD,
                           PROSE_PITCH, (const leFont *)&DejaVuSansMonoBold_18,
                           &SCHEME_NODE_PROJECT, LE_HALIGN_LEFT);

    for (unsigned i = 0u; i < STORY_ITEMS; i++)
    {
        int iy = STORY_LIST_Y + (int)i * STORY_ITEM_PITCH;
        int tx = CPAD + CHIP_DOT_D + 12;
        int tw = C2_W - 2 * CPAD - CHIP_DOT_D - 12;

        s_d_item_dot[i] = add_dot(s_d_story_card, CPAD + (CHIP_DOT_D - STORY_DOT_D) / 2,
                                  iy + STORY_DOT_Y, STORY_DOT_D, &SCHEME_NODE_PROJECT);
        s_d_item_head[i] = add_label(s_d_story_card, tx, iy, tw, STORY_HEAD_PITCH,
                                     (const leFont *)&DejaVuSansMono_20,
                                     &SCHEME_TEXT_ZINC_200, LE_HALIGN_LEFT);
        s_d_item_gloss[i] = add_label(s_d_story_card, tx, iy + STORY_HEAD_PITCH, tw,
                                      STORY_GLOSS_H, (const leFont *)&DejaVuSansMono_16,
                                      &SCHEME_TEXT_ZINC_400, LE_HALIGN_LEFT);
    }

    /* col 3: the QR slot, then the bus rail. The image and caption are children of the
     * card, so hiding the card hides the whole slot in one call. Both are repointed per
     * node in show_detail; the tiles themselves are encoded in Setup. */
    s_d_qr_card = add_card(parent, C3_X, BODY_Y, C3_W, QR_H);
    s_d_qr_img  = add_image(s_d_qr_card, QR_IMG_X, QR_IMG_Y, QR_IMG, QR_IMG);
    s_d_qr_cap  = add_label(s_d_qr_card, 0, QR_CAP_Y, C3_W, SEC_H,
                            (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500,
                            LE_HALIGN_CENTER);

    (void)add_card(parent, C3_X, RAIL_Y, C3_W, RAIL_H);
    (void)add_text(parent, C3_X + CPAD, RAIL_Y + CPAD, C3_W - 2 * CPAD, SEC_H,
                   (const leFont *)&DejaVuSansMono_12, &SCHEME_TEXT_ZINC_500,
                   LE_HALIGN_LEFT, "BUS POSITION");
    for (unsigned i = 0u; i < NODE_BUS_N; i++)   /* the project card has no bus id */
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
    UiSurface_Set(CANVAS_SYSTEM,        BASE_W, BASE_H, GFX_COLOR_MODE_RGB_565, s_fb);
    UiSurface_Set(CANVAS_SYSTEM_DETAIL, BASE_W, BASE_H, GFX_COLOR_MODE_RGB_565, s_fb_detail);
}

void ScreenSystem_Setup(void)
{
    leWidget *panel[VIEW_COUNT] = {
        [VIEW_OVERVIEW] = Marvin_PANEL_SYSTEM,
        [VIEW_DETAIL]   = Marvin_PANEL_SYSTEM_DETAIL,
    };
    const unsigned int canvas[VIEW_COUNT] = {
        [VIEW_OVERVIEW] = CANVAS_SYSTEM,
        [VIEW_DETAIL]   = CANVAS_SYSTEM_DETAIL,
    };

    for (unsigned i = 0u; i < (unsigned)VIEW_COUNT; i++)
    {
        gfxcSetWindowPosition(canvas[i], 0, 0);
        gfxcSetWindowSize(canvas[i], BASE_W, BASE_H);
        panel[i]->fn->setBackgroundType(panel[i], LE_WIDGET_BACKGROUND_FILL);

        /* Each view carries its own titlebar (hamburger + logos): the two are separate
         * layers now, so there is nothing to share. Added first so the content paints
         * over the page and never over the chrome. */
        Titlebar_Add(panel[i]);
        s_root[i] = panel[i];
    }

    ScreenSystem_SetInput(false);

    build_overview(s_root[VIEW_OVERVIEW]);
    build_detail(s_root[VIEW_DETAIL]);

    /* Encode every card's QR before the seeding show_detail below reads s_qr. Repeat
     * URLs resolve to the same tile, so the eight cards cost five. No I/O: the URL is
     * the whole input, which is why this needs no card mount and no boot-progress step
     * the way the photos and album art do. */
    for (unsigned i = 0u; i < NODE_N; i++)
    {
        s_qr[i] = QrArt_Get(NODE[i].qr_url);
    }

    /* Seed every detail string once. This also queues the detail canvas's first paint,
     * which paint_all_screens_once would do anyway — and it must NOT leave a pending
     * deferred bind behind, so the view is set to "nothing bound" straight after. */
    show_detail(0u);

    s_view = VIEW_COUNT;
    s_view_gen++;

    s_view_task = xTaskCreateStatic(view_task, "SysView",
                                    (uint32_t)(sizeof s_view_stack / sizeof s_view_stack[0]),
                                    NULL, 2u, s_view_stack, &s_view_tcb);
}

/* Only the view on the panel may answer taps. Both roots stay attached to their Legato
 * layers for their whole life and leInput walks every attached layer, so the off-screen
 * view is still in the pick walk — what keeps it out is LE_WIDGET_ENABLED, which the walk
 * requires. Visibility plays no part in this now that the views are separate layers. */
void ScreenSystem_SetInput(bool on)
{
    for (unsigned i = 0u; i < (unsigned)VIEW_COUNT; i++)
    {
        if (s_root[i] == NULL) { continue; }

        if (on && i == (unsigned)s_view) { s_root[i]->flags |=  LE_WIDGET_ENABLED; }
        else                             { s_root[i]->flags &= ~LE_WIDGET_ENABLED; }
    }
}

void ScreenSystem_SetShown(bool shown)
{
    /* Re-entering lands on the grid: leaving from a node detail via the drawer and
     * coming back to that same detail would be a confusing place to arrive.
     *
     * Leaving must drop the photo layer explicitly (OVR1 belongs to whoever is on screen,
     * and the next base view hands it to the video frame overlay) and forget which view
     * was bound; bind_view is unconditional so the next show re-binds regardless, and
     * same-view guard — the canvas is no longer on BASE by then. */
    s_shown = shown;

    if (shown)
    {
        bind_view(VIEW_OVERVIEW);
    }
    else
    {
        UiManager_NodePhotoHide();
        ScreenSystem_SetInput(false);
        s_view = VIEW_COUNT;
    }
}
