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

#include <time.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#endif

#define DEFAULT_DISCORD_APPID "1442734196761034884"

enum discord_opcode {
    DISCORD_OP_HANDSHAKE = 0,
    DISCORD_OP_FRAME     = 1,
    DISCORD_OP_CLOSE     = 2,
    DISCORD_OP_PING      = 3,
    DISCORD_OP_PONG      = 4,
};

enum discord_state {
    DISCORD_STATE_DISCONNECTED,
    DISCORD_STATE_HANDSHAKE_SENT,
    DISCORD_STATE_READY,
};

#pragma pack(push, 1)
typedef struct {
    uint32_t opcode;
    uint32_t length;
} discord_hdr_t;
#pragma pack(pop)

static cvar_t *cl_discord;
static cvar_t *cl_discord_appid;

static enum discord_state discord_status = DISCORD_STATE_DISCONNECTED;
static unsigned int discord_last_attempt = 0;
static unsigned int discord_last_update = 0;
static time_t discord_start_time = 0;
static int discord_nonce = 1;

#if defined(_WIN32)
static HANDLE discord_pipe = INVALID_HANDLE_VALUE;
#else
static int discord_sock = -1;
#endif

static int last_game_state = -1;
static char last_server_name[MAX_QPATH] = {0};
static char last_map_name[MAX_QPATH] = {0};
static int last_player_count = -1;

/*
====================
Discord_EscapeJSON
====================
*/
static void Discord_EscapeJSON(const char *in, char *out, size_t out_size)
{
    size_t j = 0;
    if (!in || !out || out_size == 0)
        return;

    for (size_t i = 0; in[i] && j + 2 < out_size; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c >= 128)
            c &= 127; // strip high bits / Kingpin color bits

        if (c == '"' || c == '\\') {
            out[j++] = '\\';
            out[j++] = (char)c;
        } else if (c == '\n' || c == '\r' || c == '\t') {
            out[j++] = ' ';
        } else if (c >= 32 && c <= 126) {
            out[j++] = (char)c;
        }
    }
    out[j] = '\0';
}

/*
====================
Discord_CloseSocket
====================
*/
static void Discord_CloseSocket(void)
{
#if defined(_WIN32)
    if (discord_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(discord_pipe);
        discord_pipe = INVALID_HANDLE_VALUE;
    }
#else
    if (discord_sock >= 0) {
        close(discord_sock);
        discord_sock = -1;
    }
#endif
    discord_status = DISCORD_STATE_DISCONNECTED;
}

