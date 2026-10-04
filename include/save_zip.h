#ifndef SAVE_ZIP_H
#define SAVE_ZIP_H
#include <stddef.h>
#define SAVE_ZIP_DIAGNOSTIC_SIZE 192
/* Caller-owned storage; no shared last-error state or filesystem paths. */
int zip_directory_diagnostic(const char*, const char*, const char*, int (*)(void*), void*, char*, size_t);
int zip_directory(const char*, const char*, const char*);
int zip_directory_cancel(const char*, const char*, const char*, int (*)(void*), void*);
#endif
