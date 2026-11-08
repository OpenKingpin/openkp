/*
Copyright (C) 2012 Andrey Nazarov

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
#include "common/async.h"
#include "common/files.h"
#include "common/msg.h"
#include "common/zone.h"
#include "common/net/net.h"
#include "common/net/chan.h"
#include "client/client.h"
#include "client/video.h"
#include "system/system.h"

/*
=============================================================================

SERVERS MENU

=============================================================================
*/

#define MAX_STATUS_RULES    64
#define MAX_STATUS_SERVERS  1024

#define SLOT_EXTRASIZE  offsetof(serverslot_t, name)

#define COL_NAME    0
#define COL_MOD     1
#define COL_MAP     2
#define COL_PLAYERS 3
#define COL_RTT     4
#define COL_MAX     5

// how many times to (re)ping
#define PING_STAGES     3

typedef enum {
    SLOT_IDLE,
    SLOT_PENDING,
    SLOT_ERROR,
    SLOT_VALID
} slotStatus_t;

typedef struct {
    slotStatus_t status;
    netadr_t    address;
    char        *hostname; // original domain name, only used for favorites
    char        *mod;      // mod directory, e.g. "ctf", "crash", or "" for main
    bool        isFavorite;
    int         numRules;
    char        *rules[MAX_STATUS_RULES];
    int         numPlayers;
    char        *players[MAX_STATUS_PLAYERS];
    unsigned    timestamp;
    uint32_t    color;
    char        name[1];
} serverslot_t;

typedef struct {
    netadr_t        address;
    char            *hostname;
    bool            isFavorite;
    slotStatus_t    status;
    unsigned        timestamp;
} queryserver_t;

typedef struct {
    menuFrameWork_t menu;
    menuList_t      list;
    menuList_t      info;
    menuList_t      players;
    void            *names[MAX_STATUS_SERVERS];
    queryserver_t   query[MAX_STATUS_SERVERS];
    int             num_query;
    char            *args;
    unsigned        timestamp;
    int             pingstage;
    int             pingindex;
    int             pingtime;
    int             pingextra;
    const char      *status_c;
    char            status_r[32];
} m_servers_t;

static m_servers_t  m_servers;

typedef struct {
    const char  *name;
    netadr_t    adr;
    unsigned    time;
    int         tries;
    bool        resolved;
    bool        done;
} kp_master_t;

static const char *const kp_master_hosts[] = {
    "master.kingpin.info",
    "master0.kingpin.info",
    "master1.kingpin.info",
    "master2.kingpin.info",
    "master3.kingpin.info",
    "master4.kingpin.info",
    "master2.shavargo.com",
};

static kp_master_t  kp_masters[q_countof(kp_master_hosts)];
static bool         masters_open;

static cvar_t   *ui_sortservers;
static cvar_t   *ui_colorservers;
static cvar_t   *ui_pingrate;

static void UpdateSelection(void)
{
    serverslot_t *s = NULL;

    if (m_servers.list.numItems) {
        if (m_servers.list.curvalue >= 0 && m_servers.list.curvalue < m_servers.list.numItems) {
            s = m_servers.list.items[m_servers.list.curvalue];
            if (s->status == SLOT_VALID) {
                m_servers.status_c = "Press Enter to connect; Space to refresh";
            } else {
                m_servers.status_c = "Press Space to refresh; Alt+Space to refresh all";
            }
        } else if (m_servers.pingstage) {
            m_servers.status_c = "Pinging servers; Press Backspace to abort";
        } else {
            m_servers.status_c = "Select a server; Press Alt+Space to refresh";
        }
    } else if (m_servers.pingstage) {
        m_servers.status_c = "Pinging servers; Press Backspace to abort";
    } else {
        m_servers.status_c = "No online servers found; Press Space to refresh";
    }

    if (s && s->status == SLOT_VALID && s->numRules && uis.width >= 500) {
        m_servers.info.generic.flags &= ~QMF_HIDDEN;
        if (m_servers.info.items != (void **)s->rules || m_servers.info.numItems != s->numRules) {
            m_servers.info.items = (void **)s->rules;
            m_servers.info.numItems = s->numRules;
            m_servers.info.curvalue = -1;
            m_servers.info.prestep = 0;
        }
    } else {
        m_servers.info.generic.flags |= QMF_HIDDEN;
        m_servers.info.items = NULL;
        m_servers.info.numItems = 0;
    }

    if (s && s->status == SLOT_VALID) {
        m_servers.players.generic.flags &= ~QMF_HIDDEN;
        if (m_servers.players.items != (void **)s->players || m_servers.players.numItems != s->numPlayers) {
            m_servers.players.items = (void **)s->players;
            m_servers.players.numItems = s->numPlayers;
            m_servers.players.curvalue = -1;
            m_servers.players.prestep = 0;
        }
    } else {
        m_servers.players.generic.flags |= QMF_HIDDEN;
        m_servers.players.items = NULL;
        m_servers.players.numItems = 0;
    }
}

static void UpdateStatus(void)
{
    serverslot_t *slot;
    int i, totalplayers = 0, totalservers = 0;

    for (i = 0; i < m_servers.list.numItems; i++) {
        slot = m_servers.list.items[i];
        if (slot->status == SLOT_VALID) {
            totalservers++;
            totalplayers += slot->numPlayers;
        }
    }

    Q_snprintf(m_servers.status_r, sizeof(m_servers.status_r),
               "%d player%s on %d server%s",
               totalplayers, totalplayers == 1 ? "" : "s",
               totalservers, totalservers == 1 ? "" : "s");
}

// doesn't free hostname!
static void FreeSlot(serverslot_t *slot)
{
    int i;

    for (i = 0; i < slot->numRules; i++)
        Z_Free(slot->rules[i]);
    for (i = 0; i < slot->numPlayers; i++)
        Z_Free(slot->players[i]);
    Z_Free(slot->mod);
    Z_Free(slot);
}

