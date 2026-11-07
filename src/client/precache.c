/*
Copyright (C) 1997-2001 Id Software, Inc.

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

//
// cl_precache.c
//

#include "client.h"

/*
================
CL_ParsePlayerSkin

Breaks up playerskin into name (optional), model and skin components.
If model or skin are found to be invalid, replaces them with sane defaults.
================
*/
void CL_ParsePlayerSkin(char *name, char *model, char *skin, const char *s)
{
    size_t len;
    char *t;
    const char *slash;

    len = strlen(s);
    Q_assert(len < MAX_QPATH);

    // isolate the player's name
    slash = strchr(s, '\\');
    if (slash) {
        len = slash - s;
        strcpy(model, slash + 1);
    } else {
        len = 0;
        strcpy(model, s);
    }

    // copy the player's name
    if (name) {
        memcpy(name, s, len);
        name[len] = 0;
    }

    // isolate the model name
    t = strchr(model, '/');
    if (!t)
        t = strchr(model, '\\');
    if (!t)
        goto default_model;
    *t = 0;

    // isolate the skin name
    strcpy(skin, t + 1);

    // fix empty model to male
    if (t == model)
        strcpy(model, "male");

    // apply restrictions on skins
    if (cl_noskins->integer == 2 || !COM_IsPath(skin))
        goto default_skin;

    if (cl_noskins->integer || !COM_IsPath(model))
        goto default_model;

    return;

default_skin:
    if (!Q_stricmp(model, "female")) {
        strcpy(model, "female");
        strcpy(skin, "athena");
    } else {
default_model:
        strcpy(model, "male");
        strcpy(skin, "grunt");
    }
}

/*
================
CL_LoadKingpinClientinfo

modelindex 255 is not sent with head, body, or legs. Those files live in
players/<model>/ and the skin numbers in the configstring pick
<head> <body> <legs> under the directories in skinfolder.txt.
================
*/
static void CL_KingpinToken(const char **pp, char *out, size_t size)
{
    const char *p = *pp;
    size_t n = 0;

    while (*p == ' ')
        p++;
    while (p[n] && p[n] != ' ' && n + 1 < size)
        n++;
    memcpy(out, p, n);
    out[n] = 0;
    *pp = p + n;
}

void CL_KingpinSkinDirs(const char *model, char *head, char *body, char *legs, size_t size)
{
    char path[MAX_QPATH];
    char *data, *p, *eol;
    int len;

    head[0] = body[0] = legs[0] = 0;
    if (Q_snprintf(path, sizeof(path), "players/%s/skinfolder.txt", model) < (int)sizeof(path)) {
        len = FS_LoadFile(path, (void **)&data);
        if (data) {
            (void)len;
            p = data;
            while (*p) {
                char *dst = NULL;
                size_t n;
                const char *s;

                eol = strchr(p, '\n');
                if (!eol)
                    eol = p + strlen(p);
                if (!Q_strncasecmp(p, "head:", 5))
                    dst = head;
                else if (!Q_strncasecmp(p, "body:", 5))
                    dst = body;
                else if (!Q_strncasecmp(p, "legs:", 5))
                    dst = legs;
                if (dst) {
                    s = p + 5;
                    n = (size_t)(eol - s);
                    while (n && (s[n - 1] == '\r' || s[n - 1] == ' '))
                        n--;
                    if (n >= size)
                        n = size - 1;
                    memcpy(dst, s, n);
                    dst[n] = 0;
                }
                if (!*eol)
                    break;
                p = eol + 1;
            }
            FS_FreeFile(data);
        }
    }

    if (!head[0])
        Q_snprintf(head, size, "players/%s", model);
    if (!body[0])
        Q_snprintf(body, size, "players/%s", model);
    if (!legs[0])
        Q_snprintf(legs, size, "players/%s", model);
}

static qhandle_t CL_RegisterKingpinPart(const char *model, const char *part)
{
    char path[MAX_QPATH];

    if (Q_snprintf(path, sizeof(path), "players/%s/%s.mdx", model, part) >= (int)sizeof(path))
        return 0;
    return R_RegisterModel(path);
}

