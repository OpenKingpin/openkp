/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2008 Andrey Nazarov
Copyright (C) 2026 OpenKP contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "ui.h"
#include "../client.h"
#include "common/cvar.h"

#define KP_VIRTUAL_WIDTH   640
#define KP_VIRTUAL_HEIGHT  480

extern cvar_t *timescale;

static struct {
    bool initialized;
    qhandle_t backtile;
    qhandle_t background;
    qhandle_t main_tv;
    qhandle_t logo;
    qhandle_t quit;
    struct {
        qhandle_t normal;
        qhandle_t hover;
    } items[4];
} kp_media;

static menuFrameWork_t kp_main;
static menuFrameWork_t kp_gang;
static menuFrameWork_t kp_demo;
static menuFrameWork_t kp_options;
static menuFrameWork_t kp_quit;

static menuSound_t KpCmd(menuCommon_t *self)
{
    Cbuf_AddText(&cmd_buffer, ((menuAction_t *)self)->cmd);
    Cbuf_AddText(&cmd_buffer, "\n");
    return QMS_MOVE;
}

static menuSound_t KpBackCmd(menuCommon_t *self)
{
    UI_PopMenu();
    return QMS_OUT;
}

static menuSound_t KpDisconnectCmd(menuCommon_t *self)
{
    Cbuf_AddText(&cmd_buffer, "disconnect\n");
    return QMS_OUT;
}

static menuSound_t KpDemoTogglePauseCmd(menuCommon_t *self)
{
    if (cls.demo.playback) {
        Cbuf_AddText(&cmd_buffer, "pause\n");
    } else if (cls.demo.recording) {
        Cbuf_AddText(&cmd_buffer, "suspend\n");
    } else {
        Com_Printf("No demo playback or recording active.\n");
        return QMS_BEEP;
    }
    return QMS_MOVE;
}

static menuSound_t KpDemoCycleSpeedCmd(menuCommon_t *self)
{
    float cur = timescale ? timescale->value : 1.0f;
    float next;

    if (cur < 0.35f) {
        next = 0.5f;
    } else if (cur < 0.75f) {
        next = 1.0f;
    } else if (cur < 1.5f) {
        next = 2.0f;
    } else if (cur < 3.0f) {
        next = 4.0f;
    } else {
        next = 0.25f;
    }

    Cvar_Set("timescale", va("%.2f", next));
    Com_Printf("Demo playback speed: %.2fx\n", next);
    return QMS_MOVE;
}

static menuSound_t KpDemoRecordOrStopCmd(menuCommon_t *self)
{
    if (cls.demo.playback || cls.demo.recording) {
        Cbuf_AddText(&cmd_buffer, "stop\n");
        return QMS_MOVE;
    }

    if (cls.state != ca_active) {
        Com_Printf("Must be in an active match to record a demo.\n");
        return QMS_BEEP;
    }

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char name[64];
    if (tm) {
        strftime(name, sizeof(name), "kp_%Y%m%d_%H%M%S", tm);
    } else {
        Q_snprintf(name, sizeof(name), "kp_%d", cls.realtime);
    }

    Cbuf_AddText(&cmd_buffer, va("record \"%s\"\n", name));
    UI_ForceMenuOff();
    return QMS_SILENT;
}

static menuSound_t KpItemFocus(menuCommon_t *self, bool gain)
{
    if (gain)
        return QMS_IN;
    return QMS_NOTHANDLED;
}

typedef struct {
    menuAction_t action;
    const char *desc;
} kpMenuItem_t;

static menuAction_t kp_main_items[] = {
    { .generic = { .type = MTYPE_ACTION, .name = "gang bang", .activate = KpCmd, .focus = KpItemFocus, .flags = QMF_HASFOCUS },
        .cmd = "pushmenu kpgang" },
    { .generic = { .type = MTYPE_ACTION, .name = "options", .activate = KpCmd, .focus = KpItemFocus },
        .cmd = "pushmenu kpoptions" },
    { .generic = { .type = MTYPE_ACTION, .name = "visuals", .activate = KpCmd, .focus = KpItemFocus },
        .cmd = "pushmenu video" },
    { .generic = { .type = MTYPE_ACTION, .name = "exit", .activate = KpCmd, .focus = KpItemFocus },
        .cmd = "pushmenu kpquit" },
};

