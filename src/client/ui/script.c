/*
Copyright (C) 2008 Andrey Nazarov

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
#include "common/files.h"

static menuSound_t Activate(menuCommon_t *self)
{
    switch (self->type) {
    case MTYPE_ACTION:
        Cbuf_AddText(&cmd_buffer, ((menuAction_t *)self)->cmd);
        Cbuf_AddText(&cmd_buffer, "\n");
        break;
    case MTYPE_BITMAP:
        Cbuf_AddText(&cmd_buffer, ((menuBitmap_t *)self)->cmd);
        Cbuf_AddText(&cmd_buffer, "\n");
        break;
    case MTYPE_SAVEGAME:
        Cbuf_AddText(&cmd_buffer, va("save \"%s\"; forcemenuoff\n", ((menuAction_t *)self)->cmd));
        break;
    case MTYPE_LOADGAME:
        Cbuf_AddText(&cmd_buffer, va("load \"%s\"\n", ((menuAction_t *)self)->cmd));
        break;
    default:
        break;
    }

    return QMS_NOTHANDLED;
}

static const cmd_option_t o_common[] = {
    { "s:", "status" },
    { NULL }
};

static void add_string(menuSpinControl_t *s, const char *tok)
{
    if (s->numItems < MAX_MENU_ITEMS) {
        s->itemnames = Z_Realloc(s->itemnames, Q_ALIGN(s->numItems + 2, MIN_MENU_ITEMS) * sizeof(char *));
        s->itemnames[s->numItems++] = UI_CopyString(tok);
    }
}

static void add_expand(menuSpinControl_t *s, const char *tok)
{
    char buf[MAX_STRING_CHARS], *temp = NULL;
    const char *data;

    cmd_macro_t *macro = Cmd_FindMacro(tok);
    if (macro) {
        size_t len = macro->function(buf, sizeof(buf));
        if (len < sizeof(buf)) {
            data = buf;
        } else if (len < INT_MAX) {
            data = temp = UI_Malloc(len + 1);
            macro->function(temp, len + 1);
        } else {
            Com_Printf("Expanded line exceeded %i chars, discarded.\n", INT_MAX);
            return;
        }
    } else {
        cvar_t *var = Cvar_FindVar(tok);
        if (var && !(var->flags & CVAR_PRIVATE))
            data = var->string;
        else
            return;
    }

    while (1) {
        tok = COM_Parse(&data);
        if (!data)
            break;
        add_string(s, tok);
    }

    Z_Free(temp);
}

static void long_args_hack(menuSpinControl_t *s, int argc)
{
    int i;

    s->itemnames = UI_Malloc(MIN_MENU_ITEMS * sizeof(char *));

    for (i = 0; i < argc; i++) {
        char *tok = Cmd_Argv(cmd_optind + i);
        if (*tok == '$') {
            tok++;
            if (*tok == '$')
                add_string(s, tok);
            else
                add_expand(s, tok);
        } else {
            add_string(s, tok);
        }
    }

    s->itemnames[s->numItems] = NULL;
}

static void Parse_Spin(menuFrameWork_t *menu, menuType_t type)
{
    menuSpinControl_t *s;
    int c, i, numItems;
    char *status = NULL;

    while ((c = Cmd_ParseOptions(o_common)) != -1) {
        switch (c) {
        case 's':
            status = cmd_optarg;
            break;
        default:
            return;
        }
    }

    numItems = Cmd_Argc() - (cmd_optind + 2);
    if (numItems < 1) {
        Com_Printf("Usage: %s <name> <cvar> <desc1> [...]\n", Cmd_Argv(0));
        return;
    }

    s = UI_Mallocz(sizeof(*s));
    s->generic.type = type;
    s->generic.name = UI_CopyString(Cmd_Argv(cmd_optind));
    s->generic.status = UI_CopyString(status);
    s->cvar = Cvar_WeakGet(Cmd_Argv(cmd_optind + 1));

    cmd_optind += 2;
    if (strchr(Cmd_ArgsFrom(cmd_optind), '$')) {
        long_args_hack(s, numItems);
    } else {
        s->itemnames = UI_Mallocz(sizeof(char *) * (numItems + 1));
        for (i = 0; i < numItems; i++) {
            s->itemnames[i] = UI_CopyString(Cmd_Argv(cmd_optind + i));
        }
        s->numItems = numItems;
    }

    Menu_AddItem(menu, s);
}

static void Parse_Pairs(menuFrameWork_t *menu)
{
    menuSpinControl_t *s;
    int c, i, numItems;
    char *status = NULL;

    while ((c = Cmd_ParseOptions(o_common)) != -1) {
        switch (c) {
        case 's':
            status = cmd_optarg;
            break;
        default:
            return;
        }
    }

    numItems = Cmd_Argc() - (cmd_optind + 2);
    if (numItems < 2 || (numItems & 1)) {
        Com_Printf("Usage: %s <name> <cvar> <desc1> <value1> [...]\n", Cmd_Argv(0));
        return;
    }

    s = UI_Mallocz(sizeof(*s));
    s->generic.type = MTYPE_PAIRS;
    s->generic.name = UI_CopyString(Cmd_Argv(cmd_optind));
    s->generic.status = UI_CopyString(status);
    s->cvar = Cvar_WeakGet(Cmd_Argv(cmd_optind + 1));
    numItems /= 2;
    s->itemnames = UI_Mallocz(sizeof(char *) * (numItems + 1));
    s->itemvalues = UI_Mallocz(sizeof(char *) * (numItems + 1));
    for (i = 0; i < numItems; i++) {
        s->itemnames[i] = UI_CopyString(Cmd_Argv(cmd_optind + 2 + i * 2));
        s->itemvalues[i] = UI_CopyString(Cmd_Argv(cmd_optind + 3 + i * 2));
    }
    s->numItems = numItems;

    Menu_AddItem(menu, s);
}

static void Parse_Range(menuFrameWork_t *menu)
{
    menuSlider_t *s;
    char *status = NULL;
    int c;

    while ((c = Cmd_ParseOptions(o_common)) != -1) {
        switch (c) {
        case 's':
            status = cmd_optarg;
            break;
        default:
            return;
        }
    }

    if (Cmd_Argc() - cmd_optind < 4) {
        Com_Printf("Usage: %s <name> <cvar> <min> <max> [step]\n", Cmd_Argv(0));
        return;
    }

    s = UI_Mallocz(sizeof(*s));
    s->generic.type = MTYPE_SLIDER;
    s->generic.name = UI_CopyString(Cmd_Argv(cmd_optind));
    s->generic.status = UI_CopyString(status);
    s->cvar = Cvar_WeakGet(Cmd_Argv(cmd_optind + 1));
    s->minvalue = Q_atof(Cmd_Argv(cmd_optind + 2));
    s->maxvalue = Q_atof(Cmd_Argv(cmd_optind + 3));
    if (Cmd_Argc() - cmd_optind > 4) {
        s->step = Q_atof(Cmd_Argv(cmd_optind + 4));
    } else {
        s->step = (s->maxvalue - s->minvalue) / SLIDER_RANGE;
    }

    Menu_AddItem(menu, s);
}

static void Parse_Action(menuFrameWork_t *menu)
{
    static const cmd_option_t o_action[] = {
        { "a", "align" },
        { "s:", "status" },
        { NULL }
    };
    menuAction_t *a;
    int uiFlags = UI_CENTER;
    char *status = NULL;
    int c;

    while ((c = Cmd_ParseOptions(o_action)) != -1) {
        switch (c) {
        case 'a':
            uiFlags = UI_LEFT | UI_ALTCOLOR;
            break;
        case 's':
            status = cmd_optarg;
            break;
        default:
            return;
        }
    }

    if (Cmd_Argc() - cmd_optind < 2) {
        Com_Printf("Usage: %s <name> <command>\n", Cmd_Argv(0));
        return;
    }

    a = UI_Mallocz(sizeof(*a));
    a->generic.type = MTYPE_ACTION;
    a->generic.name = UI_CopyString(Cmd_Argv(cmd_optind));
    a->generic.activate = Activate;
    a->generic.uiFlags = uiFlags;
    a->generic.status = UI_CopyString(status);
    a->cmd = UI_CopyString(Cmd_ArgsFrom(cmd_optind + 1));

    Menu_AddItem(menu, a);
}

static void Parse_Bitmap(menuFrameWork_t *menu)
{
    static const cmd_option_t o_bitmap[] = {
        { "s:", "status" },
        { "N:", "altname" },
        { NULL }
    };
    menuBitmap_t *b;
    char *status = NULL, *altname = NULL;
    int c;

    while ((c = Cmd_ParseOptions(o_bitmap)) != -1) {
        switch (c) {
        case 's':
            status = cmd_optarg;
            break;
        case 'N':
            altname = cmd_optarg;
            break;
        default:
            return;
        }
    }

    if (Cmd_Argc() - cmd_optind < 2) {
        Com_Printf("Usage: %s <name> <command>\n", Cmd_Argv(0));
        return;
    }

    b = UI_Mallocz(sizeof(*b));
    b->generic.type = MTYPE_BITMAP;
    b->generic.activate = Activate;
    b->generic.status = UI_CopyString(status);
    b->cmd = UI_CopyString(Cmd_ArgsFrom(cmd_optind + 1));
    b->pics[0] = R_RegisterPic(Cmd_Argv(cmd_optind));
    if (!altname)
        altname = va("%s_sel", Cmd_Argv(cmd_optind));
    b->pics[1] = R_RegisterPic(altname);
    R_GetPicSize(&b->generic.width, &b->generic.height, b->pics[0]);

    Menu_AddItem(menu, b);
}

static void Parse_Bind(menuFrameWork_t *menu)
{
    static const cmd_option_t o_bind[] = {
        { "s:", "status" },
        { "S:", "altstatus" },
        { NULL }
    };
    menuKeybind_t *k;
    const char *status = "Press Enter to change, Backspace to clear";
    const char *altstatus = "Press the desired key, Escape to cancel";
    int c;

    while ((c = Cmd_ParseOptions(o_bind)) != -1) {
        switch (c) {
        case 's':
            status = cmd_optarg;
            break;
        case 'S':
            altstatus = cmd_optarg;
            break;
        default:
            return;
        }
    }

    if (Cmd_Argc() - cmd_optind < 2) {
        Com_Printf("Usage: %s <name> <command>\n", Cmd_Argv(0));
        return;
    }

    k = UI_Mallocz(sizeof(*k));
    k->generic.type = MTYPE_KEYBIND;
    k->generic.name = UI_CopyString(Cmd_Argv(cmd_optind));
    k->generic.uiFlags = UI_CENTER;
    k->generic.status = UI_CopyString(status);
    k->cmd = UI_CopyString(Cmd_ArgsFrom(cmd_optind + 1));
    k->altstatus = UI_CopyString(altstatus);

    Menu_AddItem(menu, k);
}

static void Parse_Savegame(menuFrameWork_t *menu, menuType_t type)
{
    menuAction_t *a;
    char *status = NULL;
    int c;

    while ((c = Cmd_ParseOptions(o_common)) != -1) {
        switch (c) {
        case 's':
            status = cmd_optarg;
            break;
        default:
            return;
        }
    }

    if (Cmd_Argc() - cmd_optind < 1) {
        Com_Printf("Usage: %s <dir>\n", Cmd_Argv(0));
        return;
    }

    a = UI_Mallocz(sizeof(*a));
    a->generic.type = type;
    a->generic.name = UI_CopyString("<EMPTY>");
    a->generic.activate = Activate;
    a->generic.uiFlags = UI_CENTER;
    a->generic.status = UI_CopyString(status);
    a->cmd = UI_CopyString(Cmd_Argv(cmd_optind));

    if (type == MTYPE_LOADGAME)
        a->generic.flags |= QMF_GRAYED;

    Menu_AddItem(menu, a);
}

static void Parse_Toggle(menuFrameWork_t *menu)
{
    static const char *const yes_no_names[] = { "no", "yes", NULL };
    menuSpinControl_t *s;
    bool negate = false;
    menuType_t type = MTYPE_TOGGLE;
    int c, bit = 0;
    char *b, *status = NULL;

    while ((c = Cmd_ParseOptions(o_common)) != -1) {
        switch (c) {
        case 's':
            status = cmd_optarg;
            break;
        default:
            return;
        }
    }

    if (Cmd_Argc() - cmd_optind < 2) {
        Com_Printf("Usage: %s <name> <cvar> [~][bit]\n", Cmd_Argv(0));
        return;
    }

    b = Cmd_Argv(cmd_optind + 2);
    if (*b == '~') {
        negate = true;
        b++;
    }
    if (*b) {
        bit = Q_atoi(b);
        if (bit < 0 || bit >= 32) {
            Com_Printf("Invalid bit number: %d\n", bit);
            return;
        }
        type = MTYPE_BITFIELD;
    }

    s = UI_Mallocz(sizeof(*s));
    s->generic.type = type;
    s->generic.name = UI_CopyString(Cmd_Argv(cmd_optind));
    s->generic.status = UI_CopyString(status);
    s->cvar = Cvar_WeakGet(Cmd_Argv(cmd_optind + 1));
    s->itemnames = (char **)yes_no_names;
    s->numItems = 2;
    s->negate = negate;
    s->mask = 1U << bit;

    Menu_AddItem(menu, s);
}

static void Parse_Field(menuFrameWork_t *menu)
{
    static const cmd_option_t o_field[] = {
        { "c", "center" },
        { "i", "integer" },
        { "n", "numeric" },
        { "s:", "status" },
        { "w:", "width" },
        { NULL }
    };
    menuField_t *f;
    bool center = false;
    int flags = 0;
    char *status = NULL;
    int width = 16;
    int c;

    while ((c = Cmd_ParseOptions(o_field)) != -1) {
        switch (c) {
        case 'c':
            center = true;
            break;
        case 'i':
        case 'n':
            flags |= QMF_NUMBERSONLY;
            break;
        case 's':
            status = cmd_optarg;
            break;
        case 'w':
            width = Q_atoi(cmd_optarg);
            if (width < 1 || width > 32) {
                Com_Printf("Invalid width\n");
                return;
            }
            break;
        default:
            return;
        }
    }

    f = UI_Mallocz(sizeof(*f));
    f->generic.type = MTYPE_FIELD;
    f->generic.name = center ? NULL : UI_CopyString(Cmd_Argv(cmd_optind));
    f->generic.status = UI_CopyString(status);
    f->generic.flags = flags;
    f->cvar = Cvar_WeakGet(Cmd_Argv(center ? cmd_optind : cmd_optind + 1));
    f->width = width;

    Menu_AddItem(menu, f);
}

static void Parse_Blank(menuFrameWork_t *menu)
{
    menuSeparator_t *s;

    s = UI_Mallocz(sizeof(*s));
    s->generic.type = MTYPE_SEPARATOR;

    Menu_AddItem(menu, s);
}

static void Parse_Background(menuFrameWork_t *menu)
{
    char *s = Cmd_Argv(1);

    if (SCR_ParseColor(s, &menu->color)) {
        menu->image = 0;
        menu->transparent = menu->color.u8[3] != 255;
    } else {
        menu->image = R_RegisterPic(s);
        menu->transparent = R_GetPicSize(NULL, NULL, menu->image);
    }
}

static void Parse_Style(menuFrameWork_t *menu)
{
    static const cmd_option_t o_style[] = {
        { "c", "compact" },
        { "C", "no-compact" },
        { "t", "transparent" },
        { "T", "no-transparent" },
        { NULL }
    };
    int c;

    while ((c = Cmd_ParseOptions(o_style)) != -1) {
        switch (c) {
        case 'c':
            menu->compact = true;
            break;
        case 'C':
            menu->compact = false;
            break;
        case 't':
            menu->transparent = true;
            break;
        case 'T':
            menu->transparent = false;
            break;
        default:
            return;
        }
    }
}

static void Parse_Color(void)
{
    char *s, *c;

    if (Cmd_Argc() < 3) {
        Com_Printf("Usage: %s <state> <color>\n", Cmd_Argv(0));
        return;
    }

    s = Cmd_Argv(1);
    c = Cmd_Argv(2);

    if (!strcmp(s, "normal")) {
        SCR_ParseColor(c, &uis.color.normal);
    } else if (!strcmp(s, "active")) {
        SCR_ParseColor(c, &uis.color.active);
    } else if (!strcmp(s, "selection")) {
        SCR_ParseColor(c, &uis.color.selection);
    } else if (!strcmp(s, "disabled")) {
        SCR_ParseColor(c, &uis.color.disabled);
    } else {
        Com_Printf("Unknown state '%s'\n", s);
    }
}

static void Parse_Plaque(menuFrameWork_t *menu)
{
    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s <plaque> [logo]\n", Cmd_Argv(0));
        return;
    }

    menu->plaque = R_RegisterPic(Cmd_Argv(1));
    if (menu->plaque) {
        R_GetPicSize(&menu->plaque_rc.width,
                     &menu->plaque_rc.height, menu->plaque);
    }

    if (Cmd_Argc() > 2) {
        menu->logo = R_RegisterPic(Cmd_Argv(2));
        if (menu->logo) {
            R_GetPicSize(&menu->logo_rc.width,
                         &menu->logo_rc.height, menu->logo);
        }
    }
}

static void Parse_Banner(menuFrameWork_t *menu)
{
    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s <banner>\n", Cmd_Argv(0));
        return;
    }

    menu->banner = R_RegisterPic(Cmd_Argv(1));
    if (menu->banner) {
        R_GetPicSize(&menu->banner_rc.width,
                     &menu->banner_rc.height, menu->banner);
    }
}

static bool Parse_File(const char *path, int depth);

static bool Parse_Data(char *data, int depth)
{
    char *p, *cmd;
    int argc;
    menuFrameWork_t *menu = NULL;

    COM_Compress(data);

    while (*data) {
        p = strchr(data, '\n');
        if (p) {
            *p = 0;
        }

        Cmd_TokenizeString(data, true);

        argc = Cmd_Argc();
        if (argc) {
            cmd = Cmd_Argv(0);
            if (menu) {
                if (!strcmp(cmd, "end")) {
                    if (menu->nitems) {
                        List_Append(&ui_menus, &menu->entry);
                    } else {
                        Com_WPrintf("Menu entry without items\n");
                        menu->free(menu);
                    }
                    menu = NULL;
                } else if (!strcmp(cmd, "title")) {
                    Z_Free(menu->title);
                    menu->title = UI_CopyString(Cmd_Argv(1));
                } else if (!strcmp(cmd, "plaque")) {
                    Parse_Plaque(menu);
                } else if (!strcmp(cmd, "banner")) {
                    Parse_Banner(menu);
                } else if (!strcmp(cmd, "background")) {
                    Parse_Background(menu);
                } else if (!strcmp(cmd, "style")) {
                    Parse_Style(menu);
                } else if (!strcmp(cmd, "values")) {
                    Parse_Spin(menu, MTYPE_SPINCONTROL);
                } else if (!strcmp(cmd, "strings")) {
                    Parse_Spin(menu, MTYPE_STRINGS);
                } else if (!strcmp(cmd, "pairs")) {
                    Parse_Pairs(menu);
                } else if (!strcmp(cmd, "range")) {
                    Parse_Range(menu);
                } else if (!strcmp(cmd, "action")) {
                    Parse_Action(menu);
                } else if (!strcmp(cmd, "bitmap")) {
                    Parse_Bitmap(menu);
                } else if (!strcmp(cmd, "bind")) {
                    Parse_Bind(menu);
                } else if (!strcmp(cmd, "savegame")) {
                    Parse_Savegame(menu, MTYPE_SAVEGAME);
                } else if (!strcmp(cmd, "loadgame")) {
                    Parse_Savegame(menu, MTYPE_LOADGAME);
                } else if (!strcmp(cmd, "toggle")) {
                    Parse_Toggle(menu);
                } else if (!strcmp(cmd, "field")) {
                    Parse_Field(menu);
                } else if (!strcmp(cmd, "blank")) {
                    Parse_Blank(menu);
                } else {
                    Com_WPrintf("Unknown keyword '%s'\n", cmd);
                }
            } else {
                if (!strcmp(cmd, "begin")) {
                    char *s = Cmd_Argv(1);
                    if (!*s) {
                        Com_WPrintf("Expected menu name after '%s'\n", cmd);
                        break;
                    }
                    menu = UI_FindMenu(s);
                    if (menu) {
                        List_Remove(&menu->entry);
                        if (menu->free) {
                            menu->free(menu);
                        }
                    }
                    menu = UI_Mallocz(sizeof(*menu));
                    menu->name = UI_CopyString(s);
                    menu->push = Menu_Push;
                    menu->pop = Menu_Pop;
                    menu->free = Menu_Free;
                    menu->image = uis.backgroundHandle;
                    menu->color.u32 = uis.color.background.u32;
                    menu->transparent = uis.transparent;
                } else if (!strcmp(cmd, "include")) {
                    char *s = Cmd_Argv(1);
                    if (!*s) {
                        Com_WPrintf("Expected file name after '%s'\n", cmd);
                        break;
                    }
                    if (depth == 16) {
                        Com_WPrintf("Includes too deeply nested\n");
                    } else {
                        Parse_File(s, depth + 1);
                    }
                } else if (!strcmp(cmd, "color")) {
                    Parse_Color();
                } else if (!strcmp(cmd, "background")) {
                    char *s = Cmd_Argv(1);

                    if (SCR_ParseColor(s, &uis.color.background)) {
                        uis.backgroundHandle = 0;
                        uis.transparent = uis.color.background.u8[3] != 255;
                    } else {
                        uis.backgroundHandle = R_RegisterPic(s);
                        uis.transparent = R_GetPicSize(NULL, NULL, uis.backgroundHandle);
                    }
                } else if (!strcmp(cmd, "font")) {
                    uis.fontHandle = R_RegisterFont(Cmd_Argv(1));
                } else if (!strcmp(cmd, "cursor")) {
                    uis.cursorHandle = R_RegisterPic(Cmd_Argv(1));
                    R_GetPicSize(&uis.cursorWidth,
                                 &uis.cursorHeight, uis.cursorHandle);
                } else if (!strcmp(cmd, "weapon")) {
                    Cmd_ArgvBuffer(1, uis.weaponModel, sizeof(uis.weaponModel));
                } else {
                    Com_WPrintf("Unknown keyword '%s'\n", cmd);
                    break;
                }
            }
        }

        if (!p) {
            break;
        }

        data = p + 1;
    }

    if (menu) {
        Com_WPrintf("Menu entry without 'end' terminator\n");
        menu->free(menu);
    }

    return true;
}

static bool Parse_File(const char *path, int depth)
{
    char *raw;
    int ret;
    bool ok;

    ret = FS_LoadFile(path, (void **)&raw);
    if (!raw) {
        if (ret != Q_ERR(ENOENT) || depth) {
            Com_WPrintf("Couldn't %s %s: %s\n", depth ? "include" : "load",
                        path, Q_ErrorString(ret));
        }
        return false;
    }

    ok = Parse_Data(raw, depth);
    FS_FreeFile(raw);
    return ok;
}

static const char kp_menu_default[] =
    "background backround\n"
    "font conchars\n"
    "cursor ch1\n"
    "color normal #18141098\n"
    "color active #282018b8\n"
    "color selection #b8861cb8\n"
    "color disabled #7f7f7f\n"
    "begin video\n"
    "title \"visuals\"\n"
    "action \"1280 x 720\" \"vid_size 1280 720\"\n"
    "action \"1600 x 900\" \"vid_size 1600 900\"\n"
    "action \"1920 x 1080\" \"vid_size 1920 1080\"\n"
    "action \"2560 x 1440\" \"vid_size 2560 1440\"\n"
    "action \"3440 x 1440\" \"vid_size 3440 1440\"\n"
    "action \"3840 x 2160\" \"vid_size 3840 2160\"\n"
    "values \"fullscreen\" vid_fullscreen windowed $$vid_modelist\n"
    "toggle \"vertical sync\" gl_swapinterval\n"
    "range \"brightness\" vid_gamma 1.3 0.5\n"
    "pairs \"texture filter\" gl_texturemode "
        "nearest GL_NEAREST "
        "linear GL_LINEAR "
        "bilinear GL_LINEAR_MIPMAP_NEAREST "
        "trilinear GL_LINEAR_MIPMAP_LINEAR\n"
    "values \"dynamic lights\" gl_dynamic no yes \"only switchable\"\n"
    "values \"shadows\" gl_shadows no yes fade\n"
    "toggle \"screen blend\" gl_polyblend\n"
    "range \"screen size\" viewsize 40 100 10\n"
    "range \"hud opacity\" scr_alpha 0 1\n"
    "range \"console opacity\" con_alpha 0 1\n"
    "values \"crosshair\" crosshair none cross dot angle\n"
    "end\n"
    "begin sound\n"
    "title \"sound\"\n"
    "values \"sound\" s_enable \"no sound\" software OpenAL\n"
    "range \"volume\" s_volume 0 1\n"
    "range \"music volume\" ogg_volume 0 1\n"
    "toggle \"music\" ogg_enable\n"
    "end\n"
    "begin input\n"
    "title \"mouse\"\n"
    "field --numeric --width 8 \"sensitivity\" sensitivity\n"
    "toggle \"free look\" freelook\n"
    "toggle \"always run\" cl_run\n"
    "toggle \"mouse filter\" m_filter\n"
    "end\n"
    "begin keys\n"
    "title \"controls\"\n"
    "bind \"attack\" +attack\n"
    "bind \"activate\" +activate\n"
    "bind \"reload\" reload\n"
    "bind \"holster\" holster\n"
    "bind \"flashlight\" flashlight\n"
    "bind \"next weapon\" weapnext\n"
    "bind \"previous weapon\" weapprev\n"
    "bind \"forward\" +forward\n"
    "bind \"back\" +back\n"
    "bind \"move left\" +moveleft\n"
    "bind \"move right\" +moveright\n"
    "bind \"jump\" +moveup\n"
    "bind \"crouch\" +movedown\n"
    "bind \"run\" +speed\n"
    "bind \"chat\" messagemode\n"
    "bind \"team chat\" messagemode2\n"
    "action \"weapons\" pushmenu kpweapons\n"
    "action \"reset defaults\" defaultbinds\n"
    "end\n"
    "begin kpweapons\n"
    "title \"weapons\"\n"
    "bind \"pipe\" \"use pipe\"\n"
    "bind \"pistol\" \"use pistol\"\n"
    "bind \"shotgun\" \"use shotgun\"\n"
    "bind \"tommygun\" \"use tommygun\"\n"
    "bind \"heavy machinegun\" \"use heavy machinegun\"\n"
    "bind \"grenade launcher\" \"use grenade launcher\"\n"
    "bind \"bazooka\" \"use bazooka\"\n"
    "bind \"flamethrower\" \"use flamethrower\"\n"
    "end\n"
    "begin downloads\n"
    "title \"download options\"\n"
    "toggle \"allow downloading\" allow_download\n"
    "toggle \"maps\" allow_download_maps\n"
    "toggle \"player models\" allow_download_players\n"
    "toggle \"models\" allow_download_models\n"
    "toggle \"sounds\" allow_download_sounds\n"
    "toggle \"textures\" allow_download_textures\n"
    "toggle \"pics\" allow_download_pics\n"
    "end\n";

void UI_LoadScript(void)
{
    char *copy;

    Parse_File("q2pro.menu", 0);
    copy = UI_CopyString(kp_menu_default);
    Parse_Data(copy, 0);
    Z_Free(copy);
}
