#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "save_unmount.h"

static int unmount_result, unmount_errno, unmount_calls;
int umountSave(const char *path, int handle, _Bool ignore_errors)
{
    assert(!strcmp(path,"build/host/unmount-check/SAVE"));
    assert(handle==0 && !ignore_errors);
    unmount_calls++;
    errno=unmount_errno;
    return unmount_result;
}

int main(void)
{
    const char *parent="build/host/unmount-check";
    const char *mount="build/host/unmount-check/SAVE";
    assert(!mkdir(parent,0700) || errno==EEXIST);
    assert(!mkdir(mount,0700) || errno==EEXIST);
    struct stat before, after;
    assert(!lstat(mount,&before));

    int sdk_status=0;
    unmount_result=-1234; unmount_errno=ENOENT;
    assert(!save_unmount_directory(mount,&sdk_status));
    assert(sdk_status==-1234 && errno==ENOENT && unmount_calls==1);
    assert(!lstat(mount,&after) && before.st_ino==after.st_ino);

    unmount_result=0; unmount_errno=0;
    assert(save_unmount_directory(mount,&sdk_status));
    assert(sdk_status==0 && errno==0 && unmount_calls==2);
    assert(lstat(mount,&after) && errno==ENOENT);
    assert(!rmdir(parent));
    puts("Save unmount failure retains mount directory and SDK error.");
    return 0;
}
