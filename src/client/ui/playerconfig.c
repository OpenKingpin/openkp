/*
Copyright (C) 2003-2008 Andrey Nazarov
Copyright (C) 2026 OpenKP developers

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
#include "client/client.h"
#include <math.h>

/*
=============================================================================

PLAYER CONFIG MENU (KINGPIN)

=============================================================================
*/

#define ID_MODEL 103
#define ID_HEAD  104
#define ID_BODY  105
#define ID_LEGS  106

typedef struct {
    menuFrameWork_t     menu;
    menuField_t         name;
    menuSpinControl_t   model;
    menuSpinControl_t   head;
    menuSpinControl_t   body;
    menuSpinControl_t   legs;
    menuSpinControl_t   hand;

    refdef_t    refdef;
    entity_t    entities[4]; /* 0: legs, 1: body, 2: head, 3: weapon */

    unsigned    time;
    unsigned    oldTime;

    char *pmnames[MAX_PLAYERMODELS + 1];
} m_player_t;

static m_player_t    m_player;

static const char *const handedness[] = {
    "right",
    "left",
    "center",
    NULL
};

static void ReloadMedia(void)
{
    char scratch[MAX_QPATH];
    char head_dir[MAX_QPATH], body_dir[MAX_QPATH], legs_dir[MAX_QPATH];
    playerModelInfo_t *pmi;
    const char *model, *h_code, *b_code, *l_code;

    if (!uis.numPlayerModels)
        return;

    pmi = &uis.pmi[m_player.model.curvalue];
    model = pmi->directory;
    h_code = (pmi->head_skins && pmi->head_skins[m_player.head.curvalue]) ? pmi->head_skins[m_player.head.curvalue] : "001";
    b_code = (pmi->body_skins && pmi->body_skins[m_player.body.curvalue]) ? pmi->body_skins[m_player.body.curvalue] : "001";
    l_code = (pmi->legs_skins && pmi->legs_skins[m_player.legs.curvalue]) ? pmi->legs_skins[m_player.legs.curvalue] : "001";

    CL_KingpinSkinDirs(model, head_dir, body_dir, legs_dir, sizeof(head_dir));

    m_player.refdef.num_entities = 0;

    /* 0: Legs */
    Q_concat(scratch, sizeof(scratch), "players/", model, "/legs.mdx");
    m_player.entities[0].model = R_RegisterModel(scratch);
    m_player.entities[0].skin = CL_RegisterKingpinSkin(model, legs_dir, "legs", l_code);
    m_player.entities[0].flags = RF_FULLBRIGHT;
    m_player.refdef.num_entities++;

    /* 1: Body */
    Q_concat(scratch, sizeof(scratch), "players/", model, "/body.mdx");
    m_player.entities[1].model = R_RegisterModel(scratch);
    m_player.entities[1].skin = CL_RegisterKingpinSkin(model, body_dir, "body", b_code);
    m_player.entities[1].flags = RF_FULLBRIGHT;
    m_player.refdef.num_entities++;

    /* 2: Head */
    Q_concat(scratch, sizeof(scratch), "players/", model, "/head.mdx");
    m_player.entities[2].model = R_RegisterModel(scratch);
    m_player.entities[2].skin = CL_RegisterKingpinSkin(model, head_dir, "head", h_code);
    m_player.entities[2].flags = RF_FULLBRIGHT;
    m_player.refdef.num_entities++;

    /* 3: Weapon (shotgun, tommygun, or default) */
    Q_concat(scratch, sizeof(scratch), "players/", model, "/w_shotgun.mdx");
    m_player.entities[3].model = R_RegisterModel(scratch);
    if (!m_player.entities[3].model) {
        Q_concat(scratch, sizeof(scratch), "players/", model, "/w_tommygun.mdx");
        m_player.entities[3].model = R_RegisterModel(scratch);
    }
    m_player.entities[3].skin = 0;
    m_player.entities[3].flags = RF_FULLBRIGHT;
    if (m_player.entities[3].model)
        m_player.refdef.num_entities++;
}