qhandle_t CL_RegisterKingpinSkin(const char *model, const char *folder, const char *part, const char *code)
{
    char path[MAX_QPATH];
    const char *slash;

    if (!code || !code[0] || !part || !part[0])
        return 0;

    slash = strrchr(code, '/');
    if (slash) {
        char subdir[MAX_QPATH];
        const char *skinname = slash + 1;
        size_t dirlen = slash - code;

        if (dirlen >= sizeof(subdir))
            dirlen = sizeof(subdir) - 1;
        memcpy(subdir, code, dirlen);
        subdir[dirlen] = 0;

        if (folder && folder[0]) {
            Q_snprintf(path, sizeof(path), "%s/%s/%s_%s.tga", folder, subdir, part, skinname);
            if (FS_FileExists(path))
                return R_RegisterSkin(path);
            Q_snprintf(path, sizeof(path), "%s/%s/%s_%s.pcx", folder, subdir, part, skinname);
            if (FS_FileExists(path))
                return R_RegisterSkin(path);
        }

        if (model && model[0]) {
            Q_snprintf(path, sizeof(path), "players/%s/%s/%s_%s.tga", model, subdir, part, skinname);
            if (FS_FileExists(path))
                return R_RegisterSkin(path);
            Q_snprintf(path, sizeof(path), "players/%s/%s/%s_%s.pcx", model, subdir, part, skinname);
            if (FS_FileExists(path))
                return R_RegisterSkin(path);
        }
    } else {
        if (folder && folder[0]) {
            Q_snprintf(path, sizeof(path), "%s/%s_%s.tga", folder, part, code);
            if (FS_FileExists(path))
                return R_RegisterSkin(path);
            Q_snprintf(path, sizeof(path), "%s/%s_%s.pcx", folder, part, code);
            if (FS_FileExists(path))
                return R_RegisterSkin(path);
        }

        if (model && model[0]) {
            Q_snprintf(path, sizeof(path), "players/%s/%s_%s.tga", model, part, code);
            if (FS_FileExists(path))
                return R_RegisterSkin(path);
            Q_snprintf(path, sizeof(path), "players/%s/%s_%s.pcx", model, part, code);
            if (FS_FileExists(path))
                return R_RegisterSkin(path);
        }
    }

    return 0;
}

static void CL_LoadKingpinClientinfo(clientinfo_t *ci, const char *s)
{
    const char *spec, *slash, *cursor;
    char head_code[MAX_QPATH], body_code[MAX_QPATH], legs_code[MAX_QPATH];
    char head_dir[MAX_QPATH], body_dir[MAX_QPATH], legs_dir[MAX_QPATH];
    size_t namelen;

    memset(ci, 0, sizeof(*ci));

    spec = strchr(s, '\\');
    if (spec) {
        namelen = (size_t)(spec - s);
        if (namelen >= sizeof(ci->name))
            namelen = sizeof(ci->name) - 1;
        memcpy(ci->name, s, namelen);
        ci->name[namelen] = 0;
        spec++;
    } else {
        spec = s;
    }

    slash = strchr(spec, '/');
    if (!slash || slash == spec)
        Q_strlcpy(ci->model_name, "male_thug", sizeof(ci->model_name));
    else {
        namelen = (size_t)(slash - spec);
        if (namelen >= sizeof(ci->model_name))
            namelen = sizeof(ci->model_name) - 1;
        memcpy(ci->model_name, spec, namelen);
        ci->model_name[namelen] = 0;
        spec = slash + 1;
    }

    cursor = spec;
    CL_KingpinToken(&cursor, head_code, sizeof(head_code));
    CL_KingpinToken(&cursor, body_code, sizeof(body_code));
    CL_KingpinToken(&cursor, legs_code, sizeof(legs_code));
    Q_snprintf(ci->skin_name, sizeof(ci->skin_name), "%s %s %s", head_code, body_code, legs_code);

    if (!COM_IsPath(ci->model_name))
        Q_strlcpy(ci->model_name, "male_thug", sizeof(ci->model_name));

    ci->kp_head = CL_RegisterKingpinPart(ci->model_name, "head");
    ci->kp_legs = CL_RegisterKingpinPart(ci->model_name, "legs");
    ci->kp_body = CL_RegisterKingpinPart(ci->model_name, "body");
    if (!ci->kp_body && strcmp(ci->model_name, "male_thug")) {
        Q_strlcpy(ci->model_name, "male_thug", sizeof(ci->model_name));
        ci->kp_head = CL_RegisterKingpinPart(ci->model_name, "head");
        ci->kp_legs = CL_RegisterKingpinPart(ci->model_name, "legs");
        ci->kp_body = CL_RegisterKingpinPart(ci->model_name, "body");
    }

    CL_KingpinSkinDirs(ci->model_name, head_dir, body_dir, legs_dir, sizeof(head_dir));
    ci->kp_head_skin = CL_RegisterKingpinSkin(ci->model_name, head_dir, "head", head_code);
    ci->kp_body_skin = CL_RegisterKingpinSkin(ci->model_name, body_dir, "body", body_code);
    ci->kp_legs_skin = CL_RegisterKingpinSkin(ci->model_name, legs_dir, "legs", legs_code);

    ci->model = ci->kp_body;
    ci->skin = ci->kp_body_skin;

    for (int w = 0; w < cl.numWeaponModels; w++) {
        char weapon_filename[MAX_QPATH];
        Q_snprintf(weapon_filename, sizeof(weapon_filename),
                   "players/%s/%s", ci->model_name, cl.weaponModels[w]);
        ci->weaponmodel[w] = R_RegisterModel(weapon_filename);
        if (!ci->weaponmodel[w] && strcmp(ci->model_name, "male_thug")) {
            Q_snprintf(weapon_filename, sizeof(weapon_filename),
                       "players/male_thug/%s", cl.weaponModels[w]);
            ci->weaponmodel[w] = R_RegisterModel(weapon_filename);
        }
        if (!ci->weaponmodel[w] && w == 0) {
            ci->weaponmodel[0] = R_RegisterModel("players/male_thug/w_pipe.mdx");
        }
    }
}