static kpMenuItem_t kp_gang_items[] = {
    { .action = { .generic = { .type = MTYPE_ACTION, .name = "JOIN NETWORK SERVER", .activate = KpCmd, .focus = KpItemFocus, .flags = QMF_HASFOCUS },
                  .cmd = "pushmenu servers" },
      .desc = "SEARCH INTERNET MASTER LIST AND LOCAL LAN SERVERS" },
    { .action = { .generic = { .type = MTYPE_ACTION, .name = "PLAYER SETUP", .activate = KpCmd, .focus = KpItemFocus },
                  .cmd = "pushmenu players" },
      .desc = "CUSTOMIZE PLAYER NAME, THUG MODEL, AND GANG SKIN" },
    { .action = { .generic = { .type = MTYPE_ACTION, .name = "DEMO CONTROLS", .activate = KpCmd, .focus = KpItemFocus },
                  .cmd = "pushmenu kpdemo" },
      .desc = "MATCH PLAYBACK, REWIND, SPEED, AND RECORDING" },
    { .action = { .generic = { .type = MTYPE_ACTION, .name = "DOWNLOAD OPTIONS", .activate = KpCmd, .focus = KpItemFocus },
                  .cmd = "pushmenu downloads" },
      .desc = "CONFIGURE AUTOMATIC MAP, TEXTURE, AND SOUND DOWNLOADS" },
    { .action = { .generic = { .type = MTYPE_ACTION, .name = "DISCONNECT FROM SERVER", .activate = KpDisconnectCmd, .focus = KpItemFocus },
                  .cmd = "" },
      .desc = "LEAVE CURRENT MULTIPLAYER SERVER AND RETURN TO TITLE" },
    { .action = { .generic = { .type = MTYPE_ACTION, .name = "BACK", .activate = KpBackCmd, .focus = KpItemFocus },
                  .cmd = "" },
      .desc = "RETURN TO MAIN MENU" },
};

static kpMenuItem_t kp_demo_items[] = {
    { .action = { .generic = { .type = MTYPE_ACTION, .name = "BROWSE DEMOS", .activate = KpCmd, .focus = KpItemFocus, .flags = QMF_HASFOCUS },
                  .cmd = "pushmenu demos" },
      .desc = "OPEN DEMO FILE BROWSER TO PLAY RECORDED MATCHES" },
    { .action = { .generic = { .type = MTYPE_ACTION, .name = "PLAY / PAUSE", .activate = KpDemoTogglePauseCmd, .focus = KpItemFocus },
                  .cmd = "" },
      .desc = "PAUSE OR RESUME ACTIVE DEMO PLAYBACK OR RECORDING" },
    { .action = { .generic = { .type = MTYPE_ACTION, .name = "REWIND 10 SECONDS", .activate = KpCmd, .focus = KpItemFocus },
                  .cmd = "seek -10" },
      .desc = "STEP BACKWARD 10 SECONDS IN ACTIVE PLAYBACK" },
    { .action = { .generic = { .type = MTYPE_ACTION, .name = "FAST FORWARD 10 SECONDS", .activate = KpCmd, .focus = KpItemFocus },
                  .cmd = "seek +10" },
      .desc = "JUMP FORWARD 10 SECONDS IN ACTIVE PLAYBACK" },
    { .action = { .generic = { .type = MTYPE_ACTION, .name = "CYCLE PLAYBACK SPEED", .activate = KpDemoCycleSpeedCmd, .focus = KpItemFocus },
                  .cmd = "" },
      .desc = "CYCLE PLAYBACK SPEED: 0.25X, 0.5X, 1.0X, 2.0X, 4.0X" },
    { .action = { .generic = { .type = MTYPE_ACTION, .name = "TOGGLE PROGRESS BAR", .activate = KpCmd, .focus = KpItemFocus },
                  .cmd = "toggle scr_demobar 0 1 2" },
      .desc = "CYCLE TIMELINE AND FRAME PROGRESS OVERLAY" },
    { .action = { .generic = { .type = MTYPE_ACTION, .name = "RESTART DEMO", .activate = KpCmd, .focus = KpItemFocus },
                  .cmd = "seek 0" },
      .desc = "RESTART CURRENT DEMO FROM THE FIRST FRAME" },
    { .action = { .generic = { .type = MTYPE_ACTION, .name = "QUICK RECORD / STOP", .activate = KpDemoRecordOrStopCmd, .focus = KpItemFocus },
                  .cmd = "" },
      .desc = "TIMESTAMP RECORD CURRENT MATCH OR HALT ACTIVE DEMO" },
    { .action = { .generic = { .type = MTYPE_ACTION, .name = "BACK", .activate = KpBackCmd, .focus = KpItemFocus },
                  .cmd = "" },
      .desc = "RETURN TO PREVIOUS MENU" },
};