/*
====================
Discord_SendPacket
====================
*/
static bool Discord_SendPacket(uint32_t opcode, const char *payload)
{
    uint32_t len = payload ? (uint32_t)strlen(payload) : 0;
    discord_hdr_t hdr;
    hdr.opcode = opcode;
    hdr.length = len;

#if defined(_WIN32)
    if (discord_pipe == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    if (!WriteFile(discord_pipe, &hdr, sizeof(hdr), &written, NULL) || written != sizeof(hdr)) {
        Discord_CloseSocket();
        return false;
    }
    if (len > 0) {
        if (!WriteFile(discord_pipe, payload, len, &written, NULL) || written != len) {
            Discord_CloseSocket();
            return false;
        }
    }
    return true;
#else
    if (discord_sock < 0)
        return false;

    ssize_t ret = write(discord_sock, &hdr, sizeof(hdr));
    if (ret != sizeof(hdr)) {
        Discord_CloseSocket();
        return false;
    }
    if (len > 0) {
        ret = write(discord_sock, payload, len);
        if (ret != (ssize_t)len) {
            Discord_CloseSocket();
            return false;
        }
    }
    return true;
#endif
}

/*
====================
Discord_TryConnect
====================
*/
static bool Discord_TryConnect(void)
{
    Discord_CloseSocket();

#if defined(_WIN32)
    for (int i = 0; i < 10; i++) {
        char pipe_name[64];
        Q_snprintf(pipe_name, sizeof(pipe_name), "\\\\.\\pipe\\discord-ipc-%d", i);
        HANDLE h = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            discord_pipe = h;
            break;
        }
    }
    if (discord_pipe == INVALID_HANDLE_VALUE)
        return false;
#else
    const char *xdg_dir = getenv("XDG_RUNTIME_DIR");
    const char *tmp_dir = getenv("TMPDIR");
    char uid_str[32];
    Q_snprintf(uid_str, sizeof(uid_str), "%d", (int)getuid());

    char run_user_dir[MAX_OSPATH];
    Q_snprintf(run_user_dir, sizeof(run_user_dir), "/run/user/%s", uid_str);

    // Candidates directories where Discord IPC sockets can live (including Flatpak and Snap)
    const char *search_dirs[16];
    int dir_count = 0;

    if (xdg_dir && xdg_dir[0]) {
        static char xdg_snap[MAX_OSPATH];
        static char xdg_flatpak[MAX_OSPATH];
        Q_snprintf(xdg_snap, sizeof(xdg_snap), "%s/snap.discord", xdg_dir);
        Q_snprintf(xdg_flatpak, sizeof(xdg_flatpak), "%s/app/com.discordapp.Discord", xdg_dir);
        search_dirs[dir_count++] = xdg_dir;
        search_dirs[dir_count++] = xdg_snap;
        search_dirs[dir_count++] = xdg_flatpak;
    }

    static char run_snap[MAX_OSPATH];
    static char run_flatpak[MAX_OSPATH];
    Q_snprintf(run_snap, sizeof(run_snap), "/run/user/%s/snap.discord", uid_str);
    Q_snprintf(run_flatpak, sizeof(run_flatpak), "/run/user/%s/app/com.discordapp.Discord", uid_str);
    search_dirs[dir_count++] = run_user_dir;
    search_dirs[dir_count++] = run_snap;
    search_dirs[dir_count++] = run_flatpak;

    if (tmp_dir && tmp_dir[0])
        search_dirs[dir_count++] = tmp_dir;

    search_dirs[dir_count++] = "/tmp";
    search_dirs[dir_count++] = "/tmp/snap.discord";

    for (int d = 0; d < dir_count; d++) {
        for (int i = 0; i < 10; i++) {
            char sock_path[MAX_OSPATH];
            Q_snprintf(sock_path, sizeof(sock_path), "%s/discord-ipc-%d", search_dirs[d], i);

            int fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (fd < 0)
                continue;

            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            Q_strlcpy(addr.sun_path, sock_path, sizeof(addr.sun_path));

            if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
                // Set non-blocking
                int flags = fcntl(fd, F_GETFL, 0);
                if (flags != -1)
                    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

                discord_sock = fd;
                break;
            }
            close(fd);
        }
        if (discord_sock >= 0)
            break;
    }

    if (discord_sock < 0)
        return false;
#endif

    // Send handshake
    char handshake[256];
    const char *appid = (cl_discord_appid && cl_discord_appid->string[0]) ?
                         cl_discord_appid->string : DEFAULT_DISCORD_APPID;
    Q_snprintf(handshake, sizeof(handshake), "{\"v\":1,\"client_id\":\"%s\"}", appid);

    if (Discord_SendPacket(DISCORD_OP_HANDSHAKE, handshake)) {
        discord_status = DISCORD_STATE_HANDSHAKE_SENT;
        return true;
    }

    Discord_CloseSocket();
    return false;
}

/*
====================
Discord_ReadPackets
====================
*/
static void Discord_ReadPackets(void)
{
    if (discord_status == DISCORD_STATE_DISCONNECTED)
        return;

#if defined(_WIN32)
    if (discord_pipe == INVALID_HANDLE_VALUE)
        return;

    DWORD avail = 0;
    if (!PeekNamedPipe(discord_pipe, NULL, 0, NULL, &avail, NULL)) {
        Discord_CloseSocket();
        return;
    }
    if (avail < sizeof(discord_hdr_t))
        return;

    discord_hdr_t hdr;
    DWORD bytes_read = 0;
    if (!ReadFile(discord_pipe, &hdr, sizeof(hdr), &bytes_read, NULL) || bytes_read != sizeof(hdr)) {
        Discord_CloseSocket();
        return;
    }

    char buf[4096];
    uint32_t to_read = (hdr.length < sizeof(buf) - 1) ? hdr.length : sizeof(buf) - 1;
    if (to_read > 0) {
        if (!ReadFile(discord_pipe, buf, to_read, &bytes_read, NULL)) {
            Discord_CloseSocket();
            return;
        }
        buf[bytes_read] = '\0';
    } else {
        buf[0] = '\0';
    }
#else
    if (discord_sock < 0)
        return;

    discord_hdr_t hdr;
    ssize_t ret = recv(discord_sock, &hdr, sizeof(hdr), MSG_DONTWAIT);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        Discord_CloseSocket();
        return;
    }
    if (ret == 0) {
        Discord_CloseSocket();
        return;
    }
    if (ret != sizeof(hdr)) {
        Discord_CloseSocket();
        return;
    }

    char buf[4096];
    uint32_t to_read = (hdr.length < sizeof(buf) - 1) ? hdr.length : sizeof(buf) - 1;
    uint32_t total = 0;
    while (total < to_read) {
        ssize_t n = recv(discord_sock, buf + total, to_read - total, MSG_DONTWAIT);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;
            Discord_CloseSocket();
            return;
        }
        total += (uint32_t)n;
    }
    buf[total] = '\0';
