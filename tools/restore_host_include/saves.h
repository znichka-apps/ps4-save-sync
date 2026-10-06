#ifndef RESTORE_HOST_SAVES_H
#define RESTORE_HOST_SAVES_H
#include <stddef.h>
#include <stdint.h>
#define SAVES_PATH_HDD "build/host/restore-users/%08x/"
#define SAVES_DB_PATH "build/host/restore-users/%08x.db"
#define APOLLO_SANDBOX_PATH "build/host/restore-mount/%s/"
#define TROPHY_PATH_HDD "build/host/restore-trophies/%08x/"
#define SAVE_FLAG_PS4 16
#define SAVE_FLAG_TROPHY 128
#define SAVE_FLAG_LOCKED 256
#define SAVE_FLAG_HDD 1024
#define FILE_TYPE_PS4 1
#define LOG(...) ((void)0)
typedef struct { char *name,*title_id,*path,*dir_name; uint32_t blocks; uint16_t flags,type; } save_entry_t;
size_t strlcpy(char*,const char*,size_t);
void *open_sqlite_db(const char*);
int save_sqlite_db(void*,const char*);
int orbis_SaveTargetAbsent(const save_entry_t*,uint32_t);
int orbis_SaveDelete(const save_entry_t*);
int orbis_SaveDeleteStatus(const save_entry_t*, int*);
int orbis_SaveMountEmpty(const save_entry_t*,uint32_t,char*);
int orbis_ImportStagedSave(const char*,const char*,const char*,uint32_t,int (*)(void*),void*,int*,uint32_t);
int orbis_SaveUmount(const char*);
int orbis_UpdateSaveParams(const save_entry_t*,const char*,const char*,const char*,uint32_t);
#endif
