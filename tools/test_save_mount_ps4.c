#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sqlite3.h>
#include "saves.h"
#include "settings.h"

app_config_t apollo_config={.user_id=42,.account_id=123};
extern int sdk_stub_calls, sdk_native_calls, sdk_operation_failure;
static int absent_result=1, absent_calls, create_calls, mount_calls, mount_failure;
static const char *mount_dir="build/host/restore-mount/SAVE/";

size_t strlcpy(char *dst,const char *src,size_t size)
{
    size_t len=strlen(src);
    if (size) { size_t n=len<size-1?len:size-1; memcpy(dst,src,n); dst[n]=0; }
    return len;
}
int mkdirs(const char *path)
{
    if (!strcmp(path,mount_dir)) return mkdir(path,0700);
    assert(strstr(path,"sdimg_SAVE"));
    return 0;
}
int file_exists(const char *path) { (void)path; assert(0 && "empty mount must not use file_exists"); return -1; }
int orbis_SaveTargetAbsent(const save_entry_t *save,uint32_t user)
{
    assert(user==42 && !strcmp(save->title_id,"CUSA12345") && !strcmp(save->dir_name,"SAVE"));
    absent_calls++;
    return absent_result;
}
int createSaveEmpty(const char *volume,const char *key,int blocks)
{
    assert(strstr(volume,"sdimg_SAVE") && strstr(key,"SAVE.bin") && blocks==96);
    create_calls++;
    return 0;
}
int createSave(const char *volume,const char *key,int blocks)
{ (void)volume; (void)key; (void)blocks; assert(0 && "empty mount must use createSaveEmpty"); return -1; }
int mountSave(const char *volume,const char *key,const char *path)
{
    assert(strstr(volume,"sdimg_SAVE") && strstr(key,"SAVE.bin") && !strcmp(path,mount_dir));
    mount_calls++;
    if (mount_failure) { errno=ENOENT; return -1234; }
    return 0;
}
void *open_sqlite_db(const char *path)
{
    assert(!strcmp(path,"build/host/restore-users/0000002a.db"));
    sqlite3 *db=NULL;
    assert(sqlite3_open(":memory:",&db)==SQLITE_OK);
    assert(sqlite3_exec(db,"CREATE TABLE savedata(title_id,dir_name,main_title,sub_title,detail,tmp_dir_name,is_broken,user_param,blocks,free_blocks,size_kib,mtime,fake_broken,account_id,user_id,faked_owner,cloud_icon_url,cloud_revision,game_title_id)",NULL,NULL,NULL)==SQLITE_OK);
    return db;
}
int save_sqlite_db(void *db,const char *path) { (void)db; (void)path; return 1; }

int main(void)
{
    save_entry_t save={.title_id="CUSA12345",.dir_name="SAVE",.blocks=96};
    char mounted[32]={0};
    mkdir("build/host/restore-mount",0700);
    rmdir(mount_dir);

    assert(orbis_SaveMountEmpty(&save,42,mounted)==1);
    assert(!strcmp(mounted,"SAVE") && absent_calls==1 && create_calls==1 && mount_calls==1);
    assert(sdk_native_calls>0 && sdk_stub_calls==0);

    assert(orbis_SaveMountEmpty(&save,42,mounted)==0);
    assert(absent_calls==1 && create_calls==1 && mount_calls==1 && sdk_stub_calls==0);

    assert(rmdir(mount_dir)==0);
    absent_result=0;
    assert(orbis_SaveMountEmpty(&save,42,mounted)==0);
    assert(absent_calls==2 && create_calls==1 && mount_calls==1 && sdk_stub_calls==0);
    assert(rmdir(mount_dir)==0);
    sdk_operation_failure=0; absent_result=1; mount_failure=1;
    int uncertain=0;
    save_mount_diagnostic_t diagnostic={0};
    struct stat st;
    errno=0;
    assert(!orbis_SaveMountEmptyCheckedDiagnostic(&save,42,mounted,&uncertain,&diagnostic));
    assert(uncertain && errno==ENOENT && mount_calls==2);
    assert(!strcmp(diagnostic.call,"mountSave") && diagnostic.native_result==-1234 && diagnostic.error==ENOENT);
    assert(!stat(mount_dir,&st) && S_ISDIR(st.st_mode));
    assert(!rmdir(mount_dir));

    sdk_operation_failure=1;
    errno=0;
    assert(orbis_SaveMountEmpty(&save,42,mounted)==0 && errno==ENOSYS);
    assert(absent_calls==3 && create_calls==2 && mount_calls==2 && sdk_stub_calls==0);
    puts("PS4 empty-save mount uses native lstat; present and unknown paths are refused.");
    return 0;
}