static queryserver_t *FindQuery(const netadr_t *search)
{
    int i;
    for (i = 0; i < m_servers.num_query; i++) {
        queryserver_t *q = &m_servers.query[i];
        if (!NET_IsEqualBaseAdr(search, &q->address))
            continue;
        if (search->port && search->port != q->address.port)
            continue;
        return q;
    }
    return NULL;
}

static serverslot_t *FindSlot(const netadr_t *search, int *index_p)
{
    serverslot_t *slot, *found = NULL;
    int i;

    for (i = 0; i < m_servers.list.numItems; i++) {
        slot = m_servers.list.items[i];
        if (!NET_IsEqualBaseAdr(search, &slot->address))
            continue;
        if (search->port && search->port != slot->address.port)
            continue;
        found = slot;
        break;
    }

    if (index_p)
        *index_p = i;
    return found;
}

static uint32_t ColorForStatus(const serverStatus_t *status, unsigned ping)
{
    if (Q_atoi(Info_ValueForKey(status->infostring, "needpass")) >= 1)
        return uis.color.disabled.u32;

    if (Q_atoi(Info_ValueForKey(status->infostring, "anticheat")) >= 2)
        return uis.color.disabled.u32;

    if (Q_stricmp(Info_ValueForKey(status->infostring, "NoFake"), "ENABLED") == 0)
        return uis.color.disabled.u32;

    if (ping < 30)
        return U32_GREEN;

    return U32_WHITE;
}

/*
=================
UI_StatusEvent

A server status response has been received, validated and parsed.
=================
*/
void UI_StatusEvent(const serverStatus_t *status)
{
    serverslot_t *slot;
    char *hostname;
    char host[MAX_INFO_STRING];
    char map[MAX_INFO_STRING];
    char maxclients[MAX_INFO_STRING];
    char mod_dir[MAX_INFO_STRING];
    char gamename[MAX_INFO_STRING];
    char cashlimit[MAX_INFO_STRING];
    const char *mod_display = "DM";
    unsigned timestamp, ping;
    const char *info = status->infostring;
    char key[MAX_INFO_STRING];
    char value[MAX_INFO_STRING];
    int i;

    // ignore unless menu is up
    if (!m_servers.args) {
        return;
    }

    // Filter non-Kingpin servers: Kingpin is strictly protocol 32.
    // Master servers that index multiple games (e.g. GameSpy v0) may return
    // servers for other games (like Quake 2 protocol 34). Reject them.
    const char *proto = Info_ValueForKey(info, "protocol");
    if (!proto || Q_atoi(proto) != PROTOCOL_VERSION_KINGPIN) {
        queryserver_t *q = FindQuery(&net_from);
        if (q)
            q->status = SLOT_ERROR;
        slot = FindSlot(&net_from, &i);
        if (slot) {
            Z_Free(slot->hostname);
            FreeSlot(slot);
            m_servers.list.numItems--;
            for (; i < m_servers.list.numItems; i++)
                m_servers.list.items[i] = m_servers.list.items[i + 1];
            m_servers.list.items[m_servers.list.numItems] = NULL;
            UpdateSelection();
        }
        return;
    }

    queryserver_t *q = FindQuery(&net_from);
    bool isFavorite = false;
    if (q) {
        q->status = SLOT_VALID;
        hostname = q->hostname;
        timestamp = q->timestamp;
        isFavorite = q->isFavorite;
    } else {
        hostname = (char *)NET_AdrToString(&net_from);
        timestamp = m_servers.timestamp;
    }

    // see if already added in visible online list
    slot = FindSlot(&net_from, &i);
    if (!slot) {
        if (m_servers.list.numItems >= MAX_STATUS_SERVERS) {
            return;
        }
        i = m_servers.list.numItems++;
        hostname = UI_CopyString(hostname);
    } else {
        hostname = slot->hostname;
        timestamp = slot->timestamp;
        isFavorite = slot->isFavorite;
        FreeSlot(slot);
    }

    Q_strlcpy(host, Info_ValueForKey(info, "hostname"), sizeof(host));
    if (COM_IsWhite(host)) {
        Q_strlcpy(host, hostname, sizeof(host));
    }

    Q_strlcpy(mod_dir, Info_ValueForKey(info, "game"), sizeof(mod_dir));
    Q_strlcpy(gamename, Info_ValueForKey(info, "gamename"), sizeof(gamename));
    Q_strlcpy(cashlimit, Info_ValueForKey(info, "cashlimit"), sizeof(cashlimit));

    if (!COM_IsWhite(mod_dir) && Q_stricmp(mod_dir, "main") != 0 && COM_IsPath(mod_dir)) {
        if (!Q_stricmp(mod_dir, "ctf"))
            mod_display = "CTF";
        else if (!Q_stricmp(mod_dir, "crash"))
            mod_display = "Crash";
        else if (!Q_stricmp(mod_dir, "gunrace"))
            mod_display = "GunRace";
        else if (!Q_stricmp(mod_dir, "riotz"))
            mod_display = "Riotz";
        else if (!Q_stricmp(mod_dir, "speedway"))
            mod_display = "Speedway";
        else if (!Q_stricmp(mod_dir, "kitmod"))
            mod_display = "KitMod";
        else if (!Q_stricmp(mod_dir, "coop"))
            mod_display = "Coop";
        else if (!Q_stricmp(mod_dir, "botmatch") || !Q_stricmp(mod_dir, "compbots"))
            mod_display = "Botmatch";
        else if (!Q_stricmp(mod_dir, "ctc"))
            mod_display = "CTC";
        else if (!Q_stricmp(mod_dir, "killconfirmed"))
            mod_display = "KillConf";
        else if (!Q_stricmp(mod_dir, "killapin"))
            mod_display = "Killapin";
        else if (!Q_stricmp(mod_dir, "kpz"))
            mod_display = "KPZ";
        else
            mod_display = mod_dir;
    } else {
        mod_dir[0] = 0;
        if (cashlimit[0] && Q_atoi(cashlimit) > 0)
            mod_display = "Bagman";
        else if (Q_stristr(gamename, "Bagman") || Q_stristr(gamename, "BM"))
            mod_display = "Bagman";
        else if (Q_stristr(gamename, "coop"))
            mod_display = "Coop";
        else if (Q_stristr(gamename, "botmatch"))
            mod_display = "DM+Bots";
        else
            mod_display = "DM";
    }

    Q_strlcpy(map, Info_ValueForKey(info, "mapname"), sizeof(map));
    if (COM_IsWhite(map)) {
        strcpy(map, "???");
    }

    Q_strlcpy(maxclients, Info_ValueForKey(info, "maxclients"), sizeof(maxclients));
    if (!COM_IsUint(maxclients)) {
        strcpy(maxclients, "?");
    }

    if (timestamp > com_eventTime)
        timestamp = com_eventTime;

    ping = com_eventTime - timestamp;
    if (ping > 999)
        ping = 999;

    slot = UI_FormatColumns(SLOT_EXTRASIZE, host, mod_display, map,
                            va("%d/%s", status->numPlayers, maxclients),
                            va("%u", ping),
                            NULL);
    slot->status = SLOT_VALID;
    slot->address = net_from;
    slot->hostname = hostname;
    slot->mod = UI_CopyString(mod_dir);
    slot->isFavorite = isFavorite;
    slot->color = ColorForStatus(status, ping);

    m_servers.list.items[i] = slot;

    slot->numRules = 0;
    while (slot->numRules < MAX_STATUS_RULES) {
        Info_NextPair(&info, key, value);
        if (!info)
            break;

        if (!key[0])
            strcpy(key, "<MISSING KEY>");

        if (!value[0])
            strcpy(value, "<MISSING VALUE>");

        slot->rules[slot->numRules++] =
            UI_FormatColumns(0, key, value, NULL);
    }

    slot->numPlayers = status->numPlayers;
    for (i = 0; i < status->numPlayers; i++) {
        slot->players[i] =
            UI_FormatColumns(0,
                             va("%d", status->players[i].score),
                             va("%d", status->players[i].ping),
                             status->players[i].name,
                             NULL);
    }

    slot->timestamp = timestamp;

    if (m_servers.list.curvalue < 0 && m_servers.list.numItems > 0)
        m_servers.list.curvalue = 0;

    if (m_servers.list.sortcol >= 0)
        m_servers.list.sort(&m_servers.list);

    UpdateStatus();
    UpdateSelection();
}

