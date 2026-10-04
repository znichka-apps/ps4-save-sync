/* Google-only OAuth/Drive transport. Never use Apollo HTTP handles or logging. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <SDL2/SDL.h>
#include <curl/curl.h>
#include "cJSON.h"
#include "google_build_config.h"
#include "google_drive.h"
#include "google_store.h"
#include "google_ca.h"
#include "google_restore.h"

#define SCOPE "https://www.googleapis.com/auth/drive.file"
#define DISCOVERY_URL "https://accounts.google.com/.well-known/openid-configuration"
#define CA_PATH GOOGLE_CA_PATH
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
static int ca_request_failed;
static google_backup backup;
static google_backup_page browser_page;
static char browser_folder[129], browser_token[512];
static uint32_t browser_user;
static google_remote_backup selected_backup;
static int selected_ready;
typedef struct { FILE *file; uint64_t count, limit; } download_stream;
static size_t download_write(void *buffer, size_t size, size_t count, void *data)
{
    download_stream *s = data;
    if (SDL_AtomicGet(&cancelled) || (size && count > SIZE_MAX / size)) return 0;
    size_t n = size * count;
    if (n > s->limit - s->count) return 0;
    size_t written = fwrite(buffer,1,n,s->file);
    s->count += written;
    SDL_LockMutex(lock); state.completed_bytes = s->count; state.total_bytes = s->limit; SDL_UnlockMutex(lock);
    return written;
}

typedef struct {
    const google_upload_request *request;
    uint64_t remaining;
} upload_stream;
static size_t stream_read(void *buffer, size_t size, size_t count, void *data)
{
    upload_stream *s = data;
    if (SDL_AtomicGet(&cancelled) || (size && count > SIZE_MAX / size)) return CURL_READFUNC_ABORT;
    size_t capacity = size * count;
    if (capacity > 16384) capacity = 16384;
    if (capacity > s->remaining) capacity = (size_t)s->remaining;
    if (!capacity) return 0;
    size_t n = fread(buffer, 1, capacity, s->request->file);
    if (!n || ferror(s->request->file)) return CURL_READFUNC_ABORT;
    s->remaining -= n;
    return n;
}
static size_t upload_header(void *buffer, size_t size, size_t count, void *data)
{
    google_upload_response *r = data;
    if (size && count > SIZE_MAX / size) return 0;
    size_t n = size * count;
    const char *line = buffer;
    if (n >= 5 && !strncmp(line, "HTTP/", 5)) {
        r->location[0] = r->range[0] = 0; r->invalid_headers = 0;
    }
    char *out = NULL; size_t capacity = 0, skip = 0;
    if (n >= 9 && !strncasecmp(line,"Location:",9)) { out = r->location; capacity = sizeof(r->location); skip = 9; }
    if (n >= 6 && !strncasecmp(line,"Range:",6)) { out = r->range; capacity = sizeof(r->range); skip = 6; }
    if (out) {
        while (skip < n && (line[skip] == ' ' || line[skip] == '\t')) skip++;
        size_t end = n;
        while (end > skip && (line[end-1] == '\r' || line[end-1] == '\n')) end--;
        if (out[0] || end - skip >= capacity || memchr(line + skip,0,end-skip)) r->invalid_headers = 1;
        else { memcpy(out,line+skip,end-skip); out[end-skip] = 0; }
    }
    return n;
}

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

static void diagnostic_text(char *out, size_t capacity, const char *text)
{
    size_t i = 0;
    if (!text) text = "unavailable";
    for (; text[i] && i + 1 < capacity; i++) {
        unsigned char c = (unsigned char)text[i];
        out[i] = c >= 32 && c <= 126 ? (char)c : ' ';
    }
    out[i] = 0;
}

/* Never copy arbitrary error-buffer text into the UI. In particular, proxy
   URLs, credentials, headers, and server-supplied strings must stay private.
   Show only recognized reasons and a strictly formatted mbedTLS error code. */
