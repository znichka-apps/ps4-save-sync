#ifndef GOOGLE_RESTORE_H
#define GOOGLE_RESTORE_H
#include "google_download.h"
/* Worker-only adapter. Absent returns 1 only for confirmed absence, 0 for
   existing, -1 for unknown. No automatic target deletion is permitted. */
typedef struct {
    void *data;
    int (*cancelled)(void*);
    int (*absent)(void*, const google_backup*);
    int (*import_staged)(void*, const google_backup*, const char*);
    int (*finish)(void*); /* Atomic cancellation/success boundary after unmount. */
} google_restore_io;
int google_restore_run(google_backup*, const google_restore_io*);
int google_restore_local(google_backup*, int (*)(void*), int (*)(void*), void*);
#endif