/*
=================
UI_ErrorEvent

An ICMP destination-unreachable error has been received.
=================
*/
void UI_ErrorEvent(const netadr_t *from)
{
    serverslot_t *slot;
    int i;

    // ignore unless menu is up
    if (!m_servers.args)
        return;

    queryserver_t *q = FindQuery(from);
    if (q)
        q->status = SLOT_ERROR;

    slot = FindSlot(from, &i);
    if (!slot)
        return;

    Z_Free(slot->hostname);
    FreeSlot(slot);
    m_servers.list.numItems--;
    for (; i < m_servers.list.numItems; i++)
        m_servers.list.items[i] = m_servers.list.items[i + 1];
    m_servers.list.items[m_servers.list.numItems] = NULL;

    if (m_servers.list.curvalue >= m_servers.list.numItems)
        m_servers.list.curvalue = m_servers.list.numItems - 1;

    UpdateStatus();
    UpdateSelection();
}

static menuSound_t SetRconAddress(void)
{
    serverslot_t *slot;

    if (!m_servers.list.numItems)
        return QMS_BEEP;
    if (m_servers.list.curvalue < 0)
        return QMS_BEEP;

    slot = m_servers.list.items[m_servers.list.curvalue];
    if (slot->status == SLOT_ERROR)
        return QMS_BEEP;

    Cvar_Set("rcon_address", slot->hostname);
    return QMS_OUT;
}

static menuSound_t CopyAddress(void)
{
    serverslot_t *slot;

    if (!m_servers.list.numItems)
        return QMS_BEEP;
    if (m_servers.list.curvalue < 0)
        return QMS_BEEP;

    slot = m_servers.list.items[m_servers.list.curvalue];

    if (vid && vid->set_clipboard_data)
        vid->set_clipboard_data(slot->hostname);
    return QMS_OUT;
}

static menuSound_t PingSelected(void)
{
    serverslot_t *slot;

    if (!m_servers.list.numItems)
        return QMS_BEEP;
    if (m_servers.list.curvalue < 0)
        return QMS_BEEP;

    slot = m_servers.list.items[m_servers.list.curvalue];
    slot->timestamp = com_eventTime;

    queryserver_t *q = FindQuery(&slot->address);
    if (q) {
        q->status = SLOT_PENDING;
        q->timestamp = com_eventTime;
    }

    CL_SendStatusRequest(&slot->address);
    return QMS_SILENT;
}

static void AddServer(const netadr_t *address, const char *hostname, bool isFavorite)
{
    netadr_t tmp;
    queryserver_t *q;

    if (m_servers.num_query >= MAX_STATUS_SERVERS)
        return;

    if (!address) {
        // either address or hostname can be NULL, but not both
        if (!hostname)
            return;

        if (!NET_StringToAdr(hostname, &tmp, PORT_SERVER)) {
            Com_Printf("Bad server address: %s\n", hostname);
            return;
        }

        address = &tmp;
    }

    // ignore if already queued
    if (FindQuery(address))
        return;

    if (!hostname)
        hostname = NET_AdrToString(address);

    // privileged ports are not allowed
    if (BigShort(address->port) < 1024) {
        Com_Printf("Bad server port: %s\n", hostname);
        return;
    }

    q = &m_servers.query[m_servers.num_query++];
    q->address = *address;
    q->hostname = UI_CopyString(hostname);
    q->isFavorite = isFavorite;
    q->status = SLOT_IDLE;
    q->timestamp = com_eventTime;
}