static kpMenuItem_t kp_option_items[] = {
    { .action = { .generic = { .type = MTYPE_ACTION, .name = "CONTROLS", .activate = KpCmd, .focus = KpItemFocus, .flags = QMF_HASFOCUS },
                  .cmd = "pushmenu keys" },
      .desc = "CUSTOMIZE MOVEMENT, WEAPONS, AND ACTION KEY BINDINGS" },
    { .action = { .generic = { .type = MTYPE_ACTION, .name = "MOUSE & INPUT", .activate = KpCmd, .focus = KpItemFocus },
                  .cmd = "pushmenu input" },
      .desc = "ADJUST MOUSE SENSITIVITY, FREELOOK, AND INPUT FILTERING" },
    { .action = { .generic = { .type = MTYPE_ACTION, .name = "AUDIO & SOUND", .activate = KpCmd, .focus = KpItemFocus },
                  .cmd = "pushmenu sound" },
      .desc = "CONFIGURE SOUND VOLUME, CYPRESS HILL MUSIC, AND DRIVERS" },
    { .action = { .generic = { .type = MTYPE_ACTION, .name = "BACK", .activate = KpBackCmd, .focus = KpItemFocus },
                  .cmd = "" },
      .desc = "RETURN TO MAIN MENU" },
};

static void KpInitMedia(void)
{
    static const char *names[4] = {
        "m_gang",
        "m_option",
        "m_visuals",
        "m_exit"
    };

    kp_media.backtile   = R_RegisterImage("backtile", IT_PIC, IF_PERMANENT | IF_REPEAT | IF_OPTIONAL);
    kp_media.background = R_RegisterImage("backround", IT_PIC, IF_PERMANENT | IF_OPTIONAL);
    kp_media.main_tv    = R_RegisterImage("main", IT_PIC, IF_PERMANENT | IF_OPTIONAL);
    kp_media.logo       = R_RegisterImage("kingpin", IT_PIC, IF_PERMANENT | IF_OPTIONAL);
    kp_media.quit       = R_RegisterImage("quit", IT_PIC, IF_PERMANENT | IF_OPTIONAL);

    for (int i = 0; i < 4; i++) {
        kp_media.items[i].normal = R_RegisterImage(names[i], IT_PIC, IF_PERMANENT | IF_OPTIONAL);
        kp_media.items[i].hover  = R_RegisterImage(va("%s2", names[i]), IT_PIC, IF_PERMANENT | IF_OPTIONAL);
    }

    kp_media.initialized = true;
}

static float KpGetUIScale(void)
{
    float s = (float)uis.height / 720.0f;
    if (s < 0.75f)
        s = 0.75f;
    if (s > 2.5f)
        s = 2.5f;
    return s;
}

static void KpGetScale(float *scale, int *base_x, int *base_y, int *canvas_w, int *canvas_h)
{
    float sx = (float)uis.width / (float)KP_VIRTUAL_WIDTH;
    float sy = (float)uis.height / (float)KP_VIRTUAL_HEIGHT;
    float s = min(sx, sy);

    if (s <= 0.0f)
        s = 1.0f;

    *scale = s;
    *canvas_w = Q_rint((float)KP_VIRTUAL_WIDTH * s);
    *canvas_h = Q_rint((float)KP_VIRTUAL_HEIGHT * s);
    *base_x = (uis.width - *canvas_w) / 2;
    *base_y = (uis.height - *canvas_h) / 2;
}

