#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "common.h"

static int fail_copy_buffer;
void *__real_malloc(size_t);
void *__wrap_malloc(size_t size)
{
    if (fail_copy_buffer && size==0x20000) return NULL;
    return __real_malloc(size);
}
int sceSystemServiceParamGetInt(int key, int *value)
{
    (void)key; *value=1; return 0;
}
static int open_fds(void)
{
    DIR *d=opendir("/proc/self/fd"); assert(d);
    int count=0;
    while (readdir(d)) count++;
    assert(!closedir(d));
    return count;
}
int main(void)
{
    const char *root="build/host/copy-cleanup";
    assert(!mkdir(root,0700) || access(root,F_OK)==0);
    const char *source="build/host/copy-cleanup/source";
    const char *sub="build/host/copy-cleanup/source/sub";
    assert(!mkdir(source,0700) || access(source,F_OK)==0);
    assert(!mkdir(sub,0700) || access(sub,F_OK)==0);
    const char *input="build/host/copy-cleanup/source/sub/data";
    FILE *f=fopen(input,"wb"); assert(f);
    assert(fputc('x',f)=='x' && !fclose(f));
    int before=open_fds();
    fail_copy_buffer=1;
    assert(copy_file(input,"build/host/copy-cleanup/target")==FAILED);
    fail_copy_buffer=0;
    assert(open_fds()==before); /* Destination save mount must have no open file. */

    const char *blocker="build/host/copy-cleanup/blocker";
    f=fopen(blocker,"wb"); assert(f); assert(!fclose(f));
    assert(copy_directory("build/host/copy-cleanup/source/",
        "build/host/copy-cleanup/source/","build/host/copy-cleanup/blocker/")==FAILED);
    assert(errno==ENOTDIR); /* Failure survives copy_file and recursive DIR cleanup. */
    assert(open_fds()==before); /* Recursive copy failure closes both DIR streams. */
    assert(!unlink("build/host/copy-cleanup/target"));
    assert(!unlink(blocker)); assert(!unlink(input));
    assert(!rmdir(sub)); assert(!rmdir(source)); assert(!rmdir(root));
    puts("Save copy failure cleanup tests passed.");
    return 0;
}
