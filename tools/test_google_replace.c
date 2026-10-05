#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "google_replace.h"

static google_backup selected;
static int target, validate_fail, stage_fail, upload_fail, commit_fail, delete_fail;
static int import_fail, create_fail, rollback_fail, cancel_now, cancel_on_upload;
static int stage_calls, upload_calls, delete_calls, import_calls;
enum { ABSENT, OLD, NEW, PARTIAL };
int restore_fs_init(int *error) { *error=0; return 1; }
int restore_fs_lstat(const char *path,struct stat *st) { return lstat(path,st); }
int google_restore_validate(google_backup *b,int (*cancel)(void*),void *data)
{
    b->diagnostic[0]=0;
    if (cancel(data)) return GOOGLE_UPLOAD_CANCELLED;
    if (validate_fail || strcmp(b->title,"CUSA12345") || strcmp(b->directory,"SAVE") ||
        (strcmp(b->md5,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") &&
         strcmp(b->md5,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"))) return GOOGLE_UPLOAD_FAILED;
    FILE *f=fopen(b->archive,"rb"); if (!f) return GOOGLE_UPLOAD_FAILED;
    int ch=fgetc(f); int end=fgetc(f); fclose(f);
    if (end!=EOF || (ch!='S' && ch!='R') ||
        (ch=='S' && strcmp(b->md5,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")) ||
        (ch=='R' && strcmp(b->md5,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"))) return GOOGLE_UPLOAD_FAILED;
    return GOOGLE_UPLOAD_SUCCESS;
}
static int cancelled(void *unused) { (void)unused; return cancel_now; }
static int present(void *unused,const google_backup *b)
{
    (void)unused;assert(!strcmp(b->title,"CUSA12345")&&!strcmp(b->directory,"SAVE"));
    return target==ABSENT?0:1;
}
static int stage_backup(void *unused,google_backup *b)
{
    (void)unused;assert(target==OLD && !upload_calls && !delete_calls);stage_calls++;
    if (stage_fail) return 0;
    snprintf(b->temp_dir,sizeof(b->temp_dir),"/tmp/google-replace-stage");
    snprintf(b->archive,sizeof(b->archive),"%s/backup.zip",b->temp_dir);
    FILE *f=fopen(b->archive,"wb"); assert(f); assert(fputc('R',f)!='\0' && !fclose(f));
    strcpy(b->md5,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    strcpy(b->utc,"2026-10-05T00:00:00Z"); b->size=1;
    return 1;
}
static int upload_backup(void *unused,const google_backup *b)
{
    (void)unused;upload_calls++;
    assert(target==OLD && stage_calls==1 && !delete_calls &&
        !strcmp(b->md5,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    if (cancel_on_upload) cancel_now=1;
    return upload_fail?GOOGLE_UPLOAD_FAILED:GOOGLE_UPLOAD_SUCCESS;
}
static int commit(void *unused) { (void)unused;return !commit_fail && !cancel_now; }
static int delete_target(void *unused,const google_backup *b)
{
    (void)unused;(void)b;
    assert(upload_calls==1 && google_replace_pending(42,NULL)==1);
    delete_calls++;
    if (delete_fail) { target=PARTIAL; delete_fail=0; return 0; }
    target=ABSENT;return 1;
}
static int import_archive(void *unused,google_backup *b,int recovery)
{
    (void)unused;import_calls++;
    assert(target==ABSENT);
    if (!recovery && create_fail) return 0;
    if ((recovery && rollback_fail) || (!recovery && import_fail)) {
        target=PARTIAL;return 0;
    }
    assert(!strcmp(b->md5,recovery?"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    target=recovery?OLD:NEW;return 1;
}
static google_replace_io io={NULL,cancelled,present,stage_backup,upload_backup,commit,delete_target,import_archive};
static void remove_fixture(void)
{
    DIR *root=opendir(GOOGLE_REPLACE_ROOT);
    if (root) {
        struct dirent *e;
        while ((e=readdir(root))) if (!strncmp(e->d_name,"tx-",3)) {
            char dir[300],path[340];snprintf(dir,sizeof(dir),"%s%s",GOOGLE_REPLACE_ROOT,e->d_name);
            const char *names[]={"source.zip","rollback.zip","journal","journal.tmp"};
            for (unsigned i=0;i<4;i++) { snprintf(path,sizeof(path),"%s/%s",dir,names[i]);unlink(path); }
            assert(!rmdir(dir));
        }
        closedir(root);assert(!rmdir(GOOGLE_REPLACE_ROOT));
    }
    unlink("/tmp/google-replace-source.zip");unlink("/tmp/google-replace-stage/backup.zip");
}
static void setup(void)
{
    remove_fixture();assert(!mkdir("/tmp/google-replace-stage",0700) || errno==EEXIST);
    memset(&selected,0,sizeof(selected));
    strcpy(selected.game,"Fixture");strcpy(selected.title,"CUSA12345");strcpy(selected.directory,"SAVE");
    strcpy(selected.archive,"/tmp/google-replace-source.zip");
    strcpy(selected.md5,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    strcpy(selected.utc,"2026-10-05T00:00:00Z");selected.size=1;selected.user=42;
    FILE *f=fopen(selected.archive,"wb"); assert(f);assert(fputc('S',f)!='\0'&&!fclose(f));
    target=OLD;validate_fail=stage_fail=upload_fail=commit_fail=delete_fail=0;
    import_fail=create_fail=rollback_fail=cancel_now=cancel_on_upload=0;
    stage_calls=upload_calls=delete_calls=import_calls=0;
}
static void assert_retained(void)
{
    DIR *root=opendir(GOOGLE_REPLACE_ROOT);assert(root);
    struct dirent *e;int found=0;
    while ((e=readdir(root))) if (!strncmp(e->d_name,"tx-",3)) {
        char p[340]; const char *names[]={"source.zip","rollback.zip","journal"};
        for(unsigned i=0;i<3;i++) { snprintf(p,sizeof(p),"%s%s/%s",GOOGLE_REPLACE_ROOT,e->d_name,names[i]);assert(!access(p,R_OK)); }
        found++;
    }
    closedir(root);assert(found==1);
}
int main(void)
{
    setup();validate_fail=1;
    assert(google_replace_start(&selected,&io)==GOOGLE_REPLACE_FAILED && target==OLD && !stage_calls && !delete_calls);
    setup();strcpy(selected.title,"CUSA99999");
    assert(google_replace_start(&selected,&io)==GOOGLE_REPLACE_FAILED && target==OLD && !stage_calls && !delete_calls);
    setup();strcpy(selected.directory,"OTHER");
    assert(google_replace_start(&selected,&io)==GOOGLE_REPLACE_FAILED && target==OLD && !stage_calls && !delete_calls);
    setup();stage_fail=1;
    assert(google_replace_start(&selected,&io)==GOOGLE_REPLACE_FAILED && target==OLD && stage_calls==1 && !upload_calls);
    setup();cancel_now=1;
    assert(google_replace_start(&selected,&io)==GOOGLE_REPLACE_CANCELLED && target==OLD && !delete_calls);
    setup();upload_fail=1;
    assert(google_replace_start(&selected,&io)==GOOGLE_REPLACE_FAILED && target==OLD && !delete_calls && upload_calls==1);
    assert_retained();assert(google_replace_pending(42,NULL)==1);
    assert(google_replace_recover(42,&io)==GOOGLE_REPLACE_ROLLED_BACK && target==OLD && !delete_calls);
    setup();commit_fail=1;
    assert(google_replace_start(&selected,&io)==GOOGLE_REPLACE_FAILED && target==OLD && !delete_calls);
    setup();cancel_on_upload=1;
    assert(google_replace_start(&selected,&io)==GOOGLE_REPLACE_CANCELLED && target==OLD && !delete_calls);
    assert(google_replace_pending(42,NULL)==1);
    cancel_now=0;
    assert(google_replace_recover(42,&io)==GOOGLE_REPLACE_ROLLED_BACK && target==OLD);
    setup();delete_fail=1;
    assert(google_replace_start(&selected,&io)==GOOGLE_REPLACE_ROLLED_BACK && target==OLD && delete_calls==2);
    assert_retained();assert(google_replace_pending(42,NULL)==0);
    setup();import_fail=1;
    assert(google_replace_start(&selected,&io)==GOOGLE_REPLACE_ROLLED_BACK && target==OLD && import_calls==2);
    setup();create_fail=1;
    assert(google_replace_start(&selected,&io)==GOOGLE_REPLACE_ROLLED_BACK && target==OLD && import_calls==2);
    setup();import_fail=rollback_fail=1;
    assert(google_replace_start(&selected,&io)==GOOGLE_REPLACE_NEEDS_RECOVERY && target==PARTIAL);
    assert_retained();assert(google_replace_pending(42,NULL)==1);
    rollback_fail=0;
    assert(google_replace_recover(42,&io)==GOOGLE_REPLACE_ROLLED_BACK && target==OLD);
    assert(google_replace_pending(42,NULL)==0);
    setup();
    assert(google_replace_start(&selected,&io)==GOOGLE_REPLACE_SUCCESS && target==NEW && import_calls==1);
    assert_retained();assert(google_replace_pending(42,NULL)==0);
    remove_fixture();
    puts("Google replacement journal tests passed (validation, backup/upload, cancellation, delete/import failure, rollback retry, retained ZIPs).");
    return 0;
}