/*
================
CL_LoadClientinfo

================
*/
void CL_LoadClientinfo(clientinfo_t *ci, const char *s)
{
    int         i;

    if (cls.serverProtocol == PROTOCOL_VERSION_KINGPIN) {
        CL_LoadKingpinClientinfo(ci, s);
        return;
    }
    char        model_name[MAX_QPATH];
    char        skin_name[MAX_QPATH];
    char        model_filename[MAX_QPATH];
    char        skin_filename[MAX_QPATH];
    char        weapon_filename[MAX_QPATH];
    char        icon_filename[MAX_QPATH];

    CL_ParsePlayerSkin(ci->name, model_name, skin_name, s);

    // model file
    Q_concat(model_filename, sizeof(model_filename),
             "players/", model_name, "/tris.md2");
    ci->model = R_RegisterModel(model_filename);
    if (!ci->model && Q_stricmp(model_name, "male")) {
        strcpy(model_name, "male");
        strcpy(model_filename, "players/male/tris.md2");
        ci->model = R_RegisterModel(model_filename);
    }

    // skin file
    Q_concat(skin_filename, sizeof(skin_filename),
             "players/", model_name, "/", skin_name, ".pcx");
    ci->skin = R_RegisterSkin(skin_filename);

    // if we don't have the skin and the model was female,
    // see if athena skin exists
    if (!ci->skin && !Q_stricmp(model_name, "female")) {
        strcpy(skin_name, "athena");
        strcpy(skin_filename, "players/female/athena.pcx");
        ci->skin = R_RegisterSkin(skin_filename);
    }

    // if we don't have the skin and the model wasn't male,
    // see if the male has it (this is for CTF's skins)
    if (!ci->skin && Q_stricmp(model_name, "male")) {
        // change model to male
        strcpy(model_name, "male");
        strcpy(model_filename, "players/male/tris.md2");
        ci->model = R_RegisterModel(model_filename);

        // see if the skin exists for the male model
        Q_concat(skin_filename, sizeof(skin_filename),
                 "players/male/", skin_name, ".pcx");
        ci->skin = R_RegisterSkin(skin_filename);
    }

    // if we still don't have a skin, it means that the male model
    // didn't have it, so default to grunt
    if (!ci->skin) {
        // see if the skin exists for the male model
        strcpy(skin_name, "grunt");
        strcpy(skin_filename, "players/male/grunt.pcx");
        ci->skin = R_RegisterSkin(skin_filename);
    }

    // weapon file
    for (i = 0; i < cl.numWeaponModels; i++) {
        Q_concat(weapon_filename, sizeof(weapon_filename),
                 "players/", model_name, "/", cl.weaponModels[i]);
        ci->weaponmodel[i] = R_RegisterModel(weapon_filename);
        if (!ci->weaponmodel[i] && !Q_stricmp(model_name, "cyborg")) {
            // try male
            Q_concat(weapon_filename, sizeof(weapon_filename),
                     "players/male/", cl.weaponModels[i]);
            ci->weaponmodel[i] = R_RegisterModel(weapon_filename);
        }
    }

    // icon file
    Q_concat(icon_filename, sizeof(icon_filename),
             "/players/", model_name, "/", skin_name, "_i.pcx");
    ci->icon = R_RegisterTempPic(icon_filename);

    strcpy(ci->model_name, model_name);
    strcpy(ci->skin_name, skin_name);

    // base info should be at least partially valid
    if (ci == &cl.baseclientinfo)
        return;

    // must have loaded all data types to be valid
    if (!ci->skin || !ci->icon || !ci->model || !ci->weaponmodel[0]) {
        ci->skin = 0;
        ci->icon = 0;
        ci->model = 0;
        ci->weaponmodel[0] = 0;
        ci->model_name[0] = 0;
        ci->skin_name[0] = 0;
    }
}

