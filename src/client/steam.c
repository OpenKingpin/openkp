/*
===========================================================================
Copyright (C) 2026 OpenKP developers

This file is part of OpenKP.

OpenKP is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenKP; if not, write to the Free Software Foundation,
Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
===========================================================================
*/

#include "client.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#else
#include <stdlib.h>
#endif

#define KINGPIN_STEAM_APPID_STR "38430"
#define KINGPIN_STEAM_APPID     38430

typedef bool (*SteamAPI_Init_fn)(void);
typedef void (*SteamAPI_Shutdown_fn)(void);
typedef void (*SteamAPI_RunCallbacks_fn)(void);
typedef bool (*SteamAPI_IsSteamRunning_fn)(void);

static void *steam_handle = NULL;
static SteamAPI_Init_fn pfnSteamAPI_Init = NULL;
static SteamAPI_Shutdown_fn pfnSteamAPI_Shutdown = NULL;
static SteamAPI_RunCallbacks_fn pfnSteamAPI_RunCallbacks = NULL;
static SteamAPI_IsSteamRunning_fn pfnSteamAPI_IsSteamRunning = NULL;

static bool steam_active = false;
static cvar_t *cl_steam;

/**
 * @brief Dynamically discovers and initializes the Steamworks API for Kingpin.
 *
 * OpenKP is completely DRM-free: if the user does not own Kingpin on Steam, or if
 * the Steam client is not running, OpenKP gracefully operates in standalone mode.
 *
 * Initialization process:
 * 1. Checks for GOG Galaxy installation metadata (`goggame-1207658742.info`).
 * 2. Sets environment variables `SteamAppId` and `SteamGameId` to 38430 (Kingpin: Life of Crime).
 * 3. Generates `steam_appid.txt` if not present in the current working directory.
 * 4. Dynamically probes for `steam_api` shared libraries across platform paths.
 * 5. Resolves API function pointers (`SteamAPI_InitSafe`, `SteamAPI_Shutdown`, etc.).
 * 6. Checks if the Steam client is active via `SteamAPI_IsSteamRunning()`.
 * 7. Calls `SteamAPI_Init()` to enable Steam overlay, presence, and friend integration.
 */
