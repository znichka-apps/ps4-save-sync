#ifndef GOOGLE_DRIVE_H
#define GOOGLE_DRIVE_H
#include <stdint.h>
#include "google_upload.h"
/* Only sanitized UI data crosses the worker boundary. */
typedef struct {
    int busy;
    int cancellable;
    int mount_blocked;
    uint64_t completed_bytes, total_bytes;
    char message[192];
    char discovery_details[640];
    char verification_url[256];
    char user_code[64];
} google_drive_status;
enum { GOOGLE_CONNECT, GOOGLE_CHECK, GOOGLE_DISCONNECT, GOOGLE_UPLOAD };
int google_drive_upload_start(const char *game, const char *title, const char *directory, uint32_t user);
void google_drive_ui_upload(const char *game, const char *title, const char *directory, uint32_t user);
int google_drive_start(int action, uint32_t user);
void google_drive_snapshot(google_drive_status *out);
void google_drive_cancel(void);
void google_drive_shutdown(void);
void google_drive_ui_start(int action);
int google_drive_ui_frame(void);
#endif