#endif

    if (hdr.opcode == DISCORD_OP_CLOSE) {
        Com_DPrintf("[Discord] Received CLOSE from Discord client.\n");
        Discord_CloseSocket();
        return;
    }

    if (hdr.opcode == DISCORD_OP_FRAME) {
        if (discord_status == DISCORD_STATE_HANDSHAKE_SENT) {
            if (strstr(buf, "\"evt\":\"READY\"") || strstr(buf, "\"READY\"")) {
                discord_status = DISCORD_STATE_READY;
                Com_Printf("[Discord] Connected to local Discord IPC client.\n");
                // Reset cache to force immediate activity update
                last_game_state = -1;
                last_server_name[0] = '\0';
                last_map_name[0] = '\0';
                last_player_count = -1;
            }
        }
    }
}

/*
====================
CL_Discord_UpdateActivity
====================
*/
static void CL_Discord_UpdateActivity(void)
{
    if (discord_status != DISCORD_STATE_READY)
        return;

    char payload[2048];
    char escaped_state[256];
    char escaped_details[256];

    if (cls.state == ca_active) {
        char map[MAX_QPATH];
        const char *raw_map = cl.configstrings[CS_MODELS + 1];
        COM_StripExtension(map, COM_SkipPath(raw_map), sizeof(map));
        if (!map[0])
            Q_strlcpy(map, "unknown", sizeof(map));

        const char *servername = cl.configstrings[CS_NAME][0] ? cl.configstrings[CS_NAME] : cls.servername;
        if (!servername[0])
            servername = "Kingpin Server";

        int num_players = 0;
        int max_players = cl.maxclients > 0 ? cl.maxclients : 1;
        for (int i = 0; i < cl.maxclients && i < MAX_CLIENTS; i++) {
            if (cl.clientinfo[i].name[0])
                num_players++;
        }
        if (num_players == 0)
            num_players = 1;

        Discord_EscapeJSON(servername, escaped_state, sizeof(escaped_state));
        char details_buf[256];
        Q_snprintf(details_buf, sizeof(details_buf), "Map: %s", map);
        Discord_EscapeJSON(details_buf, escaped_details, sizeof(escaped_details));

        Q_snprintf(payload, sizeof(payload),
            "{\"cmd\":\"SET_ACTIVITY\",\"args\":{"
            "\"pid\":%d,"
            "\"activity\":{"
                "\"state\":\"%s\","
                "\"details\":\"%s\","
                "\"timestamps\":{\"start\":%lld},"
                "\"assets\":{"
                    "\"large_image\":\"cover_image_hash\","
                    "\"large_text\":\"Kingpin: Life of Crime\","
                    "\"small_image\":\"openkp\","
                    "\"small_text\":\"OpenKP\""
                "},"
                "\"party\":{"
                    "\"id\":\"openkp_party\","
                    "\"size\":[%d,%d]"
                "}"
            "}"
            "},\"nonce\":\"%d\"}",
#if defined(_WIN32)
            (int)GetCurrentProcessId(),
#else
            (int)getpid(),
#endif
            escaped_state,
            escaped_details,
            (long long)discord_start_time,
            num_players,
            max_players,
            discord_nonce++
        );
    } else {
        Discord_EscapeJSON("In Main Menu", escaped_state, sizeof(escaped_state));
        Discord_EscapeJSON("OpenKP Client", escaped_details, sizeof(escaped_details));

        Q_snprintf(payload, sizeof(payload),
            "{\"cmd\":\"SET_ACTIVITY\",\"args\":{"
            "\"pid\":%d,"
            "\"activity\":{"
                "\"state\":\"%s\","
                "\"details\":\"%s\","
                "\"timestamps\":{\"start\":%lld},"
                "\"assets\":{"
                    "\"large_image\":\"cover_image_hash\","
                    "\"large_text\":\"Kingpin: Life of Crime\""
                "}"
            "}"
            "},\"nonce\":\"%d\"}",
#if defined(_WIN32)
            (int)GetCurrentProcessId(),
#else
            (int)getpid(),
#endif
            escaped_state,
            escaped_details,
            (long long)discord_start_time,
            discord_nonce++
        );
    }

    Discord_SendPacket(DISCORD_OP_FRAME, payload);
    discord_last_update = cls.realtime;
}

