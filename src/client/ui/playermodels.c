/*
Copyright (C) 1997-2001 Id Software, Inc.
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
#include "common/files.h"

/*
=============================================================================

PLAYER MODELS (KINGPIN)

=============================================================================
*/

static int skin_cmp(const void *_a, const void *_b)
{
    const char *a = *(const char **)_a;
    const char *b = *(const char **)_b;

    return Q_stricmp(a, b);
}

typedef struct {
    char **items;
    int count;
    int capacity;
} skin_list_t;

static void AddSkinCode(skin_list_t *list, const char *code)
{
    int i;

    for (i = 0; i < list->count; i++) {
        if (!Q_stricmp(list->items[i], code))
            return;
    }

    if (list->count >= list->capacity) {
        int new_cap = list->capacity ? list->capacity * 2 : 64;
        char **new_items = Z_Malloc(new_cap * sizeof(char *));
        if (list->items) {
            memcpy(new_items, list->items, list->count * sizeof(char *));
            Z_Free(list->items);
        }
        list->items = new_items;
        list->capacity = new_cap;
    }

    list->items[list->count++] = Z_CopyString(code);
}

static void ScanDirectoryForSkins(skin_list_t *list, const char *path, const char *sub, const char *part)
{
    char prefix[32];
    int prefix_len;
    char **filenames;
    int nfiles = 0;
    int i;

    prefix_len = Q_snprintf(prefix, sizeof(prefix), "%s_", part);

    filenames = (char **)FS_ListFiles(path, ".tga;.pcx", 0, &nfiles);
    if (filenames && nfiles > 0) {
        for (i = 0; i < nfiles; i++) {
            if (!Q_strncasecmp(filenames[i], prefix, prefix_len)) {
                char code[MAX_QPATH];
                char full_code[MAX_QPATH];
                COM_StripExtension(code, filenames[i] + prefix_len, sizeof(code));
                if (sub && sub[0])
                    Q_snprintf(full_code, sizeof(full_code), "%s/%s", sub, code);
                else
                    Q_strlcpy(full_code, code, sizeof(full_code));
                AddSkinCode(list, full_code);
            }
        }
        FS_FreeList((void **)filenames);
    }
}

static void ScanSubdirectories(skin_list_t *list, const char *basepath, const char *part)
{
    char **subdirs;
    int nsub = 0;
    int i;

    subdirs = (char **)FS_ListFiles(basepath, NULL, FS_SEARCH_DIRSONLY, &nsub);
    if (!subdirs || nsub <= 0) {
        if (subdirs)
            FS_FreeList((void **)subdirs);
        return;
    }

    for (i = 0; i < nsub; i++) {
        char subpath[MAX_QPATH];
        if (!COM_IsPath(subdirs[i]))
            continue;
        if (Q_snprintf(subpath, sizeof(subpath), "%s/%s", basepath, subdirs[i]) < (int)sizeof(subpath)) {
            ScanDirectoryForSkins(list, subpath, subdirs[i], part);
        }
    }

    FS_FreeList((void **)subdirs);
}

static char **CollectSkinsCombined(const char *model, const char *folder, const char *part, int *count_out)
{
    skin_list_t list = { 0 };
    char model_dir[MAX_QPATH];
    char **out_skins;
    int i;

    *count_out = 0;

    // 1. Scan primary folder from skinfolder.txt
    if (folder && folder[0]) {
        ScanDirectoryForSkins(&list, folder, NULL, part);
        ScanSubdirectories(&list, folder, part);
    }

    // 2. Scan players/<model> folder
    Q_snprintf(model_dir, sizeof(model_dir), "players/%s", model);
    if (!folder || Q_stricmp(folder, model_dir) != 0) {
        ScanDirectoryForSkins(&list, model_dir, NULL, part);
        ScanSubdirectories(&list, model_dir, part);
    }

    // 3. Fallback if empty
    if (!list.count) {
        AddSkinCode(&list, "001");
    }

    // 4. Sort skins alphabetically
    if (list.count > 1) {
        qsort(list.items, list.count, sizeof(char *), skin_cmp);
    }

    // 5. Transfer to UI memory
    out_skins = UI_Malloc(sizeof(char *) * (list.count + 1));
    for (i = 0; i < list.count; i++) {
        out_skins[i] = UI_CopyString(list.items[i]);
        Z_Free(list.items[i]);
    }
    out_skins[list.count] = NULL;
    if (list.items)
        Z_Free(list.items);

    *count_out = list.count;
    return out_skins;
}