static void discovery_error_detail(char *out, size_t capacity, const char *error)
{
    char lower[CURL_ERROR_SIZE] = {0};
    size_t i = 0;
    for (; error[i] && i + 1 < sizeof(lower); i++)
        lower[i] = (char)tolower((unsigned char)error[i]);
    const char *reason = error[0] ? "Unrecognized error details withheld." : "No libcurl error detail.";
    if (strstr(lower, "authorization") || strstr(lower, "bearer") || strstr(lower, "secret") ||
        strstr(lower, "token") || strstr(lower, "password") || strstr(lower, "passwd"))
        reason = "Sensitive error details withheld.";
    else if (strstr(lower, "resolve proxy")) reason = "Proxy hostname resolution failed.";
    else if (strstr(lower, "resolve host")) reason = "Google hostname resolution failed.";
    else if (strstr(lower, "subject name") || strstr(lower, "hostname mismatch")) reason = "Certificate hostname mismatch.";
    else if (strstr(lower, "ca cert file") || strstr(lower, "cafile") || strstr(lower, "ca file")) reason = "CA certificate file could not be loaded.";
    else if (strstr(lower, "certificate") || strstr(lower, "x509")) reason = "Certificate verification failed.";
    else if (strstr(lower, "timed out") || strstr(lower, "timeout")) reason = "Request timed out.";
    else if (strstr(lower, "connect")) reason = "Connection failed.";
    else if (strstr(lower, "handshake") || strstr(lower, "mbedtls") || strstr(lower, "ssl")) reason = "TLS negotiation failed.";
    char backend_code[16] = {0};
    const char *code = strstr(lower, "mbedtls: (-0x");
    if (code) {
        code += strlen("mbedtls: (");
        size_t n = 3;
        while (n < 11 && isxdigit((unsigned char)code[n])) n++;
        if (n > 3 && code[n] == ')' && !strstr(reason, "withheld")) {
            memcpy(backend_code, code, n);
            backend_code[n] = 0;
        }
    }
    snprintf(out, capacity, "%s%s%s", reason, backend_code[0] ? " Backend code: " : "", backend_code);
    wipe(lower, sizeof(lower));
}

static void discovery_context(const char *error, const google_ca_context *ca_context)
{
    struct stat info;
    int exists = stat(CA_PATH, &info) == 0;
    int stat_error = exists ? 0 : errno;
    int readable = 0;
    FILE *ca = fopen(CA_PATH, "rb");
    if (ca) {
        (void)fgetc(ca);
        readable = !ferror(ca);
        if (fclose(ca) != 0) readable = 0;
    }
    curl_version_info_data *version = curl_version_info(CURLVERSION_FIRST);
    char curl_version[48], tls[64], details[144], context[640], ca_details[192];
    diagnostic_text(curl_version, sizeof(curl_version), version ? version->version : NULL);
    diagnostic_text(tls, sizeof(tls), version ? version->ssl_version : NULL);
    discovery_error_detail(details, sizeof(details), error);
    google_ca_diagnostic(ca_context, ca_details, sizeof(ca_details));
    snprintf(context, sizeof(context), "CA: exists=%s; readable=%s\nlibcurl: %s\nTLS backend: %s\nDetails: %s\n%s",
        exists ? "yes" : (stat_error == ENOENT ? "no" : "unknown"), readable ? "yes" : "no",
        curl_version, tls, details, ca_details);
    SDL_LockMutex(lock);
    snprintf(state.discovery_details, sizeof(state.discovery_details), "%s", context);
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
    (void)a; (void)b; (void)c;
    upload_stream *stream = p;
    if (stream && stream->request->file) {
        SDL_LockMutex(lock);
        state.completed_bytes = stream->request->offset + (d > 0 ? (uint64_t)d : 0);
        state.total_bytes = stream->request->total;
        SDL_UnlockMutex(lock);
    }
    return SDL_AtomicGet(&cancelled);
}