/*
=================
KpMainSize
=================
*/
static void KpMainSize(menuFrameWork_t *menu)
{
    static const struct {
        int x, y, w, h;
    } orig_coords[4] = {
        { 64,  105, 198, 28 }, /* m_gang */
        { 56,  155, 194, 26 }, /* m_option */
        { 100, 201, 171, 22 }, /* m_visuals */
        { 98,  237, 113, 26 }, /* m_exit */
    };

    float scale;
    int base_x, base_y, canvas_w, canvas_h;

    KpGetScale(&scale, &base_x, &base_y, &canvas_w, &canvas_h);

    for (int i = 0; i < 4; i++) {
        menuCommon_t *item = (menuCommon_t *)&kp_main_items[i];
        item->x = base_x + Q_rint((float)orig_coords[i].x * scale);
        item->y = base_y + Q_rint((float)orig_coords[i].y * scale);
        item->width = Q_rint((float)orig_coords[i].w * scale);
        item->height = Q_rint((float)orig_coords[i].h * scale);
        item->rect.x = item->x;
        item->rect.y = item->y;
        item->rect.width = item->width;
        item->rect.height = item->height;
    }

    menu->y1 = 0;
    menu->y2 = uis.height;
}

/*
=================
KpMainDraw
=================
*/
static void KpMainDraw(menuFrameWork_t *menu)
{
    float scale;
    int base_x, base_y, canvas_w, canvas_h;

    KpMainSize(menu);
    KpGetScale(&scale, &base_x, &base_y, &canvas_w, &canvas_h);

    /* 1. Backdrop: solid black across full viewport (clean letterboxing/pillarboxing) */
    R_DrawFill32(0, 0, uis.width, uis.height, MakeColor(0, 0, 0, 255));

    /* 2. Authentic 640x480 Kingpin brick canvas */
    if (kp_media.background) {
        R_DrawStretchPic(base_x, base_y, canvas_w, canvas_h, kp_media.background);
    }

    /* 3. The Surveillance TV Monitor at (0, 0) inside 640x480 space */
    if (kp_media.main_tv) {
        int tv_w = Q_rint(347.0f * scale);
        int tv_h = Q_rint(337.0f * scale);
        R_DrawStretchPic(base_x, base_y, tv_w, tv_h, kp_media.main_tv);
    }

    /* 4. Kingpin stencil logo at (373, 349) */
    if (kp_media.logo) {
        int logo_x = base_x + Q_rint(373.0f * scale);
        int logo_y = base_y + Q_rint(349.0f * scale);
        int logo_w = Q_rint(226.0f * scale);
        int logo_h = Q_rint(127.0f * scale);
        R_DrawStretchPic(logo_x, logo_y, logo_w, logo_h, kp_media.logo);
    }

    /* 5. Menu action buttons inside the CRT screen */
    for (int i = 0; i < 4; i++) {
        menuAction_t *a = &kp_main_items[i];
        bool focused = (a->generic.flags & QMF_HASFOCUS) != 0;
        qhandle_t pic = focused ? kp_media.items[i].hover : kp_media.items[i].normal;

        if (pic) {
            R_DrawStretchPic(a->generic.x, a->generic.y,
                             a->generic.width, a->generic.height, pic);
        } else {
            /* Fallback font rendering if graphic is missing */
            if (focused) {
                R_SetColor(MakeColor(186, 255, 208, 255));
            } else {
                R_SetColor(MakeColor(78, 138, 107, 255));
            }
            R_DrawSizedString(a->generic.x, a->generic.y,
                              a->generic.height, a->generic.height,
                              0, a->generic.name, uis.fontHandle);
            R_ClearColor();
        }
    }

    /* 6. In-game return footer prompt */
    if (cls.state >= ca_active) {
        float s = KpGetUIScale();
        int footer_h = max(24, Q_rint(32.0f * s));
        int foot_size = max(8, Q_rint(10.0f * s));
        int fy = uis.height - footer_h;

        R_DrawFill32(0, fy, uis.width, footer_h, MakeColor(10, 8, 6, 220));
        R_DrawFill32(0, fy, uis.width, 1, MakeColor(65, 54, 40, 220));

        static const char *foot_text = "[ESC] RETURN TO MATCH      [MOUSE2] RESUME";
        int fw = (int)strlen(foot_text) * foot_size;
        int fx = (uis.width - fw) / 2;

        R_SetColor(MakeColor(240, 215, 130, 255));
        R_DrawSizedString(fx, fy + (footer_h - foot_size) / 2, foot_size, foot_size, 0, foot_text, uis.fontHandle);
        R_ClearColor();
    }
}