static void ParsePlain(void *data, size_t len, size_t chunk)
{
    char *list, *p;

    if (!data)
        return;

    list = data;
    while (*list) {
        p = strchr(list, '\n');
        if (p) {
            if (p > list && *(p - 1) == '\r')
                *(p - 1) = 0;
            *p = 0;
        }

        while (*list == ' ' || *list == '\t')
            list++;

        if (*list && *list != '#' && *list != '/')
            AddServer(NULL, list, false);

        if (!p)
            break;
        list = p + 1;
    }
}

static void ParseBinary(void *data, size_t len, size_t chunk)
{
    netadr_t address;
    byte *ptr;

    if (!data)
        return;

    memset(&address, 0, sizeof(address));
    address.type = NA_IP;

    ptr = data;
    while (len >= chunk) {
        memcpy(address.ip.u8, ptr, 4);
        memcpy(&address.port, ptr + 4, 2);
        ptr += chunk;
        len -= chunk;

        AddServer(&address, NULL, false);
    }
}

static void ParseAddressBook(void)
{
    cvar_t *var;
    int i;

    for (i = 0; i < MAX_STATUS_SERVERS; i++) {
        var = Cvar_FindVar(va("adr%i", i));
        if (!var)
            break;

        if (var->string[0])
            AddServer(NULL, var->string, true);
    }
}

static bool MasterWaiting(void)
{
    int i;

    for (i = 0; i < q_countof(kp_masters); i++) {
        if (kp_masters[i].resolved && !kp_masters[i].done)
            return true;
    }
    return false;
}

static void ParseCachedServers(void)
{
    void *data = NULL;
    int len = FS_LoadFile("servers_cache.txt", &data);
    if (len > 0) {
        ParsePlain(data, len, 0);
        FS_FreeFile(data);
    }
}

#define BACKUP_SERVERS_URL "https://raw.githubusercontent.com/shavargo/kingpin-servers-mirror/refs/heads/main/servers.txt"

#if USE_CURL
typedef struct {
    asyncwork_t work;
    void *data;
    int len;
} mirror_sync_t;

static void MirrorSync_Work(void *arg)
{
    mirror_sync_t *sync = (mirror_sync_t *)arg;
    sync->len = HTTP_FetchFile(BACKUP_SERVERS_URL, &sync->data);
}

static void MirrorSync_Done(void *arg)
{
    mirror_sync_t *sync = (mirror_sync_t *)arg;
    if (sync->len > 0 && sync->data) {
        void *local_data = NULL;
        int local_len = FS_LoadFile("servers_cache.txt", &local_data);
        bool changed = (local_len != sync->len || !local_data || memcmp(local_data, sync->data, sync->len) != 0);
        if (local_data)
            FS_FreeFile(local_data);

        if (changed) {
            FS_WriteFile("servers_cache.txt", sync->data, sync->len);
            Com_DPrintf("Updated servers_cache.txt from mirror (%d bytes)\n", sync->len);
        }
        HTTP_FreeFile(sync->data);
    }
    Z_Free(sync);
}

void UI_CheckServerMirrorCache(void)
{
    mirror_sync_t *sync = Z_Malloc(sizeof(mirror_sync_t));
    sync->work.work_cb = MirrorSync_Work;
    sync->work.done_cb = MirrorSync_Done;
    sync->work.cb_arg = sync;
    Com_QueueAsyncWork(&sync->work);
}
#else
void UI_CheckServerMirrorCache(void) {}
#endif

static void QueryKingpinMaster(void)
{
    unsigned now;
    int i, j;

    NET_Config(NET_CLIENT);
    memset(kp_masters, 0, sizeof(kp_masters));
    now = Sys_Milliseconds();
    masters_open = false;

    for (i = 0; i < q_countof(kp_master_hosts); i++) {
        kp_master_t *m = &kp_masters[i];
        netadr_t adr;

        m->name = kp_master_hosts[i];
        memset(&adr, 0, sizeof(adr));
        if (!NET_StringToAdr(m->name, &adr, PORT_MASTER)) {
            Com_Printf("Couldn't resolve %s.\n", m->name);
            continue;
        }

        for (j = 0; j < i; j++) {
            if (kp_masters[j].resolved && NET_IsEqualAdr(&kp_masters[j].adr, &adr))
                break;
        }
        if (j < i)
            continue;

        m->adr = adr;
        m->resolved = true;
        m->tries = 1;
        m->time = now;
        masters_open = true;
        Netchan_OutOfBand(NS_CLIENT, &m->adr, "query");
    }

    if (masters_open)
        m_servers.status_c = "Querying masters...";
}

static void ParseMasterArgs(netadr_t *broadcast)
{
    void *data;
    int len;
    void (*parse)(void *, size_t, size_t);
    size_t chunk;
    char *s, *p;
    int i, argc;

    Cmd_TokenizeString(m_servers.args, false);

    argc = Cmd_Argc();
    if (!argc) {
        // Favorites, cached mirror servers, and Kingpin UDP masters.
        ParseAddressBook();
        ParseCachedServers();
        QueryKingpinMaster();
        return;
    }

    for (i = 0; i < argc; i++) {
        s = Cmd_Argv(i);
        if (!*s)
            continue;

        // parse binary format specifier
        parse = ParsePlain;
        chunk = 0;
        if (*s == '+' || *s == '-') {
            parse = ParseBinary;
            chunk = strtoul(s, &p, 10);
            if (s == p) {
                chunk = 6;
                s = p + 1;
            } else {
                if (chunk < 6)
                    goto ignore;
                s = p;
            }
        }

        if (!strncmp(s, "file://", 7)) {
            len = FS_LoadFile(s + 7, &data);
            if (len < 0)
                continue;
            (*parse)(data, len, chunk);
            FS_FreeFile(data);
            continue;
        }

        if (!strncmp(s, "http://", 7) || !strncmp(s, "https://", 8)) {
#if USE_CURL
            len = HTTP_FetchFile(s, &data);
            if (len < 0)
                continue;
            (*parse)(data, len, chunk);
            HTTP_FreeFile(data);
#else
            Com_Printf("Can't fetch '%s', no HTTP support compiled in.\n", s);
#endif
            continue;
        }

        if (!strncmp(s, "favorites://", 12)) {
            ParseAddressBook();
            continue;
        }

        if (!strncmp(s, "broadcast://", 12)) {
            broadcast->type = NA_BROADCAST;
            broadcast->port = BigShort(PORT_SERVER);
            continue;
        }

        if (!strncmp(s, "quake2://", 9)) {
            AddServer(NULL, s + 9, false);
            continue;
        }

ignore:
        Com_Printf("Ignoring invalid master URL: %s\n", s);
    }
}