/* Fresh handles, HTTPS only, no redirects, no verbose output, bounded responses. */
static cJSON *request_extended(const char *url, const char *form, const char *access, long *status,
                              const google_upload_request *upload, google_upload_response *reply)
{
    CURL *curl = curl_easy_init();
    struct curl_slist *headers = NULL;
    response_buffer b = {calloc(1, RESPONSE_CAP + 1), 0};
    char auth[TOKEN_CAP + 32] = {0};
    /* Stack lifetime includes easy_cleanup; this is never emitted for OAuth,
       refresh, or authenticated Drive requests. */
    char error[CURL_ERROR_SIZE] = {0};
    int discovery = !form && !access && !strcmp(url, DISCOVERY_URL);
    CURLcode result = CURLE_OK;
    google_ca_context ca_context;
    upload_stream stream = {upload, upload ? upload->length : 0};
    download_stream sink = {upload ? upload->download : NULL, 0, upload ? upload->total : 0};
    google_ca_init(&ca_context);
    ca_request_failed = 0;
    const char *failure = NULL, *failed_option = NULL;
    cJSON *json = NULL;
    *status = 0;
    if (!curl || !b.data) { result = CURLE_OUT_OF_MEMORY; failure = "initialization"; goto done; }
    headers = curl_slist_append(NULL, sink.file ? "Accept: application/zip" : "Accept: application/json");
    if (!headers) { result = CURLE_OUT_OF_MEMORY; failure = "initialization"; goto done; }
    if (access) {
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", access);
        struct curl_slist *next = curl_slist_append(headers, auth);
        if (!next) { result = CURLE_OUT_OF_MEMORY; goto done; }
        headers = next;
    }
    if (upload) {
        const char *type = upload->json ? "Content-Type: application/json; charset=UTF-8" : "Content-Type: application/zip";
        struct curl_slist *next = curl_slist_append(headers,type);
        if (!next) { result = CURLE_OUT_OF_MEMORY; goto done; } headers = next;
        next = curl_slist_append(headers,"Expect:");
        if (!next) { result = CURLE_OUT_OF_MEMORY; goto done; } headers = next;
        char value[128];
        if (upload->range) {
            snprintf(value,sizeof(value),"Content-Range: %s",upload->range);
            next = curl_slist_append(headers,value);
            if (!next) { result = CURLE_OUT_OF_MEMORY; goto done; } headers = next;
        }
        if (upload->json && upload->total) {
            next = curl_slist_append(headers,"X-Upload-Content-Type: application/zip");
            if (!next) { result = CURLE_OUT_OF_MEMORY; goto done; } headers = next;
            snprintf(value,sizeof(value),"X-Upload-Content-Length: %llu",(unsigned long long)upload->total);
            next = curl_slist_append(headers,value);
            if (!next) { result = CURLE_OUT_OF_MEMORY; goto done; } headers = next;
        }
    }
#define OPT(k,v) do { result = curl_easy_setopt(curl, k, v); \
    if (result != CURLE_OK) { failure = "setup"; failed_option = #k; \
        if (k == CURLOPT_SSL_CTX_FUNCTION || k == CURLOPT_SSL_CTX_DATA || \
            k == CURLOPT_CAINFO || k == CURLOPT_CAPATH || k == CURLOPT_FRESH_CONNECT || \
            k == CURLOPT_FORBID_REUSE || k == CURLOPT_SSL_SESSIONID_CACHE) { \
            ca_request_failed = 1; ca_context.problem = "TLS CA option setup failed"; } \
        goto done; } } while (0)
    if (discovery) { OPT(CURLOPT_ERRORBUFFER, error); }
    result = google_ca_load(&ca_context);
    if (result != CURLE_OK) { failure = "CA loading"; ca_request_failed = 1; goto done; }
    OPT(CURLOPT_URL, url);
    OPT(CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    OPT(CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
    OPT(CURLOPT_FOLLOWLOCATION, 0L);
    OPT(CURLOPT_SSL_VERIFYPEER, 1L);
    OPT(CURLOPT_SSL_VERIFYHOST, 2L);
    /* NULL skips only mbedTLS's file/path loaders, not peer/hostname checks. */
    OPT(CURLOPT_CAINFO, NULL);
    OPT(CURLOPT_CAPATH, NULL);
    OPT(CURLOPT_SSL_CTX_FUNCTION, google_ca_ssl_context);
    OPT(CURLOPT_SSL_CTX_DATA, &ca_context);
    OPT(CURLOPT_FRESH_CONNECT, 1L);
    OPT(CURLOPT_FORBID_REUSE, 1L);
    OPT(CURLOPT_SSL_SESSIONID_CACHE, 0L);
    OPT(CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
    OPT(CURLOPT_VERBOSE, 0L);
    OPT(CURLOPT_NOSIGNAL, 1L);
    OPT(CURLOPT_CONNECTTIMEOUT, 10L);
    OPT(CURLOPT_TIMEOUT, sink.file ? 0L : 30L);
    if (sink.file) {
        OPT(CURLOPT_LOW_SPEED_LIMIT, 1L);
        OPT(CURLOPT_LOW_SPEED_TIME, 30L);
    }
    OPT(CURLOPT_HTTPHEADER, headers);
    OPT(CURLOPT_WRITEFUNCTION, receive);
    OPT(CURLOPT_WRITEDATA, &b);
    if (sink.file) {
        OPT(CURLOPT_WRITEFUNCTION, download_write);
        OPT(CURLOPT_WRITEDATA, &sink);
    }
    OPT(CURLOPT_NOPROGRESS, 0L);
    OPT(CURLOPT_XFERINFOFUNCTION, progress);
    OPT(CURLOPT_XFERINFODATA, upload ? &stream : NULL);
    if (upload) {
        OPT(CURLOPT_HEADERFUNCTION, upload_header);
        OPT(CURLOPT_HEADERDATA, reply);
        OPT(CURLOPT_CUSTOMREQUEST, upload->method);
        if (upload->file) {
            if (upload->offset > INT64_MAX || fseeko(upload->file,(off_t)upload->offset,SEEK_SET) != 0) {
                result = CURLE_READ_ERROR; goto done;
            }
            OPT(CURLOPT_UPLOAD, 1L);
            OPT(CURLOPT_READFUNCTION, stream_read);
            OPT(CURLOPT_READDATA, &stream);
            OPT(CURLOPT_INFILESIZE_LARGE, (curl_off_t)upload->length);
        } else if (upload->json || !strcmp(upload->method,"PUT")) {
            OPT(CURLOPT_POSTFIELDS, upload->json ? upload->json : "");
            OPT(CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)(upload->json ? strlen(upload->json) : 0));
        }
    }
    if (form) { OPT(CURLOPT_POSTFIELDS, form); }
    result = SDL_AtomicGet(&cancelled) ? CURLE_ABORTED_BY_CALLBACK : curl_easy_perform(curl);
    if (result != CURLE_OK) {
        failure = "transport";
        if (ca_context.problem) ca_request_failed = 1;
        goto done;
    }
    if (!ca_context.attached) {
        result = CURLE_SSL_CONNECT_ERROR; failure = "TLS CA context"; ca_request_failed = 1;
        ca_context.problem = "TLS CA callback was not invoked";
        goto done;
    }
    result = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status);
    if (result != CURLE_OK) { failure = "HTTP status lookup"; goto done; }
    if (discovery && *status != 200) { failure = "HTTP"; goto done; }
    if (!sink.file) {
        const char *end = NULL;
        if (!memchr(b.data,0,b.size) && !strstr(b.data,"\\u0000"))
            json = cJSON_ParseWithOpts(b.data, &end, 1);
        if (end != b.data + b.size) { release(json); json = NULL; }
        if (!cJSON_IsObject(json)) { failure = "JSON"; release(json); json = NULL; }
    }
done:
    /* Keep callback/form/header storage alive until the easy handle is gone. */
    if (curl) curl_easy_cleanup(curl);
    if (reply) { reply->transport = result; reply->status = *status; reply->downloaded = sink.count; }
    if (discovery || ca_request_failed) {
        /* Public CA state only, even for authenticated requests; never emit
           their curl error buffers, request data, or response bodies. */
        discovery_context(discovery ? error : "", &ca_context);
        if (failure) {
            char summary[192], description[96];
            diagnostic_text(description, sizeof(description), curl_easy_strerror(result));
            if (!strcmp(failure, "HTTP"))
                snprintf(summary, sizeof(summary), "Google discovery: HTTP %ld (expected 200).", *status);
            else if (!strcmp(failure, "JSON"))
                snprintf(summary, sizeof(summary), "Google discovery: malformed JSON or non-object response.");
            else if (failed_option)
                snprintf(summary, sizeof(summary), "Discovery setup failed: %s. curl %d: %s", failed_option, (int)result, description);
            else
                snprintf(summary, sizeof(summary), "%s %s failed. curl %d: %s", discovery ? "Discovery" : "Google", failure, (int)result, description);
            message(summary);
        }
    }
    /* The SSL config holds pointers into this chain until easy_cleanup ends. */
    google_ca_free(&ca_context);
    wipe(error, sizeof(error));
    if (b.data) { wipe(b.data, RESPONSE_CAP + 1); free(b.data); }
    wipe(auth, sizeof(auth));
    /* curl's header copy contains the access token. */
    for (struct curl_slist *p = headers; p; p = p->next) wipe(p->data, strlen(p->data));
    curl_slist_free_all(headers);
    return json;
#undef OPT
}
static cJSON *request(const char *url, const char *form, const char *access, long *status)
{
    return request_extended(url,form,access,status,NULL,NULL);
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
    ca_request_failed = 0;
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
    if (!ok && !ca_request_failed) message(error_is(j, "invalid_grant") ?
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
    cJSON *j = request(DISCOVERY_URL, NULL, NULL, &status);
    if (status != 200 || !j) goto done; /* Preserve request's precise diagnostic. */
    if (!copy_string(j, "device_authorization_endpoint", endpoint, sizeof(endpoint), 1)) {
        message("Google discovery: missing or invalid device_authorization_endpoint.");
        goto done;
    }
    if (strcmp(endpoint, DEVICE_URL)) {
        message("Google discovery: unexpected device_authorization_endpoint (refused).");
        goto done;
    }
    SDL_LockMutex(lock); state.discovery_details[0] = 0; SDL_UnlockMutex(lock);
    release(j);
    j = post(endpoint, "scope", SCOPE, NULL, &status);
    double interval = seconds(j, "interval"), lifetime = seconds(j, "expires_in");
    double deadline = now() + lifetime;
    if (status != 200 || !interval || !lifetime ||
        !copy_string(j, "device_code", device, sizeof(device), 1) ||
        !copy_string(j, "verification_url", url, sizeof(url), 1) ||
        !copy_string(j, "user_code", code, sizeof(code), 1)) {
        if (!ca_request_failed) message("Google device authorization failed. Try connecting again later.");
        goto done;
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
        if (!j) { if (!ca_request_failed) message("Network or TLS failure. Connect again to retry."); break; }
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

typedef struct { char *refresh, *access; } upload_auth;
static int upload_cancelled(void *data) { (void)data; return SDL_AtomicGet(&cancelled); }
static int upload_refresh(void *data)
{
    upload_auth *auth = data; return refresh_access(auth->refresh,auth->access);
}
static int upload_wait(void *data, unsigned seconds)
{
    (void)data; return wait_until(now() + seconds);
}
static void upload_progress(void *data, uint64_t completed, uint64_t total)
{
    (void)data; SDL_LockMutex(lock);
    state.completed_bytes = completed; state.total_bytes = total;
    SDL_UnlockMutex(lock);
}
static void upload_request(void *data, const google_upload_request *q, google_upload_response *r)
{
    upload_auth *auth = data; long status;
    /* Session URLs must be validated before any Authorization header is created. */
    if (!strcmp(q->method,"PUT") && !google_upload_session_url(q->url)) {
        r->transport = CURLE_URL_MALFORMAT; return;
    }
    r->json = request_extended(q->url,NULL,auth->access,&status,q,r);
}
static int restore_finish(void *unused) { (void)unused; return begin_commit(); }
static int worker(void *unused)
{
    (void)unused;
    int upload_handled = 0;
    int download_handled = 0;
    char *refresh = calloc(1, TOKEN_CAP), *access = calloc(1, TOKEN_CAP);
    long status = 0;
    cJSON *j = NULL;
    if (now() < 0 && current_action != GOOGLE_DISCONNECT) {
        message("Console monotonic clock unavailable. Google operation stopped."); goto done;
    }
    if (!refresh || !access) { message("Not enough memory for Google connection."); goto done; }
    if (current_action == GOOGLE_UPLOAD) {
        message("Preparing decrypted ZIP backup...");
        if (!google_backup_stage(&backup,upload_cancelled,NULL)) {
            SDL_LockMutex(lock);
            snprintf(state.preparation_details, sizeof(state.preparation_details), "%s", backup.diagnostic);
            SDL_UnlockMutex(lock);
            message(backup.mount_blocked ? "Save unmount failed. Restart the app before further save operations." :
                "Could not prepare backup. Source save was not changed.");
            goto done;
        }
    }
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
    if (current_action == GOOGLE_RESTORE) {
        upload_auth auth = {refresh,access};
        google_upload_io io = {&auth,upload_request,upload_refresh,upload_cancelled,upload_progress,upload_wait};
        message("Rechecking selected backup metadata and archive...");
        if (!google_download_recheck(&selected_backup,&io)) {
            message("Backup metadata changed or could not be rechecked. Restore refused; no target written."); goto done;
        }
        int result=google_restore_local(&selected_backup.backup,upload_cancelled,restore_finish,NULL);
        if (result==GOOGLE_UPLOAD_SUCCESS) {
            selected_ready=0;
            message(google_backup_cleanup(&selected_backup.backup)?
                "Restore complete for this PS4 user. Save unmounted successfully.":
                "Restore complete; save unmounted. Temporary ZIP cleanup failed.");
        } else {
            message(result==GOOGLE_UPLOAD_CANCELLED?"Restore cancelled; NOT successful.":"Restore failed; NOT successful. Downloaded ZIP retained.");
            SDL_LockMutex(lock);
            snprintf(state.preparation_details,sizeof(state.preparation_details),"%s",selected_backup.backup.diagnostic);
            SDL_UnlockMutex(lock);
        }
        goto done;
    }
    if (current_action == GOOGLE_BROWSE || current_action == GOOGLE_NEXT || current_action == GOOGLE_DOWNLOAD) {
        upload_auth auth = {refresh,access};
        google_upload_io io = {&auth,upload_request,upload_refresh,upload_cancelled,upload_progress,upload_wait};
        if (current_action == GOOGLE_DOWNLOAD) {
            message("Downloading and validating backup...");
            int result = google_download_run(&selected_backup,&io);
            download_handled = 1;
            if (result == GOOGLE_UPLOAD_SUCCESS && !begin_commit()) {
                result = GOOGLE_UPLOAD_CANCELLED;
                if (!google_backup_cleanup(&selected_backup.backup))
                    snprintf(selected_backup.backup.diagnostic,sizeof(selected_backup.backup.diagnostic),"Temporary download cleanup failed.");
            }
            if (result == GOOGLE_UPLOAD_SUCCESS) {
                selected_ready=1; selected_backup.backup.user=current_user;
            }
            char summary[192];
            snprintf(summary,sizeof(summary),"%s [%.9s] %.63s %.20s: %s",
                result == GOOGLE_UPLOAD_SUCCESS ? "Downloaded" : "Download", selected_backup.backup.title,
                selected_backup.backup.directory,
                selected_backup.backup.utc,
                result == GOOGLE_UPLOAD_SUCCESS ? "validation passed. Temporary ZIP ready." :
                result == GOOGLE_UPLOAD_CANCELLED ? "cancelled; not ready." : "failed validation or transfer; not ready.");
            message(summary);
            if (selected_backup.backup.diagnostic[0]) {
                SDL_LockMutex(lock);
                snprintf(state.preparation_details,sizeof(state.preparation_details),"%s",selected_backup.backup.diagnostic);
                SDL_UnlockMutex(lock);
            }
        } else {
            message("Listing Google Drive backups...");
            if (current_action == GOOGLE_BROWSE) {
                memset(&browser_page,0,sizeof(browser_page)); browser_folder[0]=browser_token[0]=0;
                browser_user=current_user;
                if (!google_upload_folder(&io,browser_folder,0)) {
                    message("No accessible marked backup folder, or folder search failed."); goto done;
                }
            }
            google_backup_page page;
            if (browser_user!=current_user || !google_download_list(&io,browser_folder,browser_token,&page)) {
                message("Could not list backups. Retry Google Drive Backups."); goto done;
            }
            browser_page=page;
            SDL_LockMutex(lock); state.backups=page; state.browsing=1; SDL_UnlockMutex(lock);
            char summary[192]; snprintf(summary,sizeof(summary),"Google Drive Backups: %u valid; %u invalid metadata rejected.",page.count,page.rejected);
            message(summary);
        }
        goto done;
    }
    if (current_action == GOOGLE_UPLOAD) {
        message("Uploading save backup to Google Drive...");
        upload_auth auth = {refresh,access};
        google_upload_io io = {&auth,upload_request,upload_refresh,upload_cancelled,upload_progress,upload_wait};
        int result = google_upload_run(&backup,&io); upload_handled = 1;
        if (result == GOOGLE_UPLOAD_SUCCESS) {
            upload_progress(NULL,backup.size,backup.size);
            message("Backup complete. Drive confirmed ZIP size and checksum.");
        } else if (result == GOOGLE_UPLOAD_UNCERTAIN)
            message("Completion uncertain. Check Drive before retrying; a backup may exist.");
        else if (result == GOOGLE_UPLOAD_CANCELLED) message("Backup cancelled. No completed upload confirmed.");
        else if (!ca_request_failed) message("Backup failed. No verified completion; check connection and Drive.");
        goto done;
    }
    message("Checking Google Drive access...");
    j = request(LIST_URL, NULL, access, &status);
    if (status == 401 && !SDL_AtomicGet(&cancelled)) {
        release(j); j = NULL;
        if (!refresh_access(refresh, access)) goto done;
        j = request(LIST_URL, NULL, access, &status);
    }
    if (status != 200 || !cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(j, "files")) ||
        cJSON_GetObjectItemCaseSensitive(j, "error")) {
        if (!ca_request_failed) message("Drive files.list failed. Check network and Drive API configuration.");
        goto done;
    }
    if (!begin_commit()) goto done;
    message(google_store(current_user, GOOGLE_STORE_WRITE, refresh, TOKEN_CAP) ?
        "Connected. Drive files.list succeeded; refresh token saved for this user." :
        "Drive access succeeded, but credentials could not be saved. Connect again.");
done:
    release(j);
    if (refresh) { wipe(refresh, TOKEN_CAP); free(refresh); }
    if (access) { wipe(access, TOKEN_CAP); free(access); }
    if (current_action == GOOGLE_UPLOAD) {
        if (SDL_AtomicGet(&cancelled) && !upload_handled && !backup.mount_blocked) message("Backup cancelled before upload.");
        if (!google_backup_cleanup(&backup)) {
            SDL_LockMutex(lock);
            size_t n = strlen(state.message);
            snprintf(state.message + n,sizeof(state.message)-n," Cache cleanup failed.");
            SDL_UnlockMutex(lock);
        }
    } else if (SDL_AtomicGet(&cancelled)) {
        if (current_action != GOOGLE_DOWNLOAD && current_action != GOOGLE_RESTORE) message("Google operation cancelled.");
        else if (current_action==GOOGLE_DOWNLOAD && !download_handled) message("Download cancelled before transfer; no temporary backup ready.");
        else if (current_action==GOOGLE_RESTORE && !selected_backup.backup.diagnostic[0]) message("Restore cancelled before writing. Download retained.");
    }
    if (google_store_mount_blocked()) message("Credential save unmount failed. Restart the app before further operations.");
    SDL_LockMutex(lock);
    state.busy = 0;
    state.mount_blocked = backup.mount_blocked || selected_backup.backup.mount_blocked || google_store_mount_blocked();
    state.restore_ready = selected_ready;
    state.restore_backup = selected_backup.backup;
    state.user_code[0] = 0;
    state.verification_url[0] = 0;
    SDL_UnlockMutex(lock);
    return 0;
}

int google_drive_start(int action, uint32_t user)
{
    if (action < GOOGLE_CONNECT || action > GOOGLE_RESTORE) return 0;
    if (!lock) lock = SDL_CreateMutex();
    if (!lock) return 0;
    google_drive_status snapshot;
    google_drive_snapshot(&snapshot);
    if (snapshot.busy || snapshot.mount_blocked) return 0;
    if (selected_ready && action!=GOOGLE_RESTORE) return 0;
    if (action==GOOGLE_RESTORE && (!selected_ready || selected_backup.backup.user!=user)) return 0;
    if (thread) { SDL_WaitThread(thread, NULL); thread = NULL; }
    if (action == GOOGLE_NEXT) {
        if (browser_user != user || !browser_page.next[0]) return 0;
        strcpy(browser_token,browser_page.next);
    }
    if (action == GOOGLE_BROWSE || action == GOOGLE_DOWNLOAD) {
        if (!google_backup_cleanup(&selected_backup.backup)) return 0;
    }
    SDL_AtomicSet(&cancelled, 0);
    if (action==GOOGLE_RESTORE) selected_backup.backup.diagnostic[0]=0;
    current_action = action; current_user = user;
    SDL_LockMutex(lock);
    memset(&state, 0, sizeof(state));
    state.busy = 1;
    state.cancellable = action != GOOGLE_DISCONNECT;
    strcpy(state.message, "Contacting Google...");
    SDL_UnlockMutex(lock);
    thread = SDL_CreateThread(worker, "google_drive", NULL);
    if (!thread) {
        SDL_LockMutex(lock); state.busy = 0; state.restore_ready=selected_ready;
        state.restore_backup=selected_backup.backup; SDL_UnlockMutex(lock);
        message("Could not start Google connection worker."); return 0;
    }
    return 1;
}
int google_drive_download_start(unsigned index, uint32_t user)
{
    google_drive_status s; google_drive_snapshot(&s);
    if (s.busy || s.mount_blocked || selected_ready || !s.browsing || browser_user!=user || index>=browser_page.count) return 0;
    if (!google_backup_cleanup(&selected_backup.backup)) return 0;
    selected_backup=browser_page.entries[index];
    return google_drive_start(GOOGLE_DOWNLOAD,user);
}
int google_drive_upload_start(const char *game, const char *title, const char *directory, uint32_t user)
{
    google_drive_status status; google_drive_snapshot(&status);
    if (status.busy || status.mount_blocked || selected_ready || !game || !title || !directory ||
        strlen(game) >= sizeof(backup.game) || strlen(title) != 9 ||
        !*directory || strlen(directory) >= sizeof(backup.directory) ||
        !strcmp(directory,".") || !strcmp(directory,"..")) return 0;
    for (const char *p = title; *p; p++) if (!isalnum((unsigned char)*p)) return 0;
    for (const char *p = directory; *p; p++) if (*p == '/' || *p == '\\' || (unsigned char)*p < 32) return 0;
    memset(&backup,0,sizeof(backup));
    strcpy(backup.game,game); strcpy(backup.title,title); strcpy(backup.directory,directory); backup.user = user;
    return google_drive_start(GOOGLE_UPLOAD,user);
}
void google_drive_snapshot(google_drive_status *out)
{
    if (!lock) { memset(out, 0, sizeof(*out)); return; }
    SDL_LockMutex(lock); *out = state; SDL_UnlockMutex(lock);
}
int google_drive_discard_download(uint32_t user)
{
    google_drive_status s; google_drive_snapshot(&s);
    if (s.busy || s.mount_blocked || !selected_ready || selected_backup.backup.user!=user) return 0;
    if (!google_backup_cleanup(&selected_backup.backup)) { message("Temporary ZIP cleanup failed; download retained."); return 0; }
    selected_ready=0;
    SDL_LockMutex(lock); state.restore_ready=0; memset(&state.restore_backup,0,sizeof(state.restore_backup)); SDL_UnlockMutex(lock);
    message("Downloaded backup discarded. No restore reported successful."); return 1;
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
    google_backup_cleanup(&selected_backup.backup);
    if (lock) { SDL_DestroyMutex(lock); lock = NULL; }
}