/*
=================
KpMainKey
=================
*/
static menuSound_t KpMainKey(menuFrameWork_t *menu, int key)
{
    if (cls.state >= ca_active) {
        if (key == K_ESCAPE || key == K_MOUSE2) {
            UI_PopMenu();
            return QMS_OUT;
        }
        if (key == K_MOUSE1) {
            float s = KpGetUIScale();
            int footer_h = max(24, Q_rint(32.0f * s));
            if (uis.mouseCoords[1] >= uis.height - footer_h) {
                UI_PopMenu();
                return QMS_OUT;
            }
        }
        return QMS_NOTHANDLED;
    }

    if (key == K_ESCAPE) {
        UI_PushMenu(&kp_quit);
        return QMS_OUT;
    }

    if (key == K_MOUSE2) {
        return QMS_SILENT;
    }

    return QMS_NOTHANDLED;
}

/*
=================
KpQuitDraw
=================
*/
static void KpQuitDraw(menuFrameWork_t *menu)
{
    float scale;
    int base_x, base_y, canvas_w, canvas_h;

    /* Draw parent main menu or background underneath */
    KpMainDraw(&kp_main);

    /* Dim screen with dark overlay */
    R_DrawFill32(0, 0, uis.width, uis.height, MakeColor(0, 0, 0, 180));

    KpGetScale(&scale, &base_x, &base_y, &canvas_w, &canvas_h);

    /* Authentic Kingpin quit image centered */
    if (kp_media.quit) {
        int qw = Q_rint(205.0f * scale);
        int qh = Q_rint(37.0f * scale);
        int qx = (uis.width - qw) / 2;
        int qy = (uis.height - qh) / 2;
        R_DrawStretchPic(qx, qy, qw, qh, kp_media.quit);

        float s = KpGetUIScale();
        int hint_size = max(8, Q_rint(9.5f * s));
        int hint_y = qy + qh + Q_rint(12.0f * s);
        static const char *hint_text = "[Y] YES      [N / ESC] CANCEL";
        int hw = (int)strlen(hint_text) * hint_size;
        int hx = (uis.width - hw) / 2;

        R_SetColor(MakeColor(240, 215, 130, 255));
        R_DrawSizedString(hx, hint_y, hint_size, hint_size, 0, hint_text, uis.fontHandle);
        R_ClearColor();
    } else {
        UI_DrawString(uis.width / 2, uis.height / 2 - CONCHAR_HEIGHT,
                      UI_CENTER | UI_ALTCOLOR, "QUIT OpenKP?");
        UI_DrawString(uis.width / 2, uis.height / 2 + CONCHAR_HEIGHT,
                      UI_CENTER, "Y: YES    N: NO");
    }
}

static menuSound_t KpQuitKey(menuFrameWork_t *menu, int key)
{
    switch (key) {
    case 'y':
    case 'Y':
    case K_KP_ENTER:
        Cbuf_AddText(&cmd_buffer, "quit\n");
        return QMS_OUT;
    case 'n':
    case 'N':
    case K_ESCAPE:
    case K_MOUSE2:
        UI_PopMenu();
        return QMS_OUT;
    default:
        return QMS_NOTHANDLED;
    }
}

/*
===================================================================

SUBMENUS (Gang Bang, Demos, Options)

===================================================================
*/

static void KpSubmenuGetMetrics(menuFrameWork_t *menu, float *out_s, int *out_card_w, int *out_card_h,
                                int *out_card_gap, int *out_logo_w, int *out_logo_h,
                                int *out_header_h, int *out_start_y)
{
    float s = KpGetUIScale();

    int card_w = min(uis.width - 40, Q_rint(540.0f * s));
    int card_h = Q_rint(54.0f * s);
    int card_gap = Q_rint(12.0f * s);

    int logo_w = Q_rint(160.0f * s);
    int logo_h = Q_rint((float)logo_w * (127.0f / 226.0f));
    int header_pad = Q_rint(65.0f * s);
    int footer_h = Q_rint(36.0f * s);

    int avail_h = uis.height - footer_h - Q_rint(24.0f * s);
    int total_needed = (logo_h + header_pad) + menu->nitems * card_h + (menu->nitems - 1) * card_gap;

    if (total_needed > avail_h && menu->nitems > 0) {
        float factor = (float)avail_h / (float)total_needed;
        if (factor < 1.0f) {
            if (factor < 0.52f)
                factor = 0.52f;
            card_h = max(26, Q_rint((float)card_h * factor));
            card_gap = max(3, Q_rint((float)card_gap * factor));
            logo_w = max(80, Q_rint((float)logo_w * factor));
            logo_h = Q_rint((float)logo_w * (127.0f / 226.0f));
            header_pad = max(28, Q_rint((float)header_pad * factor));
        }
    }

    int header_h = logo_h + header_pad;
    int total_cards_h = menu->nitems * card_h + (menu->nitems - 1) * card_gap;
    int start_y = (uis.height - footer_h - (header_h + total_cards_h)) / 2;
    if (start_y < Q_rint(12.0f * s))
        start_y = Q_rint(12.0f * s);

    *out_s = s;
    *out_card_w = card_w;
    *out_card_h = card_h;
    *out_card_gap = card_gap;
    *out_logo_w = logo_w;
    *out_logo_h = logo_h;
    *out_header_h = header_h;
    *out_start_y = start_y;
}