static void ClearServers(void)
{
    serverslot_t *slot;
    int i;

    for (i = 0; i < m_servers.list.numItems; i++) {
        slot = m_servers.list.items[i];
        m_servers.list.items[i] = NULL;
        Z_Free(slot->hostname);
        FreeSlot(slot);
    }
    m_servers.list.numItems = 0;
    m_servers.list.curvalue = -1;

    for (i = 0; i < m_servers.num_query; i++) {
        Z_Free(m_servers.query[i].hostname);
    }
    m_servers.num_query = 0;

    masters_open = false;
    memset(kp_masters, 0, sizeof(kp_masters));
    m_servers.list.prestep = 0;
    m_servers.info.items = NULL;
    m_servers.info.numItems = 0;
    m_servers.players.items = NULL;
    m_servers.players.numItems = 0;
    m_servers.pingstage = 0;
}

static void FinishPingStage(void)
{
    int i = 0;

    m_servers.pingstage = 0;
    m_servers.pingindex = 0;
    m_servers.pingextra = 0;

    // Prune any unreplied / dead slots that never answered with a valid status
    while (i < m_servers.list.numItems) {
        serverslot_t *s = m_servers.list.items[i];
        if (s->status != SLOT_VALID) {
            Z_Free(s->hostname);
            FreeSlot(s);
            m_servers.list.numItems--;
            for (int j = i; j < m_servers.list.numItems; j++)
                m_servers.list.items[j] = m_servers.list.items[j + 1];
            m_servers.list.items[m_servers.list.numItems] = NULL;
        } else {
            i++;
        }
    }

    if (m_servers.list.numItems > 0)
        m_servers.list.sort(&m_servers.list);

    if (m_servers.list.curvalue >= m_servers.list.numItems)
        m_servers.list.curvalue = m_servers.list.numItems - 1;
    if (m_servers.list.curvalue < 0 && m_servers.list.numItems > 0)
        m_servers.list.curvalue = 0;

    UpdateStatus();
    UpdateSelection();
}

static void CalcPingRate(void)
{
    extern cvar_t *info_rate;

    // don't allow more than 100 packets/sec
    int rate = Cvar_ClampInteger(ui_pingrate, 0, 100);

    // assume average 450 bytes per reply packet
    if (!rate)
        rate = Q_clip(info_rate->integer / 450, 1, 100);

    // drop rate by stage
    m_servers.pingtime = (1000 * PING_STAGES) / (rate * m_servers.pingstage);
}

static void BeginPing(void);

/*
=================
UI_Frame

=================
*/
void UI_Frame(int msec)
{
    if (masters_open) {
        unsigned now = Sys_Milliseconds();
        int i;

        for (i = 0; i < q_countof(kp_masters); i++) {
            kp_master_t *m = &kp_masters[i];

            if (!m->resolved || m->done)
                continue;
            if (now - m->time <= 1500)
                continue;
            if (m->tries < 3) {
                m->tries++;
                m->time = now;
                Netchan_OutOfBand(NS_CLIENT, &m->adr, "query");
            } else {
                m->done = true;
                Com_DPrintf("%s did not answer.\n", m->name);
            }
        }
        if (!MasterWaiting()) {
            masters_open = false;
            if (!m_servers.num_query)
                ParseCachedServers();
            if (!m_servers.pingstage)
                BeginPing();
        }
    }

    if (!m_servers.pingstage)
        return;

    m_servers.pingextra += msec;
    if (m_servers.pingextra < m_servers.pingtime)
        return;

    m_servers.pingextra -= m_servers.pingtime;

    // send out next status packet
    while (m_servers.pingindex < m_servers.num_query) {
        queryserver_t *q = &m_servers.query[m_servers.pingindex++];
        if (q->status > SLOT_PENDING)
            continue;
        q->status = SLOT_PENDING;
        q->timestamp = com_eventTime;
        CL_SendStatusRequest(&q->address);
        break;
    }

    if (m_servers.pingindex >= m_servers.num_query) {
        m_servers.pingindex = 0;
        if (--m_servers.pingstage == 0)
            FinishPingStage();
        else
            CalcPingRate();
    }
}

static void BeginPing(void)
{
    if (!m_servers.num_query) {
        FinishPingStage();
        return;
    }

    m_servers.pingstage = PING_STAGES;
    m_servers.pingindex = 0;
    m_servers.pingextra = 0;
    CalcPingRate();
}

void UI_AddMasterServers(void)
{
    kp_master_t *m;
    int before, i;
    size_t len;

    if (!m_servers.args)
        return;

    m = NULL;
    for (i = 0; i < q_countof(kp_masters); i++) {
        if (!kp_masters[i].resolved || kp_masters[i].done)
            continue;
        if (NET_IsEqualAdr(&net_from, &kp_masters[i].adr)) {
            m = &kp_masters[i];
            break;
        }
    }
    if (!m)
        return;

    before = m_servers.num_query;
    len = msg_read.cursize - msg_read.readcount;
    ParseBinary(msg_read.data + msg_read.readcount, len, 6);
    m->done = true;
    Com_Printf("%s: %d new, %d total\n", m->name,
               m_servers.num_query - before, m_servers.num_query);

    if (!MasterWaiting())
        masters_open = false;
    if (!m_servers.pingstage)
        BeginPing();
}