static void RunFrame(void)
{
    int frame;
    int i;

    if (m_player.time < uis.realtime) {
        m_player.oldTime = m_player.time;

        m_player.time += 100;
        if (m_player.time < uis.realtime) {
            m_player.time = uis.realtime;
        }

        /* Kingpin standard ready animation is 32 frames (0..31) */
        frame = (m_player.time / 100) % 32;

        for (i = 0; i < m_player.refdef.num_entities; i++) {
            m_player.entities[i].oldframe = m_player.entities[i].frame;
            m_player.entities[i].frame = frame;
        }
    }
}

static void Draw(menuFrameWork_t *self)
{
    float backlerp;
    int i;

    m_player.refdef.time = uis.realtime * 0.001f;

    RunFrame();

    if (m_player.time == m_player.oldTime) {
        backlerp = 0;
    } else {
        backlerp = 1 - (float)(uis.realtime - m_player.oldTime) /
                   (float)(m_player.time - m_player.oldTime);
    }

    /* Turntable rotation: gentle swing so the player sees front and sides */
    float yaw = 250.0f + 25.0f * sinf(uis.realtime * 0.0012f);
    for (i = 0; i < m_player.refdef.num_entities; i++) {
        m_player.entities[i].backlerp = backlerp;
        m_player.entities[i].angles[YAW] = yaw;
    }

    /* Background */
    if (uis.backgroundHandle) {
        R_DrawKeepAspectPic(0, 0, uis.width, uis.height, uis.backgroundHandle);
    } else {
        R_DrawFill32(0, 0, uis.width, uis.height, MakeColor(15, 12, 10, 240));
    }

    /* Left panel: controls backdrop */
    int panel_x = 24;
    int panel_y = 48;
    int panel_w = uis.width / 2 - 32;
    int panel_h = uis.height - 72;
    if (panel_w > 460)
        panel_w = 460;

    R_DrawFill32(panel_x, panel_y, panel_w, panel_h, MakeColor(18, 15, 12, 210));
    R_DrawFill32(panel_x, panel_y, panel_w, 2, MakeColor(184, 134, 28, 200));
    R_DrawFill32(panel_x, panel_y + panel_h - 1, panel_w, 1, MakeColor(55, 48, 38, 160));
    R_DrawFill32(panel_x, panel_y, 1, panel_h, MakeColor(55, 48, 38, 160));
    R_DrawFill32(panel_x + panel_w - 1, panel_y, 1, panel_h, MakeColor(55, 48, 38, 160));

    /* Panel header */
    UI_DrawString(panel_x + 16, panel_y + 14, UI_LEFT | UI_ALTCOLOR, "PLAYER IDENTITY // LOADOUT");
    R_DrawFill32(panel_x + 16, panel_y + 28, panel_w - 32, 1, MakeColor(55, 48, 38, 180));

    /* Right side: 3D model frame pedestal (in virtual UI coords) */
    int box_x = panel_x + panel_w + 24;
    int box_w = uis.width - box_x - 24;
    int box_y = 48;
    int box_h = uis.height - 72;

    R_DrawFill32(box_x, box_y, box_w, box_h, MakeColor(10, 8, 7, 140));
    R_DrawFill32(box_x, box_y, box_w, 1, MakeColor(55, 48, 38, 160));
    R_DrawFill32(box_x, box_y + box_h - 1, box_w, 1, MakeColor(55, 48, 38, 160));
    R_DrawFill32(box_x, box_y, 1, box_h, MakeColor(55, 48, 38, 160));
    R_DrawFill32(box_x + box_w - 1, box_y, 1, box_h, MakeColor(55, 48, 38, 160));

    /* Pedestal floor accent */
    int ped_y = box_y + box_h - 40;
    R_DrawFill32(box_x + 20, ped_y, box_w - 40, 2, MakeColor(184, 134, 28, 90));

    /* Model label at bottom of 3D preview */
    const char *cur_model = uis.pmi[m_player.model.curvalue].directory;
    UI_DrawString(box_x + box_w / 2, box_y + box_h - 24, UI_CENTER | UI_ALTCOLOR, cur_model ? cur_model : "");

    /* Header top status */
    UI_DrawString(24, 18, UI_LEFT | UI_ALTCOLOR, "OPENKP // PLAYER SETUP");
    UI_DrawString(uis.width - 24, 18, UI_RIGHT, "USE ARROWS / ENTER TO SELECT");

    /* Draw UI items */
    Menu_Draw(self);

    /* Render 3D character */
    R_RenderFrame(&m_player.refdef);

    R_SetScale(uis.scale);
}

