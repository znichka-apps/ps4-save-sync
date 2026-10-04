#ifndef GOOGLE_UPLOAD_H
#define GOOGLE_UPLOAD_H
#include <stdint.h>
#include <stdio.h>
#include <curl/curl.h>
#include "cJSON.h"
#ifndef GOOGLE_BACKUP_CACHE
#define GOOGLE_BACKUP_CACHE "/data/ps4-save-sync/cache/"
#endif
#define GOOGLE_UPLOAD_CHUNK (256U * 1024U)
typedef struct {
    char game[1024], title[32], directory[64];
    char temp_dir[256], archive[288], utc[32], md5[33];
    char diagnostic[192]; /* Worker-owned, copied to UI under its existing mutex. */
    uint32_t user;
    uint64_t size;
    int mount_blocked;
} google_backup;
typedef struct {
    const char *method, *url, *json, *range;
    FILE *file;
    FILE *download; /* GET sink, capped at total bytes. */
    uint64_t offset, length, total;
} google_upload_request;
typedef struct {
    CURLcode transport;
    long status;
    char location[2048], range[96];
    int invalid_headers;
    uint64_t downloaded;
    cJSON *json;
} google_upload_response;
typedef struct {
    void *data;
    void (*request)(void*, const google_upload_request*, google_upload_response*);
    int (*refresh)(void*);
    int (*cancelled)(void*);
    void (*progress)(void*, uint64_t, uint64_t);
    int (*wait)(void*, unsigned);
} google_upload_io;
enum { GOOGLE_UPLOAD_FAILED, GOOGLE_UPLOAD_SUCCESS, GOOGLE_UPLOAD_CANCELLED, GOOGLE_UPLOAD_UNCERTAIN };
int google_upload_session_url(const char *url);
int google_upload_folder(const google_upload_io *io, char *id, int allow_create);
int google_upload_run(const google_backup *backup, const google_upload_io *io);
int google_backup_stage(google_backup *backup, int (*cancelled)(void*), void *data);
int google_backup_hash(google_backup *backup, int (*cancelled)(void*), void *data);
int google_backup_cleanup(google_backup *backup);
#endif
