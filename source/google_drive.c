/* Google-only OAuth/Drive transport. Never use Apollo HTTP handles or logging. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <time.h>
#include <SDL2/SDL.h>
#include <curl/curl.h>
#include "cJSON.h"
#include "google_build_config.h"
#include "google_drive.h"
#include "google_store.h"

#define SCOPE "https://www.googleapis.com/auth/drive.file"
#define TOKEN_URL "https://oauth2.googleapis.com/token"
#define DEVICE_URL "https://oauth2.googleapis.com/device/code"
#define LIST_URL "https://www.googleapis.com/drive/v3/files?pageSize=1&fields=files(id)"
#define TOKEN_CAP 8192
#define RESPONSE_CAP 65536
static SDL_mutex *lock;
static SDL_Thread *thread;
static SDL_atomic_t cancelled;
static google_drive_status state;
static uint32_t current_user;
static int current_action;

static void wipe(void *p, size_t n)
{
    volatile unsigned char *v = p;
    while (n--) *v++ = 0;
}

static double now(void)
{
    struct timespec t = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return -1;
    return t.tv_sec + t.tv_nsec / 1000000000.0;
}

static void message(const char *s)
{
    SDL_LockMutex(lock);
    snprintf(state.message, sizeof(state.message), "%s", s);
    SDL_UnlockMutex(lock);
}

/* Once the local atomic update starts, let it finish before reporting a result. */
static int begin_commit(void)
{
    SDL_LockMutex(lock);
    int ok = !SDL_AtomicGet(&cancelled);
    if (ok) state.cancellable = 0;
    SDL_UnlockMutex(lock);
    return ok;
}

static void destroy_json(cJSON *j)
{
    for (cJSON *p = j; p; p = p->next) {
        if (p->valuestring) wipe(p->valuestring, strlen(p->valuestring));
        if (p->child) destroy_json(p->child);
    }
    /* Wipe recursively here, delete once at the call site. */
}
static void release(cJSON *j)
{
    destroy_json(j);
    cJSON_Delete(j);
}

typedef struct { char *data; size_t size; } response_buffer;
static size_t receive(void *data, size_t size, size_t count, void *opaque)
{
    response_buffer *b = opaque;
    if (size && count > RESPONSE_CAP / size) return 0;
    size_t n = size * count;
    if (n > RESPONSE_CAP - b->size) return 0;
    memcpy(b->data + b->size, data, n);
    b->size += n;
    b->data[b->size] = 0;
    return n;
}
static int progress(void *p, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d)
{
    (void)p; (void)a; (void)b; (void)c; (void)d;
    return SDL_AtomicGet(&cancelled);
}