static void PingServers(void)
{
    netadr_t broadcast;

    S_StopAllSounds();

    ClearServers();
    UpdateStatus();

    // update status string now, because fetching and
    // resolving will take some time
    m_servers.status_c = "Resolving servers, please wait...";
    SCR_UpdateScreen();

    // fetch and resolve servers
    memset(&broadcast, 0, sizeof(broadcast));
    ParseMasterArgs(&broadcast);

    m_servers.timestamp = Sys_Milliseconds();

    // optionally ping broadcast
    if (broadcast.type)
        CL_SendStatusRequest(&broadcast);

    // Master replies arrive later. Favorites can ping in the meantime.
    if (MasterWaiting() && !m_servers.num_query)
        return;

    BeginPing();
}

static int statuscmp(serverslot_t *s1, serverslot_t *s2)
{
    if (s1->status == s2->status)
        return 0;
    if (s1->status != SLOT_VALID && s2->status == SLOT_VALID)
        return 1;
    if (s2->status != SLOT_VALID && s1->status == SLOT_VALID)
        return -1;
    return 0;
}

static int namecmp(serverslot_t *s1, serverslot_t *s2, int col)
{
    char *n1 = UI_GetColumn(s1->name, col);
    char *n2 = UI_GetColumn(s2->name, col);

    return Q_stricmp(n1, n2) * m_servers.list.sortdir;
}

static int pingcmp(serverslot_t *s1, serverslot_t *s2)
{
    int n1 = Q_atoi(UI_GetColumn(s1->name, COL_RTT));
    int n2 = Q_atoi(UI_GetColumn(s2->name, COL_RTT));

    return (n1 - n2) * m_servers.list.sortdir;
}

static int playercmp(serverslot_t *s1, serverslot_t *s2)
{
    return (s2->numPlayers - s1->numPlayers) * m_servers.list.sortdir;
}

static int addresscmp(serverslot_t *s1, serverslot_t *s2)
{
    if (s1->address.ip.u32 > s2->address.ip.u32)
        return 1;
    if (s1->address.ip.u32 < s2->address.ip.u32)
        return -1;
    if (s1->address.port > s2->address.port)
        return 1;
    if (s1->address.port < s2->address.port)
        return -1;
    return 0;
}

static int slotcmp(const void *p1, const void *p2)
{
    serverslot_t *s1 = *(serverslot_t **)p1;
    serverslot_t *s2 = *(serverslot_t **)p2;
    int r;

    // sort by validity
    r = statuscmp(s1, s2);
    if (r)
        return r;

    // sort by primary column
    switch (m_servers.list.sortcol) {
    case COL_NAME:
        break;
    case COL_MOD:
        r = namecmp(s1, s2, COL_MOD);
        break;
    case COL_MAP:
        r = namecmp(s1, s2, COL_MAP);
        break;
    case COL_PLAYERS:
        r = playercmp(s1, s2);
        break;
    case COL_RTT:
        r = pingcmp(s1, s2);
        break;
    }
    if (r)
        return r;

    // stabilize sort
    r = namecmp(s1, s2, COL_NAME);
    if (r)
        return r;

    return addresscmp(s1, s2);
}

static menuSound_t Sort(menuList_t *self)
{
    MenuList_Sort(&m_servers.list, 0, slotcmp);
    return QMS_SILENT;
}

static void ui_sortservers_changed(cvar_t *self)
{
    int i = Cvar_ClampInteger(self, -COL_MAX, COL_MAX);

    if (i > 0) {
        // ascending
        m_servers.list.sortdir = 1;
        m_servers.list.sortcol = i - 1;
    } else if (i < 0) {
        // descending
        m_servers.list.sortdir = -1;
        m_servers.list.sortcol = -i - 1;
    } else {
        // don't sort
        m_servers.list.sortdir = 1;
        m_servers.list.sortcol = -1;
    }

    if (m_servers.list.numItems) {
        m_servers.list.sort(&m_servers.list);
    }
}

static menuSound_t Connect(menuCommon_t *self)
{
    serverslot_t *slot;

    if (!m_servers.list.numItems)
        return QMS_BEEP;
    if (m_servers.list.curvalue < 0)
        return QMS_BEEP;

    slot = m_servers.list.items[m_servers.list.curvalue];
    if (slot->status == SLOT_ERROR)
        return QMS_BEEP;

    const char *target = (slot->mod && slot->mod[0]) ? slot->mod : "";
    if (Q_stricmp(fs_game->string, target) != 0) {
        Cbuf_AddText(&cmd_buffer, va("game \"%s\"\nconnect %s\n", target, slot->hostname));
    } else {
        Cbuf_AddText(&cmd_buffer, va("connect %s\n", slot->hostname));
    }
    return QMS_SILENT;
}

static menuSound_t Change(menuCommon_t *self)
{
    UpdateSelection();
    return QMS_MOVE;
}

static void SizeCompact(void)
{
    int margin_x = 4;
    int header_y = 24;
    int footer_h = 20;
    int content_w = uis.width - margin_x * 2;
    int content_h = uis.height - header_y - footer_h;
    int half_h = (content_h - 8) / 2;

//
// server list
//
    m_servers.list.generic.x            = margin_x;
    m_servers.list.generic.y            = header_y;
    m_servers.list.generic.height       = half_h;

    int col_ply = 7 * CONCHAR_WIDTH + MLIST_PADDING;
    int col_rtt = 3 * CONCHAR_WIDTH + MLIST_PADDING;
    int col_host = content_w - MLIST_SCROLLBAR_WIDTH - col_ply - col_rtt;
    if (col_host < 50)
        col_host = 50;

    m_servers.list.columns[0].width     = col_host;
    m_servers.list.columns[1].width     = 0;
    m_servers.list.columns[2].width     = 0;
    m_servers.list.columns[3].width     = col_ply;
    m_servers.list.columns[4].width     = col_rtt;

//
// player list
//
    m_servers.players.generic.x         = margin_x;
    m_servers.players.generic.y         = header_y + half_h + 8;
    m_servers.players.generic.height    = content_h - half_h - 8;

    int pcol_frg = 3 * CONCHAR_WIDTH + MLIST_PADDING;
    int pcol_rtt = 3 * CONCHAR_WIDTH + MLIST_PADDING;
    int pcol_name = content_w - MLIST_SCROLLBAR_WIDTH - (pcol_frg + pcol_rtt);
    if (pcol_name < 50)
        pcol_name = 50;

    m_servers.players.columns[0].width  = pcol_frg;
    m_servers.players.columns[1].width  = pcol_rtt;
    m_servers.players.columns[2].width  = pcol_name;

    m_servers.players.mlFlags           |= MLF_SCROLLBAR;
}

