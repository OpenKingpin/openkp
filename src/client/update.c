/*
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

#include "client.h"
#include "common/cmd.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/files.h"
#include "system/pthread.h"
#include "system/system.h"
#include "shared/jsmn.h"

#include <curl/curl.h>

#define DEFAULT_NIGHTLY_URL  "https://github.com/OpenKingpin/openkp/releases/download/nightly/version.json"
#define DEFAULT_LATEST_URL   "https://github.com/OpenKingpin/openkp/releases/latest/download/version.json"

typedef enum {
    UPDATE_STATE_IDLE = 0,
    UPDATE_STATE_CHECKING,
    UPDATE_STATE_AVAILABLE,
    UPDATE_STATE_UP_TO_DATE,
    UPDATE_STATE_DOWNLOADING,
    UPDATE_STATE_READY,
    UPDATE_STATE_APPLYING,
    UPDATE_STATE_ERROR
} update_state_t;

typedef enum {
    TASK_NONE = 0,
    TASK_CHECK,
    TASK_DOWNLOAD
} update_task_t;

typedef struct {
    pthread_mutex_t lock;
    pthread_t       thread;
    bool            thread_running;
    bool            thread_joined;
    bool            cancel_requested;

    update_task_t   current_task;
    update_state_t  state;
    update_state_t  last_reported_state;

    int             local_revision;
    int             remote_revision;
    char            local_version[64];
    char            remote_version[64];
    char            binary_url[512];
    char            error_message[256];

    char            temp_file_path[MAX_OSPATH];

    int64_t         dl_bytes_now;
    int64_t         dl_bytes_total;
    int             dl_percent;
    int             last_reported_percent;

    bool            check_verbose;
    bool            restart_when_ready;
    bool            force_download;
} update_ctx_t;

static update_ctx_t ctx;

static cvar_t *cl_autoupdate;
static cvar_t *cl_autoupdate_channel;
static cvar_t *cl_autoupdate_url;

// Memory buffer for curl HTTP GET
typedef struct {
    char   *data;
    size_t size;
    size_t capacity;
} mem_buf_t;

static size_t curl_mem_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    mem_buf_t *buf = (mem_buf_t *)userdata;
    size_t realsize = size * nmemb;

    if (buf->size + realsize + 1 > buf->capacity) {
        size_t newcap = (buf->capacity + realsize + 1) * 2;
        char *newdata = realloc(buf->data, newcap);
        if (!newdata) {
            return 0;
        }
        buf->data = newdata;
        buf->capacity = newcap;
    }

    memcpy(buf->data + buf->size, ptr, realsize);
    buf->size += realsize;
    buf->data[buf->size] = '\0';
    return realsize;
}

static size_t curl_file_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    FILE *fp = (FILE *)userdata;
    return fwrite(ptr, size, nmemb, fp);
}

static int curl_progress_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                            curl_off_t ultotal, curl_off_t ulnow)
{
    (void)ultotal;
    (void)ulnow;
    update_ctx_t *c = (update_ctx_t *)clientp;

    pthread_mutex_lock(&c->lock);
    if (c->cancel_requested) {
        pthread_mutex_unlock(&c->lock);
        return 1; // abort curl
    }
    c->dl_bytes_now = dlnow;
    c->dl_bytes_total = dltotal;
    c->dl_percent = dltotal > 0 ? (int)((dlnow * 100) / dltotal) : 0;
    pthread_mutex_unlock(&c->lock);

    return 0;
}

static bool jsoneq(const char *json, const jsmntok_t *tok, const char *s)
{
    if (tok->type == JSMN_STRING && (int)strlen(s) == (tok->end - tok->start) &&
        strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
        return true;
    }
    return false;
}

static void parse_json_manifest(const char *json, size_t len)
{
    jsmn_parser p;
    jsmntok_t tokens[256];

    jsmn_init(&p);
    int num_toks = jsmn_parse(&p, json, len, tokens, (unsigned int)q_countof(tokens));
    if (num_toks <= 0) {
        Q_strlcpy(ctx.error_message, "Failed to parse version manifest JSON", sizeof(ctx.error_message));
        ctx.state = UPDATE_STATE_ERROR;
        return;
    }

    if (tokens[0].type != JSMN_OBJECT) {
        Q_strlcpy(ctx.error_message, "Version manifest root is not a JSON object", sizeof(ctx.error_message));
        ctx.state = UPDATE_STATE_ERROR;
        return;
    }

#if defined(_WIN32)
    const char *target_platform = "windows_x64";
#elif defined(__linux__)
    const char *target_platform = "linux_x86_64";
#else
    const char *target_platform = "";
#endif

    int remote_rev = 0;
    char remote_ver[64] = {0};
    char bin_url[512] = {0};

    for (int i = 1; i < num_toks; i++) {
        if (jsoneq(json, &tokens[i], "revision") && i + 1 < num_toks) {
            char val[32] = {0};
            int vlen = tokens[i + 1].end - tokens[i + 1].start;
            if (vlen > 0 && vlen < (int)sizeof(val)) {
                memcpy(val, json + tokens[i + 1].start, vlen);
                remote_rev = atoi(val);
            }
            i++;
        } else if (jsoneq(json, &tokens[i], "version") && i + 1 < num_toks) {
            int vlen = tokens[i + 1].end - tokens[i + 1].start;
            if (vlen > 0 && vlen < (int)sizeof(remote_ver)) {
                memcpy(remote_ver, json + tokens[i + 1].start, vlen);
            }
            i++;
        } else if (*target_platform && jsoneq(json, &tokens[i], target_platform) && i + 1 < num_toks) {
            if (tokens[i + 1].type == JSMN_OBJECT) {
                int obj_children = tokens[i + 1].size;
                int cur = i + 2;
                for (int c = 0; c < obj_children && cur < num_toks; c++) {
                    if (jsoneq(json, &tokens[cur], "binary_url") && cur + 1 < num_toks) {
                        int ulen = tokens[cur + 1].end - tokens[cur + 1].start;
                        if (ulen > 0 && ulen < (int)sizeof(bin_url)) {
                            memcpy(bin_url, json + tokens[cur + 1].start, ulen);
                        }
                        cur += 2;
                    } else {
                        // Skip key and its value
                        cur += 2;
                    }
                }
                i = cur - 1;
            }
        }
    }

    ctx.remote_revision = remote_rev;
    Q_strlcpy(ctx.remote_version, remote_ver, sizeof(ctx.remote_version));
    Q_strlcpy(ctx.binary_url, bin_url, sizeof(ctx.binary_url));

    if (!*ctx.binary_url) {
        Q_strlcpy(ctx.error_message, "No binary URL found for current platform in manifest", sizeof(ctx.error_message));
        ctx.state = UPDATE_STATE_ERROR;
        return;
    }

    if (ctx.force_download || remote_rev > ctx.local_revision) {
        ctx.state = UPDATE_STATE_AVAILABLE;
    } else {
        ctx.state = UPDATE_STATE_UP_TO_DATE;
    }
}

static void *update_worker_func(void *arg)
{
    (void)arg;
    CURL *curl = curl_easy_init();
    if (!curl) {
        pthread_mutex_lock(&ctx.lock);
        Q_strlcpy(ctx.error_message, "curl_easy_init failed", sizeof(ctx.error_message));
        ctx.state = UPDATE_STATE_ERROR;
        ctx.thread_running = false;
        pthread_mutex_unlock(&ctx.lock);
        return NULL;
    }

    // Common curl options
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, com_version->string);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

    cvar_t *proxy = Cvar_FindVar("cl_http_proxy");
    if (proxy && *proxy->string) {
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy->string);
    }
    cvar_t *insecure = Cvar_FindVar("cl_http_insecure");
    if (insecure && insecure->integer) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    pthread_mutex_lock(&ctx.lock);
    update_task_t task = ctx.current_task;
    pthread_mutex_unlock(&ctx.lock);

    if (task == TASK_CHECK) {
        char url[512];
        if (cl_autoupdate_url && *cl_autoupdate_url->string) {
            Q_strlcpy(url, cl_autoupdate_url->string, sizeof(url));
        } else if (cl_autoupdate_channel && !Q_stricmp(cl_autoupdate_channel->string, "latest")) {
            Q_strlcpy(url, DEFAULT_LATEST_URL, sizeof(url));
        } else {
            Q_strlcpy(url, DEFAULT_NIGHTLY_URL, sizeof(url));
        }

        mem_buf_t buf;
        buf.capacity = 4096;
        buf.size = 0;
        buf.data = malloc(buf.capacity);

        if (!buf.data) {
            pthread_mutex_lock(&ctx.lock);
            Q_strlcpy(ctx.error_message, "Out of memory", sizeof(ctx.error_message));
            ctx.state = UPDATE_STATE_ERROR;
            ctx.thread_running = false;
            pthread_mutex_unlock(&ctx.lock);
            curl_easy_cleanup(curl);
            return NULL;
        }

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_mem_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

        CURLcode res = curl_easy_perform(curl);

        pthread_mutex_lock(&ctx.lock);
        if (res == CURLE_OK && buf.size > 0) {
            parse_json_manifest(buf.data, buf.size);
        } else {
            Q_snprintf(ctx.error_message, sizeof(ctx.error_message),
                       "Failed to fetch version manifest (%s)", curl_easy_strerror(res));
            ctx.state = UPDATE_STATE_ERROR;
        }
        ctx.thread_running = false;
        pthread_mutex_unlock(&ctx.lock);

        free(buf.data);
    } else if (task == TASK_DOWNLOAD) {
        char download_url[512];
        char temp_path[MAX_OSPATH];

        pthread_mutex_lock(&ctx.lock);
        Q_strlcpy(download_url, ctx.binary_url, sizeof(download_url));
        Q_strlcpy(temp_path, ctx.temp_file_path, sizeof(temp_path));
        ctx.dl_bytes_now = 0;
        ctx.dl_bytes_total = 0;
        ctx.dl_percent = 0;
        ctx.cancel_requested = false;
        pthread_mutex_unlock(&ctx.lock);

        FILE *fp = fopen(temp_path, "wb");
        if (!fp) {
            pthread_mutex_lock(&ctx.lock);
            Q_snprintf(ctx.error_message, sizeof(ctx.error_message),
                       "Cannot open %s for writing. Check write permissions.", temp_path);
            ctx.state = UPDATE_STATE_ERROR;
            ctx.thread_running = false;
            pthread_mutex_unlock(&ctx.lock);
            curl_easy_cleanup(curl);
            return NULL;
        }

        curl_easy_setopt(curl, CURLOPT_URL, download_url);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_file_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_progress_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);

        CURLcode res = curl_easy_perform(curl);
        fclose(fp);

        pthread_mutex_lock(&ctx.lock);
        if (res == CURLE_OK && !ctx.cancel_requested) {
            ctx.state = UPDATE_STATE_READY;
        } else {
            unlink(temp_path);
            if (ctx.cancel_requested) {
                Q_strlcpy(ctx.error_message, "Download cancelled", sizeof(ctx.error_message));
                ctx.state = UPDATE_STATE_AVAILABLE;
            } else {
                Q_snprintf(ctx.error_message, sizeof(ctx.error_message),
                           "Download failed: %s", curl_easy_strerror(res));
                ctx.state = UPDATE_STATE_ERROR;
            }
        }
        ctx.thread_running = false;
        pthread_mutex_unlock(&ctx.lock);
    }

    curl_easy_cleanup(curl);
    return NULL;
}

static void start_worker_thread(update_task_t task)
{
    if (ctx.thread_running) {
        return;
    }
    if (!ctx.thread_joined) {
        pthread_join(ctx.thread, NULL);
        ctx.thread_joined = true;
    }

    ctx.current_task = task;
    ctx.thread_running = true;
    ctx.thread_joined = false;
    ctx.cancel_requested = false;

    if (pthread_create(&ctx.thread, NULL, update_worker_func, NULL) != 0) {
        ctx.thread_running = false;
        ctx.thread_joined = true;
        Q_strlcpy(ctx.error_message, "Failed to create update thread", sizeof(ctx.error_message));
        ctx.state = UPDATE_STATE_ERROR;
    }
}

void CL_Update_Check(bool verbose)
{
    pthread_mutex_lock(&ctx.lock);
    if (ctx.state == UPDATE_STATE_DOWNLOADING) {
        Com_Printf("^3[UPDATE]^7 Cannot check: download already in progress (%d%%).\n", ctx.dl_percent);
        pthread_mutex_unlock(&ctx.lock);
        return;
    }

    ctx.check_verbose = verbose;
    ctx.state = UPDATE_STATE_CHECKING;
    ctx.last_reported_state = UPDATE_STATE_CHECKING;
    if (verbose) {
        Com_Printf("^2[UPDATE]^7 Checking for OpenKP updates (%s channel)...\n",
                   cl_autoupdate_channel ? cl_autoupdate_channel->string : "nightly");
    }
    start_worker_thread(TASK_CHECK);
    pthread_mutex_unlock(&ctx.lock);
}

void CL_Update_Start(bool force)
{
    pthread_mutex_lock(&ctx.lock);

    if (ctx.state == UPDATE_STATE_READY) {
        Com_Printf("^2[UPDATE]^7 Applying downloaded update and restarting OpenKP...\n");
        ctx.state = UPDATE_STATE_APPLYING;
        pthread_mutex_unlock(&ctx.lock);
        Sys_ApplyUpdateAndRestart(ctx.temp_file_path);
        return;
    }

    if (ctx.state == UPDATE_STATE_DOWNLOADING) {
        Com_Printf("^3[UPDATE]^7 Update download is already in progress: %d%% (%lld / %lld bytes).\n",
                   ctx.dl_percent, (long long)ctx.dl_bytes_now, (long long)ctx.dl_bytes_total);
        ctx.restart_when_ready = true;
        pthread_mutex_unlock(&ctx.lock);
        return;
    }

    // Determine target temporary binary file path
    char exe_path[MAX_OSPATH];
    if (!Sys_GetBinaryPath(exe_path, sizeof(exe_path))) {
        Com_EPrintf("^1[UPDATE]^7 Failed to determine binary path for update.\n");
        pthread_mutex_unlock(&ctx.lock);
        return;
    }
    Q_snprintf(ctx.temp_file_path, sizeof(ctx.temp_file_path), "%s.new", exe_path);

    if (ctx.state == UPDATE_STATE_AVAILABLE || force) {
        ctx.force_download = force;
        ctx.restart_when_ready = true;
        ctx.state = UPDATE_STATE_DOWNLOADING;
        ctx.last_reported_state = UPDATE_STATE_DOWNLOADING;
        ctx.last_reported_percent = -1;
        Com_Printf("^2[UPDATE]^7 Starting download of build ^3%s^7...\n",
                   *ctx.remote_version ? ctx.remote_version : "latest");
        start_worker_thread(TASK_DOWNLOAD);
    } else {
        // Need to check first
        Com_Printf("^2[UPDATE]^7 Checking for updates before download...\n");
        ctx.force_download = force;
        ctx.restart_when_ready = true;
        ctx.check_verbose = true;
        ctx.state = UPDATE_STATE_CHECKING;
        ctx.last_reported_state = UPDATE_STATE_CHECKING;
        start_worker_thread(TASK_CHECK);
    }

    pthread_mutex_unlock(&ctx.lock);
}

void CL_Update_Cancel(void)
{
    pthread_mutex_lock(&ctx.lock);
    if (ctx.state == UPDATE_STATE_DOWNLOADING) {
        ctx.cancel_requested = true;
        Com_Printf("^3[UPDATE]^7 Cancelling download...\n");
    } else {
        Com_Printf("^3[UPDATE]^7 No active update download to cancel.\n");
    }
    pthread_mutex_unlock(&ctx.lock);
}

static void CL_UpdateCheck_f(void)
{
    CL_Update_Check(true);
}

static void CL_Update_f(void)
{
    bool force = false;
    if (Cmd_Argc() > 1 && !Q_stricmp(Cmd_Argv(1), "force")) {
        force = true;
    }
    CL_Update_Start(force);
}

static void CL_UpdateCancel_f(void)
{
    CL_Update_Cancel();
}

void CL_Update_Init(void)
{
    memset(&ctx, 0, sizeof(ctx));
    pthread_mutex_init(&ctx.lock, NULL);
    ctx.thread_joined = true;

    cl_autoupdate = Cvar_Get("cl_autoupdate", "1", CVAR_ARCHIVE);
    cl_autoupdate_channel = Cvar_Get("cl_autoupdate_channel", "nightly", CVAR_ARCHIVE);
    cl_autoupdate_url = Cvar_Get("cl_autoupdate_url", "", CVAR_ARCHIVE);

    Cmd_AddCommand("update_check", CL_UpdateCheck_f);
    Cmd_AddCommand("update", CL_Update_f);
    Cmd_AddCommand("update_cancel", CL_UpdateCancel_f);

    // Clean up any stale .old binary from a previous update
    Sys_CleanOldBinary();

    // Parse local revision from VERSION string (e.g. "r3855~4b6a56ac")
    const char *v = VERSION;
    if (v && v[0] == 'r') {
        ctx.local_revision = atoi(v + 1);
    }
    Q_strlcpy(ctx.local_version, VERSION, sizeof(ctx.local_version));

    // If autoupdate is enabled, start a quiet background check at startup
    if (cl_autoupdate->integer > 0) {
        CL_Update_Check(false);
    }
}

void CL_Update_Frame(void)
{
    pthread_mutex_lock(&ctx.lock);

    // If thread completed, join it cleanly
    if (!ctx.thread_running && !ctx.thread_joined) {
        pthread_join(ctx.thread, NULL);
        ctx.thread_joined = true;

        // If a check completed and restart/download was queued
        if (ctx.current_task == TASK_CHECK) {
            if (ctx.state == UPDATE_STATE_AVAILABLE) {
                if (ctx.restart_when_ready || cl_autoupdate->integer == 2) {
                    char exe_path[MAX_OSPATH];
                    if (Sys_GetBinaryPath(exe_path, sizeof(exe_path))) {
                        Q_snprintf(ctx.temp_file_path, sizeof(ctx.temp_file_path), "%s.new", exe_path);
                        ctx.state = UPDATE_STATE_DOWNLOADING;
                        ctx.last_reported_state = UPDATE_STATE_DOWNLOADING;
                        ctx.last_reported_percent = -1;
                        Com_Printf("^2[UPDATE]^7 Automatically downloading build ^3%s^7...\n", ctx.remote_version);
                        start_worker_thread(TASK_DOWNLOAD);
                    }
                }
            }
        }
    }

    update_state_t cur_state = ctx.state;

    // Handle state notification messages safely on main thread
    if (cur_state != ctx.last_reported_state) {
        ctx.last_reported_state = cur_state;

        switch (cur_state) {
        case UPDATE_STATE_AVAILABLE:
            Com_Printf("\n=======================================================\n");
            Com_Printf("^2[UPDATE]^7 A new OpenKP build is available: ^3%s^7 (current: ^3%s^7)\n",
                       ctx.remote_version, ctx.local_version);
            Com_Printf("^2[UPDATE]^7 Type '^3update^7' in the console to upgrade.\n");
            Com_Printf("=======================================================\n\n");
            break;

        case UPDATE_STATE_UP_TO_DATE:
            if (ctx.check_verbose) {
                Com_Printf("^2[UPDATE]^7 OpenKP is up to date (%s).\n", ctx.local_version);
                ctx.check_verbose = false;
            }
            break;

        case UPDATE_STATE_READY:
            Com_Printf("\n=======================================================\n");
            Com_Printf("^2[UPDATE]^7 OpenKP build ^3%s^7 downloaded successfully!\n", ctx.remote_version);
            if (ctx.restart_when_ready) {
                Com_Printf("^2[UPDATE]^7 Applying update and restarting OpenKP...\n");
                Com_Printf("=======================================================\n\n");
                ctx.state = UPDATE_STATE_APPLYING;
                pthread_mutex_unlock(&ctx.lock);
                Sys_ApplyUpdateAndRestart(ctx.temp_file_path);
                return;
            } else {
                Com_Printf("^2[UPDATE]^7 Type '^3update^7' in the console to apply and restart.\n");
                Com_Printf("=======================================================\n\n");
            }
            break;

        case UPDATE_STATE_ERROR:
            if (ctx.check_verbose) {
                Com_Printf("^1[UPDATE]^7 %s\n", ctx.error_message);
                ctx.check_verbose = false;
            }
            break;

        default:
            break;
        }
    }

    // Periodic progress report during download
    if (cur_state == UPDATE_STATE_DOWNLOADING) {
        int pct = ctx.dl_percent;
        if (pct != ctx.last_reported_percent && (pct % 25 == 0 || pct == 100)) {
            ctx.last_reported_percent = pct;
            Com_Printf("^2[UPDATE]^7 Downloading: %d%% (%lld / %lld bytes)\n",
                       pct, (long long)ctx.dl_bytes_now, (long long)ctx.dl_bytes_total);
        }
    }

    pthread_mutex_unlock(&ctx.lock);
}

void CL_Update_Shutdown(void)
{
    pthread_mutex_lock(&ctx.lock);
    ctx.cancel_requested = true;
    pthread_mutex_unlock(&ctx.lock);

    if (ctx.thread_running) {
        pthread_join(ctx.thread, NULL);
        ctx.thread_running = false;
        ctx.thread_joined = true;
    }

    // If an update was downloaded and cl_autoupdate == 2, apply update quietly on exit
    if (ctx.state == UPDATE_STATE_READY && cl_autoupdate && cl_autoupdate->integer == 2) {
        char exe_path[MAX_OSPATH];
        if (Sys_GetBinaryPath(exe_path, sizeof(exe_path))) {
            char old_path[MAX_OSPATH];
            Q_snprintf(old_path, sizeof(old_path), "%s.old", exe_path);
#if defined(_WIN32)
            DeleteFileA(old_path);
            if (MoveFileA(exe_path, old_path)) {
                MoveFileA(ctx.temp_file_path, exe_path);
            }
#else
            unlink(old_path);
            if (rename(exe_path, old_path) == 0) {
                if (rename(ctx.temp_file_path, exe_path) == 0) {
                    chmod(exe_path, 0755);
                    unlink(old_path);
                } else {
                    rename(old_path, exe_path);
                }
            }
#endif
        }
    }

    pthread_mutex_destroy(&ctx.lock);
}
