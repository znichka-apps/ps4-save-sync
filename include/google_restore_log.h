#ifndef GOOGLE_RESTORE_LOG_H
#define GOOGLE_RESTORE_LOG_H
#include <stddef.h>

/* Each record is appended and synced before returning. Values are captured by
   the caller immediately after the operation, before cleanup can change errno. */
void google_restore_log(const char *stage, int op, int error, int zip_error,
    int native, const char *call, int failed);
void google_restore_last_failure(char *out, size_t capacity);
void google_restore_clear_failure(void);
#endif