static int pmicmpfnc(const void *_a, const void *_b)
{
    const playerModelInfo_t *a = (const playerModelInfo_t *)_a;
    const playerModelInfo_t *b = (const playerModelInfo_t *)_b;

    if (!Q_stricmp(a->directory, b->directory))
        return 0;

    /* Prioritize primary Kingpin classes */
    if (!Q_stricmp(a->directory, "male_thug")) return -1;
    if (!Q_stricmp(b->directory, "male_thug")) return 1;

    if (!Q_stricmp(a->directory, "female_bitch")) return -1;
    if (!Q_stricmp(b->directory, "female_bitch")) return 1;

    if (!Q_stricmp(a->directory, "female_chick")) return -1;
    if (!Q_stricmp(b->directory, "female_chick")) return 1;

    if (!Q_stricmp(a->directory, "male_runt")) return -1;
    if (!Q_stricmp(b->directory, "male_runt")) return 1;

    return Q_stricmp(a->directory, b->directory);
}

void PlayerModel_Load(void)
{
    char scratch[MAX_QPATH];
    int i, ndirs;
    char **dirnames;
    playerModelInfo_t *pmi;

    Q_assert(!uis.numPlayerModels);

    if (!(dirnames = (char **)FS_ListFiles("players", NULL, FS_SEARCH_DIRSONLY, &ndirs)))
        return;

    for (i = 0; i < ndirs; i++) {
        char head_dir[MAX_QPATH], body_dir[MAX_QPATH], legs_dir[MAX_QPATH];

        /* Verify Kingpin 3-part model or Quake 2 tris model */
        Q_concat(scratch, sizeof(scratch), "players/", dirnames[i], "/body.mdx");
        if (!FS_FileExists(scratch)) {
            Q_concat(scratch, sizeof(scratch), "players/", dirnames[i], "/tris.md2");
            if (!FS_FileExists(scratch))
                continue;
        }

        CL_KingpinSkinDirs(dirnames[i], head_dir, body_dir, legs_dir, sizeof(head_dir));

        pmi = &uis.pmi[uis.numPlayerModels++];
        pmi->directory = UI_CopyString(dirnames[i]);
        pmi->head_skins = CollectSkinsCombined(dirnames[i], head_dir, "head", &pmi->nhead);
        pmi->body_skins = CollectSkinsCombined(dirnames[i], body_dir, "body", &pmi->nbody);
        pmi->legs_skins = CollectSkinsCombined(dirnames[i], legs_dir, "legs", &pmi->nlegs);

        if (uis.numPlayerModels == MAX_PLAYERMODELS)
            break;
    }

    FS_FreeList((void **)dirnames);

    if (uis.numPlayerModels > 1)
        qsort(uis.pmi, uis.numPlayerModels, sizeof(uis.pmi[0]), pmicmpfnc);
}

void PlayerModel_Free(void)
{
    playerModelInfo_t *pmi;
    int i, j;

    for (i = 0, pmi = uis.pmi; i < uis.numPlayerModels; i++, pmi++) {
        if (pmi->head_skins) {
            for (j = 0; j < pmi->nhead; j++)
                Z_Free(pmi->head_skins[j]);
            Z_Free(pmi->head_skins);
        }
        if (pmi->body_skins) {
            for (j = 0; j < pmi->nbody; j++)
                Z_Free(pmi->body_skins[j]);
            Z_Free(pmi->body_skins);
        }
        if (pmi->legs_skins) {
            for (j = 0; j < pmi->nlegs; j++)
                Z_Free(pmi->legs_skins[j]);
            Z_Free(pmi->legs_skins);
        }
        Z_Free(pmi->directory);
        memset(pmi, 0, sizeof(*pmi));
    }

    uis.numPlayerModels = 0;
}