static void Size(menuFrameWork_t *self)
{
    int sw = uis.width;
    int sh = uis.height;
    int panel_x = 24;
    int panel_w = sw / 2 - 32;
    if (panel_w > 460)
        panel_w = 460;

    int cur_y = 100;
    int ctrl_x = panel_x + 140;

    /* 3D Model viewport occupies right side (virtual UI coords) */
    int box_x = panel_x + panel_w + 24;
    int box_w = sw - box_x - 24;
    if (box_w < 200)
        box_w = 200;
    int box_y = 48;
    int box_h = sh - 72;

    /* Convert to unscaled framebuffer coords for R_RenderFrame */
    m_player.refdef.x = Q_rint(box_x / uis.scale);
    m_player.refdef.y = Q_rint(box_y / uis.scale);
    m_player.refdef.width = Q_rint(box_w / uis.scale);
    m_player.refdef.height = Q_rint(box_h / uis.scale);

    m_player.refdef.fov_x = 90;
    m_player.refdef.fov_y = V_CalcFov(m_player.refdef.fov_x,
                                      m_player.refdef.width, m_player.refdef.height);

    /* Form items on left */
    m_player.name.generic.x     = ctrl_x;
    m_player.name.generic.y     = cur_y;
    cur_y += MENU_SPACING + 14;

    m_player.model.generic.x    = ctrl_x;
    m_player.model.generic.y    = cur_y;
    cur_y += MENU_SPACING + 10;

    m_player.head.generic.x     = ctrl_x;
    m_player.head.generic.y     = cur_y;
    cur_y += MENU_SPACING + 10;

    m_player.body.generic.x     = ctrl_x;
    m_player.body.generic.y     = cur_y;
    cur_y += MENU_SPACING + 10;

    m_player.legs.generic.x     = ctrl_x;
    m_player.legs.generic.y     = cur_y;
    cur_y += MENU_SPACING + 10;

    m_player.hand.generic.x     = ctrl_x;
    m_player.hand.generic.y     = cur_y;
}

static menuSound_t Change(menuCommon_t *self)
{
    playerModelInfo_t *pmi;

    switch (self->id) {
    case ID_MODEL:
        pmi = &uis.pmi[m_player.model.curvalue];
        m_player.head.itemnames = pmi->head_skins;
        m_player.head.curvalue = 0;
        SpinControl_Init(&m_player.head);

        m_player.body.itemnames = pmi->body_skins;
        m_player.body.curvalue = 0;
        SpinControl_Init(&m_player.body);

        m_player.legs.itemnames = pmi->legs_skins;
        m_player.legs.curvalue = 0;
        SpinControl_Init(&m_player.legs);

        ReloadMedia();
        break;

    case ID_HEAD:
    case ID_BODY:
    case ID_LEGS:
        ReloadMedia();
        break;

    default:
        break;
    }
    return QMS_MOVE;
}