static void SizeFull(void)
{
    int margin_x = 8;
    int header_y = 18;
    int footer_h = 16;
    int content_w = uis.width - margin_x * 2;
    int content_h = uis.height - header_y - footer_h;

    // Right pane width (player list): ~26 chars wide
    int right_w = 26 * CONCHAR_WIDTH + MLIST_PADDING * 2 + MLIST_SCROLLBAR_WIDTH;
    if (right_w > content_w * 35 / 100)
        right_w = content_w * 35 / 100;
    if (right_w < 180)
        right_w = 180;

    int left_w = content_w - right_w - 8;
    int top_h = (content_h - 6) / 2;
    int bottom_h = content_h - top_h - 6;

//
// server list
//
    m_servers.list.generic.x            = margin_x;
    m_servers.list.generic.y            = header_y;
    m_servers.list.generic.height       = top_h;

    int col_rtt = 4 * CONCHAR_WIDTH + MLIST_PADDING;
    int col_ply = 7 * CONCHAR_WIDTH + MLIST_PADDING;
    int col_map = 16 * CONCHAR_WIDTH + MLIST_PADDING;
    int col_mod = 9 * CONCHAR_WIDTH + MLIST_PADDING;
    int col_fixed = col_mod + col_map + col_ply + col_rtt + MLIST_SCROLLBAR_WIDTH;
    int col_host = left_w - col_fixed;
    if (col_host < 80)
        col_host = 80;

    m_servers.list.columns[0].width     = col_host;
    m_servers.list.columns[1].width     = col_mod;
    m_servers.list.columns[2].width     = col_map;
    m_servers.list.columns[3].width     = col_ply;
    m_servers.list.columns[4].width     = col_rtt;

//
// server info
//
    m_servers.info.generic.x            = margin_x;
    m_servers.info.generic.y            = header_y + top_h + 6;
    m_servers.info.generic.height       = bottom_h;

    int info_key_w = left_w * 35 / 100;
    m_servers.info.columns[0].width     = info_key_w;
    m_servers.info.columns[1].width     = left_w - info_key_w - MLIST_SCROLLBAR_WIDTH;

//
// player list
//
    m_servers.players.generic.x         = margin_x + left_w + 8;
    m_servers.players.generic.y         = header_y;
    m_servers.players.generic.height    = content_h;

    int pcol_frg = 4 * CONCHAR_WIDTH + MLIST_PADDING;
    int pcol_rtt = 4 * CONCHAR_WIDTH + MLIST_PADDING;
    int pcol_name = right_w - MLIST_SCROLLBAR_WIDTH - (pcol_frg + pcol_rtt);
    if (pcol_name < 60)
        pcol_name = 60;

    m_servers.players.columns[0].width  = pcol_frg;
    m_servers.players.columns[1].width  = pcol_rtt;
    m_servers.players.columns[2].width  = pcol_name;

    m_servers.players.mlFlags           |= MLF_SCROLLBAR;
}

static void Size(menuFrameWork_t *self)
{
    if (uis.width >= 500)
        SizeFull();
    else
        SizeCompact();
    UpdateSelection();
}

static menuSound_t Keydown(menuFrameWork_t *self, int key)
{
    // ignore autorepeats
    if (Key_IsDown(key) > 1)
        return QMS_NOTHANDLED;

    switch (key) {
    case 'r':
        if (Key_IsDown(K_CTRL))
            return SetRconAddress();
        return QMS_NOTHANDLED;

    case 'c':
        if (Key_IsDown(K_CTRL))
            return CopyAddress();
        return QMS_NOTHANDLED;

    case K_SPACE:
        if (Key_IsDown(K_ALT) || !m_servers.list.numItems) {
            PingServers();
            return QMS_SILENT;
        }
        return PingSelected();

    case K_BACKSPACE:
        if (m_servers.pingstage) {
            FinishPingStage();
            return QMS_OUT;
        }
        return QMS_SILENT;

    default:
        return QMS_NOTHANDLED;
    }
}

static void DrawStatus(void)
{
    int h = 14;
    int y = uis.height - h;

    // Dark bar with top amber border
    R_DrawFill32(0, y, uis.width, h, MakeColor(14, 11, 9, 230));
    R_DrawFill32(0, y, uis.width, 1, MakeColor(180, 140, 50, 160));

    // Progress indicator when pinging
    if (m_servers.pingstage == PING_STAGES && m_servers.num_query > 0) {
        int pw = m_servers.pingindex * uis.width / m_servers.num_query;
        R_DrawFill32(0, y + 1, pw, 2, MakeColor(240, 190, 40, 230));
    }

    int text_y = y + 3;
    if (m_servers.list.numItems && m_servers.list.curvalue >= 0) {
        serverslot_t *slot = m_servers.list.items[m_servers.list.curvalue];
        if (slot->status > SLOT_PENDING) {
            UI_DrawString(8, text_y, UI_LEFT | UI_ALTCOLOR, slot->hostname);
        }
    }

    if (m_servers.status_c)
        UI_DrawString(uis.width / 2, text_y, UI_CENTER, m_servers.status_c);

    if (uis.width >= 500 && m_servers.list.numItems)
        UI_DrawString(uis.width - 8, text_y, UI_RIGHT | UI_ALTCOLOR, m_servers.status_r);
}