/*
=================
CL_RegisterSounds
=================
*/
void CL_RegisterSounds(void)
{
    int i;
    char    *s;

    S_BeginRegistration();
    CL_RegisterTEntSounds();
    for (i = 1; i < cl.csr.max_sounds; i++) {
        s = cl.configstrings[cl.csr.sounds + i];
        if (!s[0])
            break;
        cl.sound_precache[i] = S_RegisterSound(s);
    }
    S_EndRegistration();
}

/*
=================
CL_RegisterBspModels

Registers main BSP file and inline models
=================
*/
void CL_RegisterBspModels(void)
{
    char *name = cl.configstrings[cl.csr.models + 1];
    int i, ret;

    if (!name[0]) {
        Com_Error(ERR_DROP, "%s: no map set", __func__);
    }
    ret = BSP_Load(name, &cl.bsp);
    if (cl.bsp == NULL) {
        Com_Error(ERR_DROP, "Couldn't load %s: %s", name, BSP_ErrorString(ret));
    }

    if (cl.bsp->checksum != Q_atoi(cl.configstrings[cl.csr.mapchecksum])) {
        if (cls.demo.playback) {
            Com_WPrintf("Local map version differs from demo: %i != %s\n",
                        cl.bsp->checksum, cl.configstrings[cl.csr.mapchecksum]);
        } else {
            Com_Error(ERR_DROP, "Local map version differs from server: %i != %s",
                      cl.bsp->checksum, cl.configstrings[cl.csr.mapchecksum]);
        }
    }

    for (i = 2; i < cl.csr.max_models; i++) {
        name = cl.configstrings[cl.csr.models + i];
        if (!name[0] && i != MODELINDEX_PLAYER) {
            break;
        }
        if (name[0] == '*')
            cl.model_clip[i] = BSP_InlineModel(cl.bsp, name);
        else
            cl.model_clip[i] = NULL;
    }
}

static const char *const kp_standard_weapons[] = {
    "w_pipe.mdx",
    "w_pistol.mdx",
    "w_shotgun.mdx",
    "w_tommygun.mdx",
    "w_hgun.mdx",
    "w_bazooka.mdx",
    "w_flame.mdx"
};

