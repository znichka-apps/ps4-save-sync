#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <zip.h>
#include <orbis/SaveData.h>
#include "saves.h"
#include "settings.h"
#include "google_upload.h"
#include "save_zip.h"
app_config_t apollo_config = {.user_id = 42};
static int mounted, mount_calls, unmount_calls, fail_mount, fail_unmount, cancelled_, cancel_during_zip;
static int fail_cache, fail_temp, fail_time;
static google_backup backup;
#ifdef __PS4__
static int cancel(void *p);
static google_backup ordinary;
static int phase_calls, interleave, interleaved;
void google_replace_phase(uint32_t user,const char *phase,const char *step)
{
    assert(user==42 && phase && step);
    phase_calls++;
    if (interleave && !interleaved) {
        interleaved=1;
        assert(!ordinary.replace_trace);
        assert(google_backup_stage(&ordinary,cancel,NULL));
        assert(phase_calls==1); /* Concurrent ordinary save must not inherit Replace tracing. */
    }
}
#endif
int mkdirs(const char *path) {
    if (fail_cache) { errno=EACCES; return -1; }
    return mkdir(path,0700)==0 || errno==EEXIST ? 0 : -1;
}
char *__real_mkdtemp(char*);
char *__wrap_mkdtemp(char *p) { if (fail_temp) { errno=ENOSPC; return NULL; } return __real_mkdtemp(p); }
time_t __real_time(time_t*);
time_t __wrap_time(time_t *t) { if (fail_time) { errno=EIO; return -1; } return __real_time(t); }
int orbis_SaveMount(const save_entry_t *save,uint32_t mode,char *path)
{
    assert(!mounted && mode==ORBIS_SAVE_DATA_MOUNT_MODE_RDONLY && !save->path);
    assert(!strcmp(save->title_id,"CUSA12345") && !strcmp(save->dir_name,"SAVE_DATA"));
    assert(save->flags==(SAVE_FLAG_PS4|SAVE_FLAG_HDD) && save->type==FILE_TYPE_PS4);
    mount_calls++;
    if (fail_mount) { errno=EACCES; return 0; }
    mounted=1; strcpy(path,"SAVE_DATA"); return 1;
}
int orbis_SaveUmount(const char *path)
{
    assert(mounted && !strcmp(path,"SAVE_DATA")); unmount_calls++;
    if (fail_unmount) { errno=EIO; return 0; }
    mounted=0; return 1;
}
static int cancel(void *p)
{
    (void)p;
    if (cancel_during_zip && mounted) cancelled_=1;
    return cancelled_;
}
static void reset(void)
{
    memset(&backup,0,sizeof(backup)); strcpy(backup.game,"Owned Game"); strcpy(backup.title,"CUSA12345");
    strcpy(backup.directory,"SAVE_DATA"); backup.user=42;
    mounted=mount_calls=unmount_calls=fail_mount=fail_unmount=cancelled_=cancel_during_zip=0;
    fail_cache=fail_temp=fail_time=0;
}
static void file(const char *path,const char *data)
{
    FILE *fp=fopen(path,"wb"); assert(fp); assert(fwrite(data,1,strlen(data),fp)==strlen(data)); assert(!fclose(fp));
}
static void remove_cache(void) { assert(!rmdir(GOOGLE_BACKUP_CACHE)); }
int main(void)
{
    mkdir("build/host/mount",0700); mkdir("build/host/mount/SAVE_DATA",0700);
    mkdir("build/host/mount/SAVE_DATA/sce_sys",0700); mkdir("build/host/mount/SAVE_DATA/nested",0700);
    file("build/host/mount/SAVE_DATA/sce_sys/param.sfo","original-param");
    file("build/host/mount/SAVE_DATA/nested/data.bin","original-data");
    reset(); assert(google_backup_stage(&backup,cancel,NULL));
    assert(mount_calls==1 && unmount_calls==1 && !mounted && backup.size && strlen(backup.md5)==32);
    zip_t *zip=zip_open(backup.archive,ZIP_RDONLY|ZIP_CHECKCONS,NULL); assert(zip);
    assert(zip_name_locate(zip,"SAVE_DATA/sce_sys/param.sfo",0)>=0);
    assert(zip_name_locate(zip,"SAVE_DATA/nested/data.bin",0)>=0);
    assert(zip_get_num_entries(zip,0)==5); /* save root, two dirs, two files */
    zip_file_t *entry=zip_fopen(zip,"SAVE_DATA/nested/data.bin",0); assert(entry);
    char text[32]={0}; assert(zip_fread(entry,text,sizeof(text))==13 && !strcmp(text,"original-data"));
    assert(!zip_fclose(entry) && !zip_close(zip));
    google_backup second=backup;
    second.temp_dir[0]=second.archive[0]=0;
    assert(google_backup_stage(&second,cancel,NULL) && strcmp(second.archive,backup.archive));
    assert(google_backup_cleanup(&second));
    google_backup other=backup; strcpy(other.archive,"build/host/unrelated");
    file(other.archive,"keep");
    assert(!google_backup_cleanup(&other) && access(other.archive,F_OK)==0);
    assert(google_backup_cleanup(&backup) && !backup.archive[0]); remove_cache();
    reset(); fail_cache=1; assert(!google_backup_stage(&backup,cancel,NULL) && !mount_calls);
    assert(strstr(backup.diagnostic,"cache creation: errno="));
    reset(); fail_temp=1; assert(!google_backup_stage(&backup,cancel,NULL) && !mount_calls && !backup.temp_dir[0]);
    assert(strstr(backup.diagnostic,"mkdtemp: errno=")); remove_cache();
    reset(); fail_time=1; assert(!google_backup_stage(&backup,cancel,NULL) && unmount_calls==1 && !mounted);
    assert(strstr(backup.diagnostic,"UTC timestamp (time): errno="));
    assert(google_backup_cleanup(&backup)); remove_cache();
    reset(); strcpy(backup.archive,"build/host/missing-archive");
    assert(!google_backup_hash(&backup,cancel,NULL) && strstr(backup.diagnostic,"archive reading (fopen)"));
    reset(); strcpy(backup.archive,"build/host/empty-archive"); file(backup.archive,"");
    assert(!google_backup_hash(&backup,cancel,NULL) && strstr(backup.diagnostic,"archive reading (empty)"));
    assert(!unlink(backup.archive));
    reset(); fail_mount=1; assert(!google_backup_stage(&backup,cancel,NULL) && !unmount_calls);
    assert(strstr(backup.diagnostic,"mount: errno="));
    assert(google_backup_cleanup(&backup)); remove_cache();
    reset(); fail_unmount=1; assert(!google_backup_stage(&backup,cancel,NULL));
    assert(backup.mount_blocked && !backup.size && mounted);
    assert(strstr(backup.diagnostic,"unmount: errno="));
    assert(google_backup_cleanup(&backup)); remove_cache();
    reset(); cancel_during_zip=1; assert(!google_backup_stage(&backup,cancel,NULL));
    assert(unmount_calls==1 && !mounted && !backup.size); assert(google_backup_cleanup(&backup)); remove_cache();
    reset(); cancelled_=1; assert(!google_backup_stage(&backup,cancel,NULL) && !mount_calls);
    reset(); backup.user=99; assert(!google_backup_stage(&backup,cancel,NULL) && !mount_calls);
    reset(); strcpy(backup.directory,"../GoogleAuth"); assert(!google_backup_stage(&backup,cancel,NULL) && !mount_calls);
    reset(); memset(backup.directory,'a',32); backup.directory[32]=0;
    assert(!google_backup_stage(&backup,cancel,NULL) && !mount_calls);
    reset(); strcpy(backup.title,"PSSY00001"); strcpy(backup.directory,"GoogleAuth");
    assert(!google_backup_stage(&backup,cancel,NULL) && !mount_calls);
    reset();
    /* Symlinks must not escape into credentials/unrelated files, or produce partial success. */
    assert(!symlink("../../unrelated","build/host/mount/SAVE_DATA/link"));
    assert(!google_backup_stage(&backup,cancel,NULL) && !mounted);
    assert(strstr(backup.diagnostic,"ZIP unsupported file type"));
    char captured[sizeof(backup.diagnostic)]; strcpy(captured,backup.diagnostic);
    assert(google_backup_cleanup(&backup)); remove_cache();
    assert(!strcmp(captured,backup.diagnostic));
    unlink("build/host/mount/SAVE_DATA/link"); unlink("build/host/unrelated");
    FILE *fp=fopen("build/host/mount/SAVE_DATA/sce_sys/param.sfo","rb"); assert(fp);
    memset(text,0,sizeof(text)); assert(fread(text,1,sizeof(text),fp)==14 && !strcmp(text,"original-param")); fclose(fp);
#ifdef __PS4__
    reset(); ordinary=backup; backup.replace_trace=1;
    phase_calls=interleaved=0; interleave=1;
    assert(google_backup_stage(&backup,cancel,NULL));
    assert(interleaved && phase_calls==4 && !ordinary.replace_trace);
    assert(google_backup_cleanup(&ordinary) && google_backup_cleanup(&backup)); remove_cache();
#endif
    puts("Google save staging tests passed (real ZIP layout; mocked mounts; source unchanged).");
    return 0;
}