static void Pop(menuFrameWork_t *self)
{
    char scratch[MAX_QPATH];
    playerModelInfo_t *pmi;
    const char *h_code, *b_code, *l_code;

    if (!uis.numPlayerModels)
        return;

    pmi = &uis.pmi[m_player.model.curvalue];
    h_code = (pmi->head_skins && pmi->head_skins[m_player.head.curvalue]) ? pmi->head_skins[m_player.head.curvalue] : "001";
    b_code = (pmi->body_skins && pmi->body_skins[m_player.body.curvalue]) ? pmi->body_skins[m_player.body.curvalue] : "001";
    l_code = (pmi->legs_skins && pmi->legs_skins[m_player.legs.curvalue]) ? pmi->legs_skins[m_player.legs.curvalue] : "001";

    Cvar_SetEx("name", m_player.name.field.text, FROM_CONSOLE);

    Q_snprintf(scratch, sizeof(scratch), "%s/%s %s %s",
               pmi->directory, h_code, b_code, l_code);

    Cvar_SetEx("skin", scratch, FROM_CONSOLE);
    Cvar_SetEx("hand", va("%d", m_player.hand.curvalue), FROM_CONSOLE);
}

static bool Push(menuFrameWork_t *self)
{
    char currentskin[MAX_QPATH];
    char cur_model[MAX_QPATH] = "male_thug";
    char cur_head[MAX_QPATH]  = "009";
    char cur_body[MAX_QPATH]  = "019";
    char cur_legs[MAX_QPATH]  = "017";
    int i, j;
    int current_model_idx = 0;
    char *slash, *cursor;
    playerModelInfo_t *pmi;

    if (!uis.numPlayerModels) {
        PlayerModel_Load();
        if (!uis.numPlayerModels) {
            Com_Printf("No player models found.\n");
            return false;
        }
    }

    /* Parse skin cvar: "model/head body legs" */
    Cvar_VariableStringBuffer("skin", currentskin, sizeof(currentskin));
    slash = strchr(currentskin, '/');
    if (!slash)
        slash = strchr(currentskin, '\\');

    if (slash) {
        *slash = 0;
        Q_strlcpy(cur_model, currentskin, sizeof(cur_model));
        cursor = slash + 1;

        /* Extract head, body, legs tokens */
        while (*cursor == ' ') cursor++;
        j = 0;
        while (*cursor && *cursor != ' ' && j < (int)sizeof(cur_head) - 1)
            cur_head[j++] = *cursor++;
        cur_head[j] = 0;

        while (*cursor == ' ') cursor++;
        j = 0;
        while (*cursor && *cursor != ' ' && j < (int)sizeof(cur_body) - 1)
            cur_body[j++] = *cursor++;
        cur_body[j] = 0;

        while (*cursor == ' ') cursor++;
        j = 0;
        while (*cursor && *cursor != ' ' && j < (int)sizeof(cur_legs) - 1)
            cur_legs[j++] = *cursor++;
        cur_legs[j] = 0;
    }

    /* Populate model names */
    for (i = 0; i < uis.numPlayerModels; i++) {
        m_player.pmnames[i] = uis.pmi[i].directory;
        if (!Q_stricmp(uis.pmi[i].directory, cur_model)) {
            current_model_idx = i;
        }
    }
    m_player.pmnames[uis.numPlayerModels] = NULL;
    m_player.model.itemnames = m_player.pmnames;
    m_player.model.curvalue = current_model_idx;

    pmi = &uis.pmi[current_model_idx];

    /* Bind head, body, legs */
    m_player.head.itemnames = pmi->head_skins;
    m_player.head.curvalue = 0;
    for (j = 0; j < pmi->nhead; j++) {
        if (!Q_stricmp(pmi->head_skins[j], cur_head)) {
            m_player.head.curvalue = j;
            break;
        }
    }

    m_player.body.itemnames = pmi->body_skins;
    m_player.body.curvalue = 0;
    for (j = 0; j < pmi->nbody; j++) {
        if (!Q_stricmp(pmi->body_skins[j], cur_body)) {
            m_player.body.curvalue = j;
            break;
        }
    }

    m_player.legs.itemnames = pmi->legs_skins;
    m_player.legs.curvalue = 0;
    for (j = 0; j < pmi->nlegs; j++) {
        if (!Q_stricmp(pmi->legs_skins[j], cur_legs)) {
            m_player.legs.curvalue = j;
            break;
        }
    }

    /* Handedness */
    m_player.hand.curvalue = Cvar_ClampInteger(Cvar_Get("hand", "0", 0), 0, 2);

    /* Player name */
    Q_strlcpy(m_player.name.field.text, Cvar_VariableString("name"), sizeof(m_player.name.field.text));
    m_player.name.field.cursorPos = strlen(m_player.name.field.text);

    SpinControl_Init(&m_player.model);
    SpinControl_Init(&m_player.head);
    SpinControl_Init(&m_player.body);
    SpinControl_Init(&m_player.legs);
    SpinControl_Init(&m_player.hand);

    ReloadMedia();

    m_player.time = uis.realtime - 100;
    m_player.oldTime = m_player.time;
    RunFrame();

    return true;
}