/* Fresh handles, HTTPS only, no redirects, no verbose output, bounded responses. */
static cJSON *request(const char *url, const char *form, const char *access, long *status)
{
    CURL *curl = curl_easy_init();
    struct curl_slist *headers = NULL;
    response_buffer b = {calloc(1, RESPONSE_CAP + 1), 0};
    char auth[TOKEN_CAP + 32] = {0};
    cJSON *json = NULL;
    *status = 0;
    if (!curl || !b.data) goto done;
    headers = curl_slist_append(NULL, "Accept: application/json");
    if (!headers) goto done;
    if (access) {
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", access);
        struct curl_slist *next = curl_slist_append(headers, auth);
        if (!next) goto done;
        headers = next;
    }
#define OPT(k,v) if (curl_easy_setopt(curl, k, v) != CURLE_OK) goto done
    OPT(CURLOPT_URL, url);
    OPT(CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    OPT(CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
    OPT(CURLOPT_FOLLOWLOCATION, 0L);
    OPT(CURLOPT_SSL_VERIFYPEER, 1L);
    OPT(CURLOPT_SSL_VERIFYHOST, 2L);
    OPT(CURLOPT_CAINFO, "/mnt/sandbox/PSSY00001_000/app0/assets/google/cacert.pem");
    OPT(CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
    OPT(CURLOPT_VERBOSE, 0L);
    OPT(CURLOPT_NOSIGNAL, 1L);
    OPT(CURLOPT_CONNECTTIMEOUT, 10L);
    OPT(CURLOPT_TIMEOUT, 30L);
    OPT(CURLOPT_HTTPHEADER, headers);
    OPT(CURLOPT_WRITEFUNCTION, receive);
    OPT(CURLOPT_WRITEDATA, &b);
    OPT(CURLOPT_NOPROGRESS, 0L);
    OPT(CURLOPT_XFERINFOFUNCTION, progress);
    if (form) { OPT(CURLOPT_POSTFIELDS, form); }
    if (SDL_AtomicGet(&cancelled) || curl_easy_perform(curl) != CURLE_OK) goto done;
    if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status) != CURLE_OK) goto done;
    json = cJSON_ParseWithOpts(b.data, NULL, 1);
    if (!cJSON_IsObject(json)) { release(json); json = NULL; }
done:
    /* Keep callback/form/header storage alive until the easy handle is gone. */
    if (curl) curl_easy_cleanup(curl);
    if (b.data) { wipe(b.data, RESPONSE_CAP + 1); free(b.data); }
    wipe(auth, sizeof(auth));
    /* curl's header copy contains the access token. */
    for (struct curl_slist *p = headers; p; p = p->next) wipe(p->data, strlen(p->data));
    curl_slist_free_all(headers);
    return json;
#undef OPT
}

static int copy_string(cJSON *j, const char *key, char *out, size_t cap, int printable)
{
    cJSON *p = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!cJSON_IsString(p) || !p->valuestring[0] || strlen(p->valuestring) >= cap) return 0;
    for (const unsigned char *s = (void*)p->valuestring; *s; s++)
        if (*s < 32 || *s == 127 || (printable && *s > 126)) return 0;
    strcpy(out, p->valuestring);
    return 1;
}
static double seconds(cJSON *j, const char *key)
{
    cJSON *p = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsNumber(p) && isfinite(p->valuedouble) && p->valuedouble > 0 &&
        p->valuedouble <= 86400 ? p->valuedouble : 0;
}
static int error_is(cJSON *j, const char *value)
{
    cJSON *p = cJSON_GetObjectItemCaseSensitive(j, "error");
    return cJSON_IsString(p) && !strcmp(p->valuestring, value);
}
static int scope_ok(cJSON *j, int required)
{
    cJSON *p = cJSON_GetObjectItemCaseSensitive(j, "scope");
    if (!p) return !required;
    /* This app never accepts additional scopes. */
    return cJSON_IsString(p) && !strcmp(p->valuestring, SCOPE);
}
static int wait_until(double target)
{
    double current;
    while ((current = now()) >= 0 && current < target) {
        if (SDL_AtomicGet(&cancelled)) return 0;
        SDL_Delay(50);
    }
    if (current < 0) message("Console monotonic clock unavailable. Google operation stopped.");
    return current >= 0 && !SDL_AtomicGet(&cancelled);
}

/* All form values are escaped, including registration and device/refresh codes. */
static cJSON *post(const char *url, const char *key, const char *value, const char *grant, long *status)
{
    CURL *c = curl_easy_init();
    char *id = NULL, *secret = NULL, *v = NULL, *g = NULL, *body = NULL;
    cJSON *j = NULL;
    *status = 0;
    if (!c) return NULL;
    id = curl_easy_escape(c, GDRIVE_CLIENT_ID, 0);
    secret = curl_easy_escape(c, GDRIVE_CLIENT_SECRET, 0);
    v = curl_easy_escape(c, value, 0);
    if (grant) g = curl_easy_escape(c, grant, 0);
    if (!id || !secret || !v || (grant && !g)) goto done;
    size_t cap = strlen(id) + strlen(secret) + strlen(v) + (g ? strlen(g) : 0) + 128;
    body = calloc(1, cap);
    if (!body) goto done;
    if (grant) snprintf(body, cap, "client_id=%s&client_secret=%s&%s=%s&grant_type=%s", id, secret, key, v, g);
    else snprintf(body, cap, "client_id=%s&%s=%s", id, key, v);
    j = request(url, body, NULL, status);
done:
    if (body) { wipe(body, strlen(body)); free(body); }
    if (secret) wipe(secret, strlen(secret));
    if (v) wipe(v, strlen(v));
    curl_free(id); curl_free(secret); curl_free(v); curl_free(g);
    curl_easy_cleanup(c);
    return j;
}