static void KpSubmenuSize(menuFrameWork_t *menu)
{
    float s;
    int card_w, card_h, card_gap, logo_w, logo_h, header_h, start_y;

    KpSubmenuGetMetrics(menu, &s, &card_w, &card_h, &card_gap, &logo_w, &logo_h, &header_h, &start_y);

    int cards_y = start_y + header_h;
    int card_x = (uis.width - card_w) / 2;

    for (int i = 0; i < menu->nitems; i++) {
        menuCommon_t *item = menu->items[i];
        item->x = card_x;
        item->y = cards_y + i * (card_h + card_gap);
        item->width = card_w;
        item->height = card_h;
        item->rect.x = item->x;
        item->rect.y = item->y;
        item->rect.width = item->width;
        item->rect.height = item->height;
    }

    menu->y1 = 0;
    menu->y2 = uis.height;
}

static void KpSubmenuDraw(menuFrameWork_t *menu)
{
    float s;
    int card_w, card_h, card_gap, logo_w, logo_h, header_h, start_y;

    KpSubmenuSize(menu);
    KpSubmenuGetMetrics(menu, &s, &card_w, &card_h, &card_gap, &logo_w, &logo_h, &header_h, &start_y);

    /* 1. Black backing for pillarboxes and background */
    R_DrawFill32(0, 0, uis.width, uis.height, MakeColor(0, 0, 0, 255));
    if (kp_media.background) {
        R_DrawKeepAspectPic(0, 0, uis.width, uis.height, kp_media.background);
        R_DrawFill32(0, 0, uis.width, uis.height, MakeColor(0, 0, 0, 150));
    }

    /* 2. Top Header */
    int center_x = uis.width / 2;

    /* Stencil logo */
    if (kp_media.logo) {
        int lx = center_x - logo_w / 2;
        int ly = start_y;
        R_DrawStretchPic(lx, ly, logo_w, logo_h, kp_media.logo);
    }

    /* Category title & subtitle */
    int title_y = start_y + logo_h + Q_rint(8.0f * s);
    int title_size = max(10, Q_rint(16.0f * s));
    int sub_size = max(8, Q_rint(9.5f * s));

    if (menu->title) {
        char title_upper[64];
        Q_strlcpy(title_upper, menu->title, sizeof(title_upper));
        Q_strupr(title_upper);

        int tw = (int)strlen(title_upper) * title_size;
        int tx = center_x - tw / 2;

        /* Title shadow + glow */
        R_SetColor(MakeColor(12, 10, 8, 255));
        R_DrawSizedString(tx + 2, title_y + 2, title_size, title_size, 0, title_upper, uis.fontHandle);

        R_SetColor(MakeColor(240, 216, 120, 255));
        R_DrawSizedString(tx, title_y, title_size, title_size, 0, title_upper, uis.fontHandle);
        R_ClearColor();
    }

    const char *subtitle = menu->status;
    char dyn_status[64];
    if (menu == &kp_demo) {
        if (cls.demo.playback) {
            float spd = timescale ? timescale->value : 1.0f;
            Q_snprintf(dyn_status, sizeof(dyn_status), "// PLAYBACK ACTIVE [%.2fX]", spd);
            subtitle = dyn_status;
        } else if (cls.demo.recording) {
            Q_snprintf(dyn_status, sizeof(dyn_status), "// RECORDING MATCH%s", cls.demo.paused ? " (PAUSED)" : "");
            subtitle = dyn_status;
        }
    }

    if (subtitle) {
        int sw = (int)strlen(subtitle) * sub_size;
        int sx = center_x - sw / 2;
        int sy = title_y + title_size + Q_rint(5.0f * s);

        R_SetColor(MakeColor(150, 140, 125, 255));
        R_DrawSizedString(sx, sy, sub_size, sub_size, 0, subtitle, uis.fontHandle);
        R_ClearColor();
    }

    /* Sleek divider line with gold center accent */
    int div_y = title_y + title_size + sub_size + Q_rint(10.0f * s);
    int div_x1 = center_x - card_w / 2;
    R_DrawFill32(div_x1, div_y, card_w, 1, MakeColor(55, 48, 38, 200));

    int accent_w = Q_rint(120.0f * s);
    R_DrawFill32(center_x - accent_w / 2, div_y, accent_w, 2, MakeColor(230, 195, 115, 255));

    /* 3. Render Cards */
    int font_item_size = max(8, min(card_h / 2 - 2, Q_rint(12.0f * s)));
    int font_desc_size = max(7, min(card_h / 2 - 4, Q_rint(8.5f * s)));

    for (int i = 0; i < menu->nitems; i++) {
        kpMenuItem_t *item = (kpMenuItem_t *)menu->items[i];
        bool focused = (item->action.generic.flags & QMF_HASFOCUS) != 0;

        int ix = item->action.generic.x;
        int iy = item->action.generic.y;
        int iw = item->action.generic.width;
        int ih = item->action.generic.height;

        int tab_w = focused ? max(4, Q_rint(5.0f * s)) : max(2, Q_rint(3.0f * s));
        int text_x = ix + Q_rint(24.0f * s);
        int t_y = iy + (ih - font_item_size - (item->desc ? (font_desc_size + 3) : 0)) / 2;
        if (t_y < iy + 2)
            t_y = iy + 2;
        int d_y = t_y + font_item_size + 3;

        if (focused) {
            /* Focused card: warm dark slat, glowing border, amber left tab */
            R_DrawFill32(ix, iy, iw, ih, MakeColor(48, 38, 25, 220));
            R_DrawFill32(ix, iy, iw, 1, MakeColor(240, 210, 140, 180));        /* top */
            R_DrawFill32(ix, iy + ih - 1, iw, 1, MakeColor(15, 12, 10, 255));    /* bottom */
            R_DrawFill32(ix + iw - 1, iy, 1, ih, MakeColor(180, 145, 75, 200));  /* right */
            R_DrawFill32(ix, iy, tab_w, ih, MakeColor(230, 195, 115, 255));      /* left tab */

            /* Arrow indicator */
            R_SetColor(MakeColor(230, 195, 115, 255));
            R_DrawSizedString(ix + Q_rint(10.0f * s), t_y, font_item_size, font_item_size, 0, ">", uis.fontHandle);

            /* Title with shadow */
            R_SetColor(MakeColor(15, 12, 8, 255));
            R_DrawSizedString(text_x + 1, t_y + 1, font_item_size, font_item_size, 0, item->action.generic.name, uis.fontHandle);
            R_SetColor(MakeColor(255, 250, 235, 255));
            R_DrawSizedString(text_x, t_y, font_item_size, font_item_size, 0, item->action.generic.name, uis.fontHandle);

            /* Description */
            if (item->desc) {
                R_SetColor(MakeColor(225, 195, 130, 255));
                R_DrawSizedString(text_x, d_y, font_desc_size, font_desc_size, 0, item->desc, uis.fontHandle);
            }
        } else {
            /* Unfocused card: smoked dark acrylic, fine dark bronze border */
            R_DrawFill32(ix, iy, iw, ih, MakeColor(16, 14, 11, 150));
            R_DrawFill32(ix, iy, iw, 1, MakeColor(48, 42, 34, 160));
            R_DrawFill32(ix, iy + ih - 1, iw, 1, MakeColor(20, 17, 14, 200));
            R_DrawFill32(ix + iw - 1, iy, 1, ih, MakeColor(48, 42, 34, 160));
            R_DrawFill32(ix, iy, tab_w, ih, MakeColor(70, 60, 48, 180));

            /* Title */
            R_SetColor(MakeColor(185, 175, 160, 255));
            R_DrawSizedString(text_x, t_y, font_item_size, font_item_size, 0, item->action.generic.name, uis.fontHandle);

            /* Description */
            if (item->desc) {
                R_SetColor(MakeColor(120, 112, 100, 255));
                R_DrawSizedString(text_x, d_y, font_desc_size, font_desc_size, 0, item->desc, uis.fontHandle);
            }
        }
        R_ClearColor();
    }

    /* 4. Footer navigation prompt */
    int footer_h = Q_rint(36.0f * s);
    int foot_size = max(8, Q_rint(9.5f * s));
    int fy = uis.height - footer_h;

    R_DrawFill32(0, fy, uis.width, footer_h, MakeColor(10, 8, 6, 210));
    R_DrawFill32(0, fy, uis.width, 1, MakeColor(65, 54, 40, 220));

    static const char *foot_text = "[ENTER] SELECT      [UP / DOWN] NAVIGATE      [ESC] BACK";
    int fw = (int)strlen(foot_text) * foot_size;
    int fx = (uis.width - fw) / 2;

    R_SetColor(MakeColor(185, 165, 125, 255));
    R_DrawSizedString(fx, fy + (footer_h - foot_size) / 2, foot_size, foot_size, 0, foot_text, uis.fontHandle);
    R_ClearColor();
}

