#include <errno.h>
#include <unistd.h>
#include "sd.h"
#include "save_unmount.h"

int save_unmount_directory(const char *mount_dir, int *sdk_status)
{
    errno = 0;
    int status = umountSave(mount_dir, 0, 0);
    int unmount_error = errno;
    if (sdk_status) *sdk_status = status;
    if (status == 0) (void)rmdir(mount_dir);
    errno = unmount_error;
    return status == 0;
}