static int accept_tokens(cJSON *j, char *access, char *refresh, int initial)
{
    char type[32];
    if (cJSON_GetObjectItemCaseSensitive(j, "error") || cJSON_GetObjectItemCaseSensitive(j, "error_code") ||
        !scope_ok(j, initial) || !copy_string(j, "token_type", type, sizeof(type), 1) ||
        strcasecmp(type, "Bearer") || !seconds(j, "expires_in") ||
        !copy_string(j, "access_token", access, TOKEN_CAP, 1)) return 0;
    if (initial || cJSON_GetObjectItemCaseSensitive(j, "refresh_token"))
        return copy_string(j, "refresh_token", refresh, TOKEN_CAP, 1);
    return 1;
}
static int refresh_access(char *refresh, char *access)
{
    long status = 0;
    cJSON *j = post(TOKEN_URL, "refresh_token", refresh, "refresh_token", &status);
    cJSON *replacement = cJSON_GetObjectItemCaseSensitive(j, "refresh_token");
    int rotated = cJSON_IsString(replacement) && strcmp(replacement->valuestring, refresh);
    int ok = status == 200 && accept_tokens(j, access, refresh, 0);
    if (!ok) message(error_is(j, "invalid_grant") ?
        "Google connection expired or revoked. Disconnect, then connect again." :
        "Could not refresh access. Check network, TLS certificates, and console clock.");
    /* Preserve a replacement refresh token even if the next Drive request fails
       or is cancelled. The previous refresh token may no longer be valid. */
    if (ok && rotated) {
        if (!begin_commit()) ok = 0;
        else {
            ok = google_store(current_user, GOOGLE_STORE_WRITE, refresh, TOKEN_CAP);
            SDL_LockMutex(lock); state.cancellable = 1; SDL_UnlockMutex(lock);
            if (!ok) message("Could not save refreshed credentials. Connect again.");
        }
    }
    release(j);
    return ok;
}

static int authorize(char *access, char *refresh)
{
    long status = 0;
    char device[TOKEN_CAP] = {0}, url[256], code[64], endpoint[256];
    int ok = 0;
    cJSON *j = request("https://accounts.google.com/.well-known/openid-configuration", NULL, NULL, &status);
    if (status != 200 || !copy_string(j, "device_authorization_endpoint", endpoint, sizeof(endpoint), 1) ||
        strcmp(endpoint, DEVICE_URL)) {
        message("Google discovery failed. Check network, TLS certificates, and console clock.");
        goto done;
    }
    release(j);
    j = post(endpoint, "scope", SCOPE, NULL, &status);
    double interval = seconds(j, "interval"), lifetime = seconds(j, "expires_in");
    double deadline = now() + lifetime;
    if (status != 200 || !interval || !lifetime ||
        !copy_string(j, "device_code", device, sizeof(device), 1) ||
        !copy_string(j, "verification_url", url, sizeof(url), 1) ||
        !copy_string(j, "user_code", code, sizeof(code), 1)) {
        message("Google device authorization failed. Try connecting again later."); goto done;
    }
    SDL_LockMutex(lock);
    strcpy(state.verification_url, url);
    strcpy(state.user_code, code);
    SDL_UnlockMutex(lock);
    message("Open this URL on your phone and enter the code. Use Cancel to stop.");
    while (!SDL_AtomicGet(&cancelled)) {
        release(j); j = NULL;
        double target = now() + interval;
        if (target >= deadline) { message("Authorization expired. Connect again for a new code."); break; }
        if (!wait_until(target)) break;
        j = post(TOKEN_URL, "device_code", device, "urn:ietf:params:oauth:grant-type:device_code", &status);
        if (now() >= deadline) { message("Authorization expired. Connect again for a new code."); break; }
        if (!j) { message("Network or TLS failure. Connect again to retry."); break; }
        if (error_is(j, "authorization_pending")) continue;
        if (error_is(j, "slow_down")) { interval += 5; continue; }
        if (error_is(j, "access_denied")) { message("Google authorization denied."); break; }
        if (error_is(j, "expired_token")) { message("Authorization expired. Connect again for a new code."); break; }
        if (status != 200 || !accept_tokens(j, access, refresh, 1)) {
            message("Google authorization failed or required Drive scope was not granted."); break;
        }
        ok = 1; break;
    }
done:
    release(j);
    wipe(device, sizeof(device));
    return ok;
}

