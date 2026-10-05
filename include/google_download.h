#ifndef GOOGLE_DOWNLOAD_H
#define GOOGLE_DOWNLOAD_H
#include "google_upload.h"
#define GOOGLE_BACKUP_PAGE 10
typedef struct { char id[129]; google_backup backup; } google_remote_backup;
typedef struct {
    google_remote_backup entries[GOOGLE_BACKUP_PAGE];
    unsigned count, rejected;
    char next[512];
} google_backup_page;
int google_download_metadata(cJSON *file, google_remote_backup *out);
int google_download_list(const google_upload_io*, const char*, const char*, google_backup_page*);
int google_download_zip(const google_backup*, int (*)(void*), void*);
/* Validate the already opened archive; caller retains fd ownership. */
int google_download_zip_fd(const google_backup*, int, int (*)(void*), void*, int*);
int google_download_run(google_remote_backup*, const google_upload_io*);
int google_download_recheck(const google_remote_backup*, const google_upload_io*);
#endif