/*
=================
CL_RegisterVWepModels

Builds a list of visual weapon models
=================
*/
void CL_RegisterVWepModels(void)
{
    int         i;
    char        *name;

    if (cls.serverProtocol == PROTOCOL_VERSION_KINGPIN) {
        cl.numWeaponModels = 0;
        if (!cl_vwep->integer) {
            cl.numWeaponModels = 1;
            strcpy(cl.weaponModels[0], "w_pipe.mdx");
            return;
        }

        for (i = 1; i < cl.csr.max_models; i++) {
            name = cl.configstrings[cl.csr.models + i];
            if (!name[0] && i != MODELINDEX_PLAYER)
                break;
            if (name[0] != '#')
                continue;

            const char *weap = name + 1;
            bool exists = false;
            for (int k = 0; k < cl.numWeaponModels; k++) {
                if (!strcmp(cl.weaponModels[k], weap)) {
                    exists = true;
                    break;
                }
            }
            if (!exists && cl.numWeaponModels < MAX_CLIENTWEAPONMODELS) {
                Q_strlcpy(cl.weaponModels[cl.numWeaponModels++], weap, sizeof(cl.weaponModels[0]));
            }
        }

        for (i = 0; i < (int)q_countof(kp_standard_weapons); i++) {
            bool exists = false;
            for (int k = 0; k < cl.numWeaponModels; k++) {
                if (!strcmp(cl.weaponModels[k], kp_standard_weapons[i])) {
                    exists = true;
                    break;
                }
            }
            if (!exists && cl.numWeaponModels < MAX_CLIENTWEAPONMODELS) {
                Q_strlcpy(cl.weaponModels[cl.numWeaponModels++], kp_standard_weapons[i], sizeof(cl.weaponModels[0]));
            }
        }

        if (cl.numWeaponModels == 0) {
            cl.numWeaponModels = 1;
            strcpy(cl.weaponModels[0], "w_pipe.mdx");
        }
        return;
    }

    cl.numWeaponModels = 1;
    strcpy(cl.weaponModels[0], "weapon.md2");

    // only default model when vwep is off
    if (!cl_vwep->integer) {
        return;
    }

    for (i = 2; i < cl.csr.max_models; i++) {
        name = cl.configstrings[cl.csr.models + i];
        if (!name[0] && i != MODELINDEX_PLAYER) {
            break;
        }
        if (name[0] != '#') {
            continue;
        }

        // special player weapon model
        Q_strlcpy(cl.weaponModels[cl.numWeaponModels++], name + 1, sizeof(cl.weaponModels[0]));

        if (cl.numWeaponModels == MAX_CLIENTWEAPONMODELS) {
            break;
        }
    }
}

/*
=================
CL_SetSky

=================
*/
void CL_SetSky(void)
{
    float       rotate = 0;
    int         autorotate = 1;
    vec3_t      axis;

    if (cl.csr.extended)
        sscanf(cl.configstrings[CS_SKYROTATE], "%f %d", &rotate, &autorotate);
    else
        rotate = Q_atof(cl.configstrings[CS_SKYROTATE]);

    if (sscanf(cl.configstrings[CS_SKYAXIS], "%f %f %f",
               &axis[0], &axis[1], &axis[2]) != 3) {
        Com_DPrintf("Couldn't parse CS_SKYAXIS\n");
        VectorClear(axis);
    }

    R_SetSky(cl.configstrings[CS_SKY], rotate, autorotate, axis);
}

/*
=================
CL_RegisterImage

Hack to handle RF_CUSTOMSKIN for remaster
=================
*/
static qhandle_t CL_RegisterImage(const char *s)
{
    // if it's in a subdir and has an extension, it's either a sprite or a skin
    // allow /some/pic.pcx escape syntax
    if (cl.csr.extended && *s != '/' && *s != '\\' && *COM_FileExtension(s)) {
        if (!FS_pathcmpn(s, CONST_STR_LEN("sprites/psx_flare")))
            return R_RegisterImage(s, IT_SPRITE, IF_DEFAULT_FLARE);

        if (!FS_pathcmpn(s, CONST_STR_LEN("sprites/")))
            return R_RegisterSprite(s);

        if (strchr(s, '/'))
            return R_RegisterSkin(s);
    }

    // Kingpin puts the download list in the image slots before spawn.
    // Those paths are sounds and maps, not pictures.
    if (cls.serverProtocol == PROTOCOL_VERSION_KINGPIN && strchr(s, '/')) {
        const char *ext = COM_FileExtension(s);

        if (ext && strcmp(ext, ".pcx") && strcmp(ext, ".tga") &&
            strcmp(ext, ".wal") && strcmp(ext, ".jpg") && strcmp(ext, ".png"))
            return 0;
        return R_RegisterSkin(s);
    }

    return R_RegisterTempPic(s);
}