static int worker(void *unused)
{
    (void)unused;
    char *refresh = calloc(1, TOKEN_CAP), *access = calloc(1, TOKEN_CAP);
    long status = 0;
    cJSON *j = NULL;
    if (now() < 0 && current_action != GOOGLE_DISCONNECT) {
        message("Console monotonic clock unavailable. Google operation stopped."); goto done;
    }
    if (!refresh || !access) { message("Not enough memory for Google connection."); goto done; }
    if (current_action == GOOGLE_DISCONNECT) {
        message(google_store(current_user, GOOGLE_STORE_CLEAR, refresh, TOKEN_CAP) ?
            "Disconnected on this PS4 user. Other consoles are unaffected." : "Could not clear local credentials.");
        goto done;
    }
    if (current_action == GOOGLE_CONNECT) {
        if (!authorize(access, refresh)) goto done;
    } else {
        if (!google_store(current_user, GOOGLE_STORE_READ, refresh, TOKEN_CAP)) {
            message("Could not read this user's Google credentials."); goto done;
        }
        if (!refresh[0]) { message("Google Drive is not connected for this PS4 user."); goto done; }
        /* Access tokens are memory-only. Refresh before every status check. */
        if (!refresh_access(refresh, access)) goto done;
    }
    if (SDL_AtomicGet(&cancelled)) goto done;
    message("Checking Google Drive access...");
    j = request(LIST_URL, NULL, access, &status);
    if (status == 401 && !SDL_AtomicGet(&cancelled)) {
        release(j); j = NULL;
        if (!refresh_access(refresh, access)) goto done;
        j = request(LIST_URL, NULL, access, &status);
    }
    if (status != 200 || !cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(j, "files")) ||
        cJSON_GetObjectItemCaseSensitive(j, "error")) {
        message("Drive files.list failed. Check network and Drive API configuration."); goto done;
    }
    if (!begin_commit()) goto done;
    message(google_store(current_user, GOOGLE_STORE_WRITE, refresh, TOKEN_CAP) ?
        "Connected. Drive files.list succeeded; refresh token saved for this user." :
        "Drive access succeeded, but credentials could not be saved. Connect again.");
done:
    release(j);
    if (refresh) { wipe(refresh, TOKEN_CAP); free(refresh); }
    if (access) { wipe(access, TOKEN_CAP); free(access); }
    if (SDL_AtomicGet(&cancelled)) message("Google operation cancelled.");
    SDL_LockMutex(lock);
    state.busy = 0;
    state.user_code[0] = 0;
    state.verification_url[0] = 0;
    SDL_UnlockMutex(lock);
    return 0;
}

int google_drive_start(int action, uint32_t user)
{
    if (action < GOOGLE_CONNECT || action > GOOGLE_DISCONNECT) return 0;
    if (!lock) lock = SDL_CreateMutex();
    if (!lock) return 0;
    google_drive_status snapshot;
    google_drive_snapshot(&snapshot);
    if (snapshot.busy) return 0;
    if (thread) { SDL_WaitThread(thread, NULL); thread = NULL; }
    SDL_AtomicSet(&cancelled, 0);
    current_action = action; current_user = user;
    SDL_LockMutex(lock);
    memset(&state, 0, sizeof(state));
    state.busy = 1;
    state.cancellable = action != GOOGLE_DISCONNECT;
    strcpy(state.message, "Contacting Google...");
    SDL_UnlockMutex(lock);
    thread = SDL_CreateThread(worker, "google_drive", NULL);
    if (!thread) {
        SDL_LockMutex(lock); state.busy = 0; SDL_UnlockMutex(lock);
        message("Could not start Google connection worker."); return 0;
    }
    return 1;
}
void google_drive_snapshot(google_drive_status *out)
{
    if (!lock) { memset(out, 0, sizeof(*out)); return; }
    SDL_LockMutex(lock); *out = state; SDL_UnlockMutex(lock);
}
void google_drive_cancel(void)
{
    if (!lock) return;
    SDL_LockMutex(lock);
    if (state.busy && state.cancellable) SDL_AtomicSet(&cancelled, 1);
    SDL_UnlockMutex(lock);
}
void google_drive_shutdown(void)
{
    google_drive_cancel();
    if (thread) { SDL_WaitThread(thread, NULL); thread = NULL; }
    if (lock) { SDL_DestroyMutex(lock); lock = NULL; }
}
