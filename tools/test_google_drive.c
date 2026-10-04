/* Deterministic host tests: real JSON parser, mocked Google HTTP and save mount. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL2/SDL.h>
#include <curl/curl.h>
#include "google_store.h"
#include "google_drive.h"

typedef struct {
    const char *url, *form, *ca;
    size_t (*receive)(void*, size_t, size_t, void*);
    void *buffer;
    long peer, host, redirect, protocols, verbose;
    long status;
} mock_handle;
typedef struct { long status; const char *json; CURLcode result; double delay; } reply;
static reply replies[20];
static int reply_count, reply_index, writes, clears, fail_store, fail_write, list_calls, cancel_on_list;
static double fake_time, poll_times[10];
static int polls, cancel_on_delay;
static uint32_t last_user;
static char stored[8192];

static CURL *mock_init(void) { return (CURL*)calloc(1, sizeof(mock_handle)); }
static CURLcode mock_setopt(CURL *curl, CURLoption key, ...)
{
    mock_handle *h = (void*)curl;
    va_list ap;
    va_start(ap, key);
    switch (key) {
    case CURLOPT_WRITEFUNCTION: h->receive = va_arg(ap, size_t (*)(void*, size_t, size_t, void*)); break;
    case CURLOPT_XFERINFOFUNCTION: (void)va_arg(ap, int (*)(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t)); break;
    default:
        if (key < CURLOPTTYPE_OBJECTPOINT) {
            long v = va_arg(ap, long);
            if (key == CURLOPT_SSL_VERIFYPEER) h->peer = v;
            if (key == CURLOPT_SSL_VERIFYHOST) h->host = v;
            if (key == CURLOPT_FOLLOWLOCATION) h->redirect = v;
            if (key == CURLOPT_PROTOCOLS) h->protocols = v;
            if (key == CURLOPT_VERBOSE) h->verbose = v;
        } else {
            void *v = va_arg(ap, void*);
            if (key == CURLOPT_URL) h->url = v;
            if (key == CURLOPT_CAINFO) h->ca = v;
            if (key == CURLOPT_POSTFIELDS) h->form = v;
            if (key == CURLOPT_WRITEDATA) h->buffer = v;
        }
    }
    va_end(ap);
    return CURLE_OK;
}
static CURLcode mock_perform(CURL *curl)
{
    mock_handle *h = (void*)curl;
    assert(h->peer == 1 && h->host == 2 && h->redirect == 0 && h->verbose == 0);
    assert(h->protocols == CURLPROTO_HTTPS && h->ca && strstr(h->ca, "assets/google/cacert.pem"));
    assert(h->url && !strncmp(h->url, "https://", 8));
    if (strstr(h->url, "/device/code")) {
        assert(h->form && strstr(h->form, "drive.file"));
        assert(!strstr(h->form, "client_secret"));
    }
    if (h->form && strstr(h->form, "device_code=")) poll_times[polls++] = fake_time;
    if (strstr(h->url, "/drive/v3/files")) {
        list_calls++;
        if (cancel_on_list) google_drive_cancel();
    }
    assert(reply_index < reply_count);
    reply *r = &replies[reply_index++];
    fake_time += r->delay;
    h->status = r->status;
    if (r->result != CURLE_OK) return r->result;
    size_t n = strlen(r->json);
    return h->receive((void*)r->json, 1, n, h->buffer) == n ? CURLE_OK : CURLE_WRITE_ERROR;
}
static CURLcode mock_getinfo(CURL *curl, CURLINFO info, long *out)
{
    assert(info == CURLINFO_RESPONSE_CODE);
    *out = ((mock_handle*)curl)->status;
    return CURLE_OK;
}
static char *mock_escape(CURL *curl, const char *s, int n) { (void)curl; return curl_easy_escape(NULL, s, n); }
static void mock_cleanup(CURL *curl) { free(curl); }
static int mock_clock(clockid_t id, struct timespec *t)
{
    assert(id == CLOCK_MONOTONIC);
    t->tv_sec = (time_t)fake_time;
    t->tv_nsec = (long)((fake_time - t->tv_sec) * 1000000000);
    return 0;
}
static void mock_delay(Uint32 ms);
#undef curl_easy_setopt
#undef curl_easy_getinfo
#define curl_easy_init mock_init
#define curl_easy_setopt mock_setopt
#define curl_easy_perform mock_perform
#define curl_easy_getinfo mock_getinfo
#define curl_easy_escape mock_escape
#define curl_easy_cleanup mock_cleanup
#define clock_gettime mock_clock
#define SDL_Delay mock_delay
#include "../source/google_drive.c"

static void mock_delay(Uint32 ms)
{
    google_drive_status view;
    google_drive_snapshot(&view);
    if (view.user_code[0]) {
        assert(!strcmp(view.user_code, "WWWWWWWWWWWWWWW"));
        assert(!strcmp(view.verification_url, "https://www.google.com/device"));
    }
    fake_time += ms / 1000.0;
    if (cancel_on_delay) google_drive_cancel();
}
int google_store(uint32_t user, int operation, char *token, size_t cap)
{
    last_user = user;
    if (fail_store || (fail_write && operation == GOOGLE_STORE_WRITE)) return 0;
    if (operation == GOOGLE_STORE_READ) snprintf(token, cap, "%s", stored);
    if (operation == GOOGLE_STORE_WRITE) { snprintf(stored, sizeof(stored), "%s", token); writes++; }
    if (operation == GOOGLE_STORE_CLEAR) { stored[0] = 0; clears++; }
    return 1;
}
static void reset(int action)
{
    memset(&state, 0, sizeof(state));
    state.busy = 1;
    state.cancellable = action != GOOGLE_DISCONNECT;
    current_action = action; current_user = 42;
    reply_count = reply_index = writes = clears = polls = list_calls = fail_store = fail_write = cancel_on_delay = cancel_on_list = 0;
    fake_time = 0; stored[0] = 0;
    SDL_AtomicSet(&cancelled, 0);
}
static void add(long status, const char *json) { replies[reply_count++] = (reply){status, json, CURLE_OK, 0}; }
static void device(void)
{
    add(200, "{\"device_authorization_endpoint\":\"https://oauth2.googleapis.com/device/code\"}");
    add(200, "{\"device_code\":\"synthetic-device\",\"verification_url\":\"https://www.google.com/device\",\"user_code\":\"WWWWWWWWWWWWWWW\",\"interval\":1,\"expires_in\":60}");
}
static const char *tokens = "{\"access_token\":\"synthetic-access\",\"refresh_token\":\"synthetic-refresh\",\"token_type\":\"Bearer\",\"expires_in\":3600,\"scope\":\"https://www.googleapis.com/auth/drive.file\"}";
static void run(void) { worker(NULL); assert(!state.busy && !state.user_code[0]); }

int main(void)
{
    assert(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    lock = SDL_CreateMutex(); assert(lock);
    reset(GOOGLE_CONNECT); device();
    add(400, "{\"error\":\"authorization_pending\"}");
    add(400, "{\"error\":\"slow_down\"}");
    add(200, tokens); add(200, "{\"files\":[]}"); run();
    assert(writes == 1 && last_user == 42 && list_calls == 1 && polls == 3);
    assert(poll_times[1] - poll_times[0] >= 1 && poll_times[2] - poll_times[1] >= 6);
    assert(strstr(state.message, "succeeded"));

    const char *errors[] = {"access_denied", "expired_token"};
    for (int i = 0; i < 2; i++) {
        char body[80]; snprintf(body, sizeof(body), "{\"error\":\"%s\"}", errors[i]);
        reset(GOOGLE_CONNECT); device(); add(400, body); run(); assert(!writes && !list_calls);
    }
    reset(GOOGLE_CONNECT); device(); cancel_on_delay = 1; run();
    assert(reply_index == 2 && !writes && strstr(state.message, "cancelled"));
    reset(GOOGLE_CONNECT); device(); add(200, "{}"); replies[2].result = CURLE_SSL_CACERT; run(); assert(!writes);
    reset(GOOGLE_CONNECT); device(); add(200, tokens); replies[2].delay = 61; run(); assert(!writes);
    reset(GOOGLE_CONNECT); device(); add(200, "{bad json"); run(); assert(!writes);
    reset(GOOGLE_CONNECT); device(); add(200, "{\"access_token\":\"x\"}"); run(); assert(!writes);
    reset(GOOGLE_CONNECT); device();
    add(200, "{\"access_token\":\"synthetic-access\",\"refresh_token\":\"synthetic-refresh\",\"token_type\":\"Bearer\",\"expires_in\":3600,\"scope\":\"https://www.googleapis.com/auth/drive\"}");
    run(); assert(!writes && !list_calls);
    reset(GOOGLE_CONNECT); device(); add(200, tokens); add(403, "{\"error\":{}}"); run(); assert(!writes);
    reset(GOOGLE_CONNECT); device(); add(200, tokens); add(200, "{\"files\":[]}"); fail_store = 1; run();
    assert(strstr(state.message, "could not be saved"));

    reset(GOOGLE_CHECK); strcpy(stored, "synthetic-old-refresh");
    add(200, tokens); add(401, "{\"error\":{}}"); add(200, tokens); add(200, "{\"files\":[]}"); run();
    assert(writes == 2 && list_calls == 2 && !strcmp(stored, "synthetic-refresh"));
    reset(GOOGLE_CHECK); strcpy(stored, "synthetic-old-refresh");
    add(200, tokens); add(403, "{\"error\":{}}"); run();
    assert(writes == 1 && !strcmp(stored, "synthetic-refresh") && strstr(state.message, "failed"));
    reset(GOOGLE_CHECK); strcpy(stored, "synthetic-old-refresh");
    add(200, tokens); fail_write = 1; run();
    assert(!list_calls && strstr(state.message, "Could not save"));
    reset(GOOGLE_CHECK); strcpy(stored, "synthetic-old-refresh");
    add(200, tokens); add(200, "{\"files\":[]}"); cancel_on_list = 1; run();
    assert(writes == 1 && !strcmp(stored, "synthetic-refresh") && strstr(state.message, "cancelled"));
    reset(GOOGLE_CONNECT); device();
    add(200, "{\"access_token\":\"synthetic-access\",\"refresh_token\":\"synthetic-refresh\",\"token_type\":\"Bearer\",\"expires_in\":3600,\"scope\":\"https://www.googleapis.com/auth/drive.file\",\"error\":\"invalid_grant\"}");
    run(); assert(!writes && !list_calls);
    reset(GOOGLE_CHECK); run(); assert(!reply_index && strstr(state.message, "not connected"));
    reset(GOOGLE_CHECK); strcpy(stored, "synthetic-old-refresh"); add(400, "{\"error\":\"invalid_grant\"}"); run();
    assert(!writes && !list_calls && strstr(state.message, "revoked"));
    reset(GOOGLE_DISCONNECT); strcpy(stored, "synthetic-refresh"); run();
    assert(clears == 1 && !stored[0] && !reply_index && last_user == 42);

    /* Cancellation cannot misreport a credential update already committing. */
    reset(GOOGLE_CONNECT); assert(begin_commit()); google_drive_cancel();
    assert(!SDL_AtomicGet(&cancelled));

    SDL_DestroyMutex(lock); lock = NULL;
    curl_global_cleanup();
    puts("Google auth host tests passed (mock HTTP/save-data; no real tokens).");
    return 0;
}