static void Free(menuFrameWork_t *self)
{
    Z_Free(m_player.menu.items);
    memset(&m_player, 0, sizeof(m_player));
}

void M_Menu_PlayerConfig(void)
{
    static const vec3_t origin = { 72.0f, 0.0f, -12.0f };
    static const vec3_t angles = { 0.0f, 250.0f, 0.0f };
    int i;

    m_player.menu.name = "players";
    m_player.menu.push = Push;
    m_player.menu.pop = Pop;
    m_player.menu.size = Size;
    m_player.menu.draw = Draw;
    m_player.menu.free = Free;
    m_player.menu.image = uis.backgroundHandle;
    m_player.menu.color.u32 = uis.color.background.u32;
    m_player.menu.transparent = false;

    for (i = 0; i < 4; i++) {
        m_player.entities[i].flags = RF_FULLBRIGHT;
        VectorCopy(angles, m_player.entities[i].angles);
        VectorCopy(origin, m_player.entities[i].origin);
        VectorCopy(origin, m_player.entities[i].oldorigin);
    }

    m_player.refdef.num_entities = 0;
    m_player.refdef.entities = m_player.entities;
    m_player.refdef.rdflags = RDF_NOWORLDMODEL;

    m_player.name.generic.type = MTYPE_FIELD;
    m_player.name.generic.flags = QMF_HASFOCUS;
    m_player.name.generic.name = "name";
    m_player.name.width = MAX_CLIENT_NAME - 1;

    m_player.model.generic.type = MTYPE_SPINCONTROL;
    m_player.model.generic.id = ID_MODEL;
    m_player.model.generic.name = "model";
    m_player.model.generic.change = Change;

    m_player.head.generic.type = MTYPE_SPINCONTROL;
    m_player.head.generic.id = ID_HEAD;
    m_player.head.generic.name = "head";
    m_player.head.generic.change = Change;

    m_player.body.generic.type = MTYPE_SPINCONTROL;
    m_player.body.generic.id = ID_BODY;
    m_player.body.generic.name = "body";
    m_player.body.generic.change = Change;

    m_player.legs.generic.type = MTYPE_SPINCONTROL;
    m_player.legs.generic.id = ID_LEGS;
    m_player.legs.generic.name = "legs";
    m_player.legs.generic.change = Change;

    m_player.hand.generic.type = MTYPE_SPINCONTROL;
    m_player.hand.generic.name = "hand";
    m_player.hand.itemnames = (char **)handedness;

    Menu_AddItem(&m_player.menu, &m_player.name);
    Menu_AddItem(&m_player.menu, &m_player.model);
    Menu_AddItem(&m_player.menu, &m_player.head);
    Menu_AddItem(&m_player.menu, &m_player.body);
    Menu_AddItem(&m_player.menu, &m_player.legs);
    Menu_AddItem(&m_player.menu, &m_player.hand);

    List_Append(&ui_menus, &m_player.menu.entry);
}
