#ifndef SAVE_UNMOUNT_H
#define SAVE_UNMOUNT_H

/* One SDK unmount attempt. On failure the mount directory is left in place. */
int save_unmount_directory(const char *mount_dir, int *sdk_status);

#endif
