#include "ui/widgets/song_list/widget_song_list_demo.h"

#include <stdio.h>

#include "ui/widgets/song_list/widget_song_list.h"
#include "game/catalog.h"
#include "log.h"

#include "gfx/legato/generated/le_gen_assets.h"
#include "gfx/legato/generated/screen/le_gen_screen_Dashboard.h"

static leWidget *s_list = NULL;

static bool demo_row(void *ctx, int index, songlist_row_t *out)
{
    (void)ctx;
    const catalog_entry_t *e = Catalog_At(index);
    static char dur[8];

    if (e == NULL) { return false; }

    if (e->length_s != 0)
    {
        (void)snprintf(dur, sizeof(dur), "%u:%02u",
                       (unsigned)(e->length_s / 60u), (unsigned)(e->length_s % 60u));
    }
    else { dur[0] = '\0'; }

    out->title      = e->title;
    out->artist     = e->artist;
    out->right      = dur;
    out->badge      = "";        /* tier deferred — no badge yet */
    out->badgeColor = 0;
    return true;
}

static void demo_select(void *ctx, int index)
{
    (void)ctx;
    const catalog_entry_t *e = Catalog_At(index);
    LOG_INFO("songlist: selected #%d  %s - %s\r\n", index,
             (e != NULL) ? e->title : "?", (e != NULL) ? e->artist : "?");
}

void SongList_DemoAttach(void)
{
    leWidget *root;
    int n;

    if (s_list != NULL) { LOG_INFO("songlist: already attached\r\n"); return; }

    (void)Catalog_Reload();
    n = Catalog_Count();

    root = screenGetRoot_Dashboard(0);
    if (root == NULL) { LOG_WARN("songlist: Dashboard screen not shown\r\n"); return; }

    s_list = SongList_New();
    if (s_list == NULL) { LOG_WARN("songlist: widget alloc failed\r\n"); return; }

    s_list->fn->setPosition(s_list, 40, 80);
    s_list->fn->setSize(s_list, 560, 640);
    SongList_SetFonts(s_list,
                      (const leFont *)&NotoSans_Regular_20,
                      (const leFont *)&NotoSans_Regular_14,
                      (const leFont *)&figmaFont_Menlo_14);
    SongList_SetRowHeight(s_list, 64);
    SongList_SetModel(s_list, n, demo_row, NULL);
    SongList_SetSelectHandler(s_list, demo_select, NULL);

    root->fn->addChild(root, s_list);
    LOG_INFO("songlist: attached, %d song(s)\r\n", n);
}

void SongList_DemoDetach(void)
{
    if (s_list != NULL)
    {
        leWidget_Delete(s_list);   /* removes itself from its parent, then frees */
        s_list = NULL;
    }
    Dashboard_PANEL_BASE->fn->setVisible(Dashboard_PANEL_BASE, LE_TRUE);
}

void SongList_DemoSetDashboard(bool show)
{
    Dashboard_PANEL_BASE->fn->setVisible(Dashboard_PANEL_BASE, show ? LE_TRUE : LE_FALSE);
}

bool SongList_DemoToggleFill(void)
{
    static bool on = false;
    if (s_list == NULL) { return false; }
    on = !on;
    SongList_SetDebugFill(s_list, on);
    return on;
}

int SongList_DemoZOrder(int *out_count)
{
    leWidget *p;
    if (s_list == NULL) { if (out_count != NULL) { *out_count = 0; } return -1; }
    p = s_list->parent;
    if (p == NULL) { if (out_count != NULL) { *out_count = 0; } return -1; }
    if (out_count != NULL) { *out_count = (int)p->fn->getChildCount(p); }
    return (int)p->fn->getIndexOfChild(p, s_list);
}

bool SongList_DemoStats(uint32_t *draw_count, int *selected)
{
    if (s_list == NULL) { return false; }
    if (draw_count != NULL) { *draw_count = SongList_DrawCount(s_list); }
    if (selected != NULL)   { *selected = SongList_Selected(s_list); }
    return true;
}
