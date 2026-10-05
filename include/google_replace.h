#ifndef GOOGLE_REPLACE_H
#define GOOGLE_REPLACE_H
#include "google_restore.h"

#ifndef GOOGLE_REPLACE_ROOT
#define GOOGLE_REPLACE_ROOT "/data/ps4-save-sync/replace/"
#endif

typedef struct {
    void *data;
    int (*cancelled)(void *);
    int (*present)(void *, const google_backup *); /* 1 present, 0 absent, -1 unknown */
    int (*stage_backup)(void *, google_backup *);  /* Must unmount before returning. */
    int (*upload_backup)(void *, const google_backup *); /* GOOGLE_UPLOAD_* */
    int (*commit)(void *); /* Disable cancellation before any target mutation. */
    int (*delete_target)(void *, const google_backup *); /* Verified absent on return. */
    int (*import_archive)(void *, google_backup *, int recovery); /* Checked unmount on success. */
} google_replace_io;

enum {
    GOOGLE_REPLACE_FAILED,
    GOOGLE_REPLACE_SUCCESS,
    GOOGLE_REPLACE_CANCELLED,
    GOOGLE_REPLACE_ROLLED_BACK,
    GOOGLE_REPLACE_NEEDS_RECOVERY
};

/* Pending returns 1 for a recoverable journal, 0 for none, -1 for unsafe/ambiguous state. */
int google_replace_pending(uint32_t user, google_backup *source);
int google_replace_start(google_backup *source, const google_replace_io *io);
int google_replace_recover(uint32_t user, const google_replace_io *io);
int google_replace_local(google_backup *, const google_upload_io *, int (*)(void *), int (*)(void *), void *);
int google_replace_recover_local(uint32_t user, int *mount_blocked);
#endif