static menuSound_t KpSubmenuKey(menuFrameWork_t *menu, int key)
{
    if (key == K_ESCAPE || key == K_MOUSE2) {
        UI_PopMenu();
        return QMS_OUT;
    }

    return QMS_NOTHANDLED;
}

static void KpListFree(menuFrameWork_t *menu)
{
    Z_Free(menu->items);
    menu->items = NULL;
    menu->nitems = 0;
}

static void KpAddMenuItems(menuFrameWork_t *menu, kpMenuItem_t *items, int count)
{
    for (int i = 0; i < count; i++)
        Menu_AddItem(menu, &items[i].action);
}

static void KpAddActions(menuFrameWork_t *menu, menuAction_t *items, int count)
{
    for (int i = 0; i < count; i++)
        Menu_AddItem(menu, &items[i]);
}

static void KpDropMenu(const char *name)
{
    menuFrameWork_t *old = UI_FindMenu(name);

    if (!old)
        return;

    List_Remove(&old->entry);
    if (old->free)
        old->free(old);
}

static void KpBind(menuFrameWork_t *menu, const char *name, const char *title,
                   const char *status, kpMenuItem_t *items, int count)
{
    menu->name = (char *)name;
    menu->title = (char *)title;
    menu->status = (char *)status;
    menu->draw = KpSubmenuDraw;
    menu->size = KpSubmenuSize;
    menu->keydown = KpSubmenuKey;
    menu->free = KpListFree;
    menu->transparent = false;
    if (items)
        KpAddMenuItems(menu, items, count);
}

