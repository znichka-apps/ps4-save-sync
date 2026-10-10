#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <orbis/SaveData.h>
#include "google_store.h"
static int mounted, unmounts, fail_mount, fail_unmount, fail_sync, fail_rename;
static char mapped[2][512];
static const char *map_path(const char *path, int index)
{
    const char *prefix = "/mnt/sandbox/PSSY00001_000/";
    assert(!strncmp(path, prefix, strlen(prefix)));
    snprintf(mapped[index], sizeof(mapped[index]), "build/host/store/%s", path + strlen(prefix));
    return mapped[index];
}
static FILE *mapped_open(const char *path, const char *mode) { return fopen(map_path(path, 0), mode); }
static int mapped_unlink(const char *path) { return unlink(map_path(path, 0)); }
static int mapped_rename(const char *from, const char *to)
{
    if (fail_rename) { errno = EIO; return -1; }
    return rename(map_path(from, 0), map_path(to, 1));
}
static int mapped_sync(int fd) { return fail_sync ? -1 : fsync(fd); }
int32_t sceSaveDataMount2(const OrbisSaveDataMount2 *m, OrbisSaveDataMountResult *r)
{
    assert(!mounted && !strcmp(m->dirName->data, "GoogleAuth"));
    assert(m->mountMode == (ORBIS_SAVE_DATA_MOUNT_MODE_CREATE2 | ORBIS_SAVE_DATA_MOUNT_MODE_RDWR));
    assert(m->blocks == ORBIS_SAVE_DATA_BLOCKS_MIN2 && !m->unk1 && !m->unk2);
    if (fail_mount) return -1;
    snprintf(r->mountPathName, sizeof(r->mountPathName), "/u%d", m->userId);
    char dir[128]; snprintf(dir, sizeof(dir), "build/host/store/u%d", m->userId);
    assert(mkdir(dir, 0700) == 0 || errno == EEXIST);
    mounted = 1;
    return 0;
}
int32_t sceSaveDataUmount(OrbisSaveDataMountPoint *m)
{
    assert(mounted && m->data[0] == '/'); mounted = 0; unmounts++;
    return fail_unmount ? -1 : 0;
}
#define fopen mapped_open
#define unlink mapped_unlink
#define rename mapped_rename
#define fsync mapped_sync
#include "../source/google_store.c"

int main(void)
{
    char token[8192];
    assert(google_store(42, GOOGLE_STORE_CLEAR, token, sizeof(token)));
    assert(google_store(43, GOOGLE_STORE_CLEAR, token, sizeof(token)));
    assert(google_store(42, GOOGLE_STORE_READ, token, sizeof(token)) && !token[0]);
    strcpy(token, "synthetic-original");
    assert(google_store(42, GOOGLE_STORE_WRITE, token, sizeof(token)));
    strcpy(token, "synthetic-other-user");
    assert(google_store(43, GOOGLE_STORE_WRITE, token, sizeof(token)));
    assert(google_store(42, GOOGLE_STORE_READ, token, sizeof(token)) && !strcmp(token, "synthetic-original"));
    fail_sync = 1; strcpy(token, "synthetic-replacement");
    assert(!google_store(42, GOOGLE_STORE_WRITE, token, sizeof(token))); fail_sync = 0;
    assert(google_store(42, GOOGLE_STORE_READ, token, sizeof(token)) && !strcmp(token, "synthetic-original"));
    fail_rename = 1; strcpy(token, "synthetic-replacement");
    assert(!google_store(42, GOOGLE_STORE_WRITE, token, sizeof(token))); fail_rename = 0;
    assert(google_store(42, GOOGLE_STORE_READ, token, sizeof(token)) && !strcmp(token, "synthetic-original"));
    assert(access("build/host/store/u42/refresh_token.tmp", F_OK) != 0);
    fail_mount = 1; int before = unmounts;
    assert(!google_store(42, GOOGLE_STORE_READ, token, sizeof(token))); fail_mount = 0;
    assert(before == unmounts);
    fail_unmount = 1;
    assert(!google_store(42, GOOGLE_STORE_READ, token, sizeof(token)) && !token[0]); fail_unmount = 0;
    assert(google_store_mount_blocked()); before=unmounts;
    assert(!google_store(43,GOOGLE_STORE_READ,token,sizeof(token)) && !token[0] && before==unmounts);
    credential_mount_blocked=0; /* Simulate a fresh process after the required restart. */
    FILE *fp = mapped_open("/mnt/sandbox/PSSY00001_000/u42/refresh_token", "wb");
    assert(fp); assert(fwrite("bad\0data", 1, 8, fp) == 8); fclose(fp);
    assert(!google_store(42, GOOGLE_STORE_READ, token, sizeof(token)) && !token[0]);
    fp = mapped_open("/mnt/sandbox/PSSY00001_000/u42/refresh_token", "wb");
    assert(fp); assert(fwrite("bad\ndata", 1, 8, fp) == 8); fclose(fp);
    assert(!google_store(42, GOOGLE_STORE_READ, token, sizeof(token)) && !token[0]);
    assert(!google_store(42, 99, token, sizeof(token)));
    assert(google_store(42, GOOGLE_STORE_CLEAR, token, sizeof(token)));
    assert(google_store(43, GOOGLE_STORE_READ, token, sizeof(token)) && !strcmp(token, "synthetic-other-user"));
    assert(google_store(43, GOOGLE_STORE_CLEAR, token, sizeof(token)));
    assert(!mounted);
    puts("Google credential host tests passed (mock PS4 mounts, real atomic file operations).");
    return 0;
}
