#ifndef RESTORE_FS_H
#define RESTORE_FS_H
#include <sys/stat.h>
/* Bypass CI musl's ENOSYS stubs, never weaken no-follow semantics. */
int restore_fs_init(int *native_error);
int restore_fs_lstat(const char *, struct stat *);
int restore_fs_openat(int, const char *, int, mode_t);
int restore_fs_mkdirat(int, const char *, mode_t);
#endif