/*
=================
M_Menu_Kingpin
=================
*/
void M_Menu_Kingpin(void)
{
    KpInitMedia();

    KpDropMenu("main");
    KpDropMenu("game");

    /* Setup main menu */
    memset(&kp_main, 0, sizeof(kp_main));
    kp_main.name = "main";
    kp_main.draw = KpMainDraw;
    kp_main.size = KpMainSize;
    kp_main.keydown = KpMainKey;
    kp_main.free = KpListFree;
    kp_main.transparent = false;
    KpAddActions(&kp_main, kp_main_items, q_countof(kp_main_items));

    /* Setup submenus */
    KpBind(&kp_gang, "kpgang", "gang bang", "// MULTIPLAYER OPERATIONS",
           kp_gang_items, q_countof(kp_gang_items));
    KpBind(&kp_demo, "kpdemo", "demo theater", "// MATCH PLAYBACK & RECORDING",
           kp_demo_items, q_countof(kp_demo_items));
    KpBind(&kp_options, "kpoptions", "options", "// SYSTEM CONFIGURATION",
           kp_option_items, q_countof(kp_option_items));

    /* Setup quit dialog */
    memset(&kp_quit, 0, sizeof(kp_quit));
    kp_quit.name = "kpquit";
    kp_quit.draw = KpQuitDraw;
    kp_quit.keydown = KpQuitKey;
    kp_quit.transparent = true;

    List_Append(&ui_menus, &kp_main.entry);
    List_Append(&ui_menus, &kp_gang.entry);
    List_Append(&ui_menus, &kp_demo.entry);
    List_Append(&ui_menus, &kp_options.entry);
    List_Append(&ui_menus, &kp_quit.entry);
}