static void Draw(menuFrameWork_t *self)
{
    Menu_Draw(self);

    // Title banner bar
    R_DrawFill32(0, 0, uis.width, 16, MakeColor(14, 11, 9, 220));
    R_DrawFill32(0, 15, uis.width, 1, MakeColor(180, 140, 50, 140));

    UI_DrawString(8, 4, UI_LEFT | UI_ALTCOLOR, "SERVER BROWSER // ACTIVE SERVERS");

    if (m_servers.pingstage) {
        UI_DrawString(uis.width - 8, 4, UI_RIGHT, "QUERYING / PINGING...");
    } else {
        UI_DrawString(uis.width - 8, 4, UI_RIGHT | UI_ALTCOLOR, "OPENKP");
    }

    DrawStatus();
}

static bool Push(menuFrameWork_t *self)
{
    // save our arguments for refreshing
    m_servers.args = UI_CopyString(COM_StripQuotes(Cmd_RawArgsFrom(2)));
    return true;
}

static void Pop(menuFrameWork_t *self)
{
    ClearServers();
    Z_Freep(&m_servers.args);
}

static void Expose(menuFrameWork_t *self)
{
    PingServers();
    ui_sortservers_changed(ui_sortservers);
}

static void Free(menuFrameWork_t *self)
{
    ClearServers();
    Z_Freep(&m_servers.args);
    Z_Free(m_servers.menu.items);
    memset(&m_servers, 0, sizeof(m_servers));
}

static void ui_colorservers_changed(cvar_t *self)
{
    if (self->integer)
        m_servers.list.mlFlags |= MLF_COLOR;
    else
        m_servers.list.mlFlags &= ~MLF_COLOR;
}

void M_Menu_Servers(void)
{
    ui_sortservers = Cvar_Get("ui_sortservers", "0", 0);
    ui_sortservers->changed = ui_sortservers_changed;
    ui_colorservers = Cvar_Get("ui_colorservers", "0", 0);
    ui_colorservers->changed = ui_colorservers_changed;
    ui_pingrate = Cvar_Get("ui_pingrate", "0", 0);

    m_servers.menu.name     = "servers";
    m_servers.menu.title    = NULL;

    m_servers.menu.draw         = Draw;
    m_servers.menu.expose       = Expose;
    m_servers.menu.push         = Push;
    m_servers.menu.pop          = Pop;
    m_servers.menu.size         = Size;
    m_servers.menu.keydown      = Keydown;
    m_servers.menu.free         = Free;
    m_servers.menu.image        = uis.backgroundHandle;
    m_servers.menu.color.u32    = uis.color.background.u32;
    m_servers.menu.transparent  = uis.transparent;

//
// server list
//
    m_servers.list.generic.type         = MTYPE_LIST;
    m_servers.list.generic.flags        = QMF_LEFT_JUSTIFY | QMF_HASFOCUS;
    m_servers.list.generic.activate     = Connect;
    m_servers.list.generic.change       = Change;
    m_servers.list.items                = m_servers.names;
    m_servers.list.numcolumns           = COL_MAX;
    m_servers.list.sortdir              = 1;
    m_servers.list.sortcol              = -1;
    m_servers.list.sort                 = Sort;
    m_servers.list.extrasize            = SLOT_EXTRASIZE;
    m_servers.list.mlFlags              = MLF_HEADER | MLF_SCROLLBAR;

    m_servers.list.columns[0].uiFlags   = UI_LEFT;
    m_servers.list.columns[0].name      = "Hostname";
    m_servers.list.columns[1].uiFlags   = UI_CENTER;
    m_servers.list.columns[1].name      = "Mod";
    m_servers.list.columns[2].uiFlags   = UI_CENTER;
    m_servers.list.columns[2].name      = "Map";
    m_servers.list.columns[3].uiFlags   = UI_CENTER;
    m_servers.list.columns[3].name      = "Players";
    m_servers.list.columns[4].uiFlags   = UI_RIGHT;
    m_servers.list.columns[4].name      = "RTT";

    ui_colorservers_changed(ui_colorservers);

//
// server info
//
    m_servers.info.generic.type         = MTYPE_LIST;
    m_servers.info.generic.flags        = QMF_LEFT_JUSTIFY | QMF_HIDDEN;
    m_servers.info.generic.activate     = Connect;
    m_servers.info.numcolumns           = 2;
    m_servers.info.mlFlags              = MLF_HEADER | MLF_SCROLLBAR;

    m_servers.info.columns[0].uiFlags   = UI_LEFT;
    m_servers.info.columns[0].name      = "Key";
    m_servers.info.columns[1].uiFlags   = UI_LEFT;
    m_servers.info.columns[1].name      = "Value";

//
// player list
//
    m_servers.players.generic.type      = MTYPE_LIST;
    m_servers.players.generic.flags     = QMF_LEFT_JUSTIFY | QMF_HIDDEN;
    m_servers.players.generic.activate  = Connect;
    m_servers.players.numcolumns        = 3;
    m_servers.players.mlFlags           = MLF_HEADER;

    m_servers.players.columns[0].uiFlags    = UI_RIGHT;
    m_servers.players.columns[0].name       = "Frg";
    m_servers.players.columns[1].uiFlags    = UI_RIGHT;
    m_servers.players.columns[1].name       = "RTT";
    m_servers.players.columns[2].uiFlags    = UI_LEFT;
    m_servers.players.columns[2].name       = "Name";

    Menu_AddItem(&m_servers.menu, &m_servers.list);
    Menu_AddItem(&m_servers.menu, &m_servers.info);
    Menu_AddItem(&m_servers.menu, &m_servers.players);

    List_Append(&ui_menus, &m_servers.menu.entry);
}