void CL_Steam_Init(void)
{
    cl_steam = Cvar_Get("cl_steam", "1", CVAR_ARCHIVE);
    if (!cl_steam->integer)
        return;

    // Check GOG Galaxy installation presence
    if (FS_FileExists("goggame-1207658742.info") || FS_FileExists("../goggame-1207658742.info")) {
        Com_Printf("[GOG] Detected GOG Galaxy Kingpin installation (Product ID: 1207658742)\n");
    }

    // Set Steam environment variables so Steam client / Steamworks knows the AppID
#if defined(_WIN32)
    SetEnvironmentVariableA("SteamAppId", KINGPIN_STEAM_APPID_STR);
    SetEnvironmentVariableA("SteamGameId", KINGPIN_STEAM_APPID_STR);
    _putenv("SteamAppId=" KINGPIN_STEAM_APPID_STR);
    _putenv("SteamGameId=" KINGPIN_STEAM_APPID_STR);
#else
    setenv("SteamAppId", KINGPIN_STEAM_APPID_STR, 0);
    setenv("SteamGameId", KINGPIN_STEAM_APPID_STR, 0);
#endif

    // If steam_appid.txt is missing in the current working directory, create it
    if (!FS_FileExists("steam_appid.txt")) {
        FILE *f = fopen("steam_appid.txt", "w");
        if (f) {
            fputs(KINGPIN_STEAM_APPID_STR "\n", f);
            fclose(f);
        }
    }

    // Candidate Steam library names across platforms
    static const char *const steam_libs[] = {
#if defined(_WIN32)
#if defined(_WIN64)
        "steam_api64.dll",
#else
        "steam_api.dll",
#endif
        "steam_api64.dll",
        "steam_api.dll",
#elif defined(__APPLE__)
        "libsteam_api.dylib",
#else
        "libsteam_api.so",
#endif
        NULL
    };

    // Try loading Steam API dynamically
    for (int i = 0; steam_libs[i]; i++) {
        // Direct / system load
        Sys_LoadLibrary(steam_libs[i], NULL, &steam_handle);
        if (steam_handle)
            break;

        // Try local ./ relative path (needed on Linux/POSIX for dlopen)
        char local_path[MAX_QPATH];
        Q_snprintf(local_path, sizeof(local_path), "./%s", steam_libs[i]);
        Sys_LoadLibrary(local_path, NULL, &steam_handle);
        if (steam_handle)
            break;

        // Try parent directory
        char parent_path[MAX_QPATH];
        Q_snprintf(parent_path, sizeof(parent_path), "../%s", steam_libs[i]);
        Sys_LoadLibrary(parent_path, NULL, &steam_handle);
        if (steam_handle)
            break;

        // Try sys_libdir (executable directory)
        if (sys_libdir && sys_libdir->string[0]) {
            char lib_path[MAX_QPATH];
            Q_snprintf(lib_path, sizeof(lib_path), "%s/%s", sys_libdir->string, steam_libs[i]);
            Sys_LoadLibrary(lib_path, NULL, &steam_handle);
            if (steam_handle)
                break;
        }

        // Try game directory
        if (fs_gamedir[0]) {
            char path[MAX_QPATH];
            Q_snprintf(path, sizeof(path), "%s/%s", fs_gamedir, steam_libs[i]);
            Sys_LoadLibrary(path, NULL, &steam_handle);
            if (steam_handle)
                break;
        }
    }

    if (!steam_handle) {
        Com_DPrintf("[Steam] steam_api library not found; running DRM-free standalone.\n");
        return;
    }

    pfnSteamAPI_Init = (SteamAPI_Init_fn)Sys_GetProcAddress(steam_handle, "SteamAPI_InitSafe");
    if (!pfnSteamAPI_Init)
        pfnSteamAPI_Init = (SteamAPI_Init_fn)Sys_GetProcAddress(steam_handle, "SteamAPI_Init");
    pfnSteamAPI_Shutdown = (SteamAPI_Shutdown_fn)Sys_GetProcAddress(steam_handle, "SteamAPI_Shutdown");
    pfnSteamAPI_RunCallbacks = (SteamAPI_RunCallbacks_fn)Sys_GetProcAddress(steam_handle, "SteamAPI_RunCallbacks");
    pfnSteamAPI_IsSteamRunning = (SteamAPI_IsSteamRunning_fn)Sys_GetProcAddress(steam_handle, "SteamAPI_IsSteamRunning");

    if (!pfnSteamAPI_Init || !pfnSteamAPI_Shutdown || !pfnSteamAPI_RunCallbacks) {
        Com_DPrintf("[Steam] Failed to resolve Steamworks entry points.\n");
        Sys_FreeLibrary(steam_handle);
        steam_handle = NULL;
        return;
    }

    // Check if Steam client is actually running before initializing
    if (pfnSteamAPI_IsSteamRunning && !pfnSteamAPI_IsSteamRunning()) {
        Com_DPrintf("[Steam] Steam client is not running.\n");
        Sys_FreeLibrary(steam_handle);
        steam_handle = NULL;
        return;
    }

    if (pfnSteamAPI_Init()) {
        steam_active = true;
        Com_Printf("[Steam] Initialized successfully. Playing Kingpin: Life of Crime (AppID %d)\n", KINGPIN_STEAM_APPID);
    } else {
        Com_DPrintf("[Steam] SteamAPI_Init() returned false.\n");
        Sys_FreeLibrary(steam_handle);
        steam_handle = NULL;
    }
}

/**
 * @brief Pumps Steamworks asynchronous callback dispatch once per client frame.
 *
 * Dispatches Steam callbacks for friend invites, auth tickets, and overlay states.
 */
void CL_Steam_Frame(void)
{
    if (steam_active && pfnSteamAPI_RunCallbacks) {
        pfnSteamAPI_RunCallbacks();
    }
}

/**
 * @brief Unloads the Steamworks runtime and releases dynamic library handles.
 */
void CL_Steam_Shutdown(void)
{
    if (steam_active) {
        if (pfnSteamAPI_Shutdown)
            pfnSteamAPI_Shutdown();
        steam_active = false;
        Com_Printf("[Steam] Shutdown.\n");
    }

    if (steam_handle) {
        Sys_FreeLibrary(steam_handle);
        steam_handle = NULL;
    }

    pfnSteamAPI_Init = NULL;
    pfnSteamAPI_Shutdown = NULL;
    pfnSteamAPI_RunCallbacks = NULL;
    pfnSteamAPI_IsSteamRunning = NULL;
}