/*
=================
CL_PrepRefresh

Call before entering a new level, or after changing dlls
=================
*/
void CL_PrepRefresh(void)
{
    int         i;
    char        *name;

    if (!cls.ref_initialized)
        return;
    if (!cl.mapname[0])
        return;     // no map loaded

    // register models, pics, and skins
    R_BeginRegistration(cl.mapname);

    CL_RegisterVWepModels();

    CL_LoadState(LOAD_MODELS);

    CL_RegisterTEntModels();

    for (i = 2; i < cl.csr.max_models; i++) {
        name = cl.configstrings[cl.csr.models + i];
        if (!name[0] && i != MODELINDEX_PLAYER) {
            break;
        }
        if (name[0] == '#') {
            continue;
        }
        cl.model_draw[i] = R_RegisterModel(name);
    }

    CL_LoadState(LOAD_IMAGES);
    for (i = 1; i < cl.csr.max_images; i++) {
        name = cl.configstrings[cl.csr.images + i];
        if (!name[0]) {
            break;
        }
        cl.image_precache[i] = CL_RegisterImage(name);
    }

    CL_LoadState(LOAD_CLIENTS);
    for (i = 0; i < MAX_CLIENTS; i++) {
        name = cl.configstrings[cl.csr.playerskins + i];
        if (!name[0]) {
            continue;
        }
        CL_LoadClientinfo(&cl.clientinfo[i], name);
    }

    if (cls.serverProtocol == PROTOCOL_VERSION_KINGPIN)
        CL_LoadClientinfo(&cl.baseclientinfo, "unnamed\\male_thug/016 016 016");
    else
        CL_LoadClientinfo(&cl.baseclientinfo, "unnamed\\male/grunt");

    // set sky textures and speed
    CL_SetSky();

    // the renderer can now free unneeded stuff
    R_EndRegistration();

    // clear any lines of console text
    Con_ClearNotify_f();

    SCR_UpdateScreen();

    // start the cd track
    OGG_Play();
}

/*
=================
CL_UpdateConfigstring

A configstring update has been parsed.
=================
*/
void CL_UpdateConfigstring(int index)
{
    const char *s = cl.configstrings[index];

    if (index == cl.csr.maxclients) {
        cl.maxclients = Q_atoi(s);
        return;
    }

    if (index == cl.csr.airaccel) {
        // Kingpin stores the server version here, not an air-accel flag.
        if (cls.serverProtocol != PROTOCOL_VERSION_KINGPIN)
            cl.pmp.airaccelerate = cl.pmp.qwmode || Q_atoi(s);
        return;
    }

    if (index == cl.csr.models + 1) {
        if (!Com_ParseMapName(cl.mapname, s, sizeof(cl.mapname)))
            Com_Error(ERR_DROP, "%s: bad world model: %s", __func__, s);
        return;
    }

    if (index >= cl.csr.lights && index < cl.csr.lights + MAX_LIGHTSTYLES) {
        CL_SetLightStyle(index - cl.csr.lights, s);
        return;
    }

    if (cls.state < ca_precached) {
        return;
    }

    if (index >= cl.csr.models + 2 && index < cl.csr.models + cl.csr.max_models) {
        int i = index - cl.csr.models;

        cl.model_draw[i] = R_RegisterModel(s);
        if (*s == '*')
            cl.model_clip[i] = BSP_InlineModel(cl.bsp, s);
        else
            cl.model_clip[i] = NULL;
        return;
    }

    if (index >= cl.csr.sounds && index < cl.csr.sounds + cl.csr.max_sounds) {
        cl.sound_precache[index - cl.csr.sounds] = S_RegisterSound(s);
        return;
    }

    if (index >= cl.csr.images && index < cl.csr.images + cl.csr.max_images) {
        cl.image_precache[index - cl.csr.images] = CL_RegisterImage(s);
        return;
    }

    if (index >= cl.csr.playerskins && index < cl.csr.playerskins + MAX_CLIENTS) {
        CL_LoadClientinfo(&cl.clientinfo[index - cl.csr.playerskins], s);
        return;
    }

    if (index == CS_CDTRACK) {
        OGG_Play();
        return;
    }

    if (index == CS_SKYROTATE || index == CS_SKYAXIS) {
        CL_SetSky();
        return;
    }
}