/*
====================
CL_Discord_Init
====================
*/
void CL_Discord_Init(void)
{
    cl_discord = Cvar_Get("cl_discord", "1", CVAR_ARCHIVE);
    cl_discord_appid = Cvar_Get("cl_discord_appid", DEFAULT_DISCORD_APPID, CVAR_ARCHIVE);

    discord_status = DISCORD_STATE_DISCONNECTED;
    discord_last_attempt = 0;
    discord_last_update = 0;
    discord_start_time = time(NULL);
    discord_nonce = 1;

    last_game_state = -1;
    last_server_name[0] = '\0';
    last_map_name[0] = '\0';
    last_player_count = -1;

    if (!cl_discord->integer)
        return;

    Discord_TryConnect();
}

/*
====================
CL_Discord_Frame
====================
*/
void CL_Discord_Frame(void)
{
    if (!cl_discord || !cl_discord->integer) {
        if (discord_status != DISCORD_STATE_DISCONNECTED)
            Discord_CloseSocket();
        return;
    }

    // Try reconnecting periodically if disconnected
    if (discord_status == DISCORD_STATE_DISCONNECTED) {
        if (cls.realtime - discord_last_attempt >= 5000) {
            discord_last_attempt = cls.realtime;
            Discord_TryConnect();
        }
        return;
    }

    // Process incoming data from Discord
    Discord_ReadPackets();

    if (discord_status != DISCORD_STATE_READY)
        return;

    // Detect game state changes
    char map[MAX_QPATH] = {0};
    int current_players = 0;

    if (cls.state == ca_active) {
        const char *raw_map = cl.configstrings[CS_MODELS + 1];
        COM_StripExtension(map, COM_SkipPath(raw_map), sizeof(map));
        for (int i = 0; i < cl.maxclients && i < MAX_CLIENTS; i++) {
            if (cl.clientinfo[i].name[0])
                current_players++;
        }
    }

    bool state_changed = false;
    if (cls.state != last_game_state)
        state_changed = true;
    else if (cls.state == ca_active) {
        if (strcmp(cls.servername, last_server_name) != 0 ||
            strcmp(map, last_map_name) != 0 ||
            current_players != last_player_count)
            state_changed = true;
    }

    // Periodic heartbeat (every 15 seconds) or on state change
    if (state_changed || (cls.realtime - discord_last_update >= 15000)) {
        if (cls.state != last_game_state)
            discord_start_time = time(NULL);

        last_game_state = cls.state;
        Q_strlcpy(last_server_name, cls.servername, sizeof(last_server_name));
        Q_strlcpy(last_map_name, map, sizeof(last_map_name));
        last_player_count = current_players;

        CL_Discord_UpdateActivity();
    }
}

/*
====================
CL_Discord_Shutdown
====================
*/
void CL_Discord_Shutdown(void)
{
    if (discord_status == DISCORD_STATE_READY) {
        // Clear activity before closing
        char payload[512];
        Q_snprintf(payload, sizeof(payload),
            "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%d,\"activity\":null},\"nonce\":\"%d\"}",
#if defined(_WIN32)
            (int)GetCurrentProcessId(),
#else
            (int)getpid(),
#endif
            discord_nonce++
        );
        Discord_SendPacket(DISCORD_OP_FRAME, payload);
    }

    Discord_CloseSocket();
}
