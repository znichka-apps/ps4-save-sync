#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "saves.h"
#include "google_replace.h"
#include "../source/google_replace_ps4.c"

app_config_t apollo_config={.user_id=42};
static int target=1,mounted,upload_calls,delete_calls,import_calls,commit_calls,mode;
int orbis_SaveTargetAbsent(const save_entry_t *s,uint32_t user)
{
    assert(user==42 && !strcmp(s->title_id,"CUSA12345") && !strcmp(s->dir_name,"SAVE"));
    return target?0:1;
}
int orbis_SaveDelete(const save_entry_t *s)
{
    assert(!mounted && !strcmp(s->title_id,"CUSA12345"));
    delete_calls++;target=0;return 1;
}
int google_backup_stage(google_backup *b,int (*cancel)(void*),void *data)
{
    assert(!cancel(data) && !mounted && target);
    mounted=1;
    assert(!strcmp(b->title,"CUSA12345") && b->user==42);
    mounted=0;
    return mode!=1;
}
int google_upload_run(const google_backup *b,const google_upload_io *io)
{
    (void)b;(void)io;
    assert(!mounted);upload_calls++;return mode==2?GOOGLE_UPLOAD_FAILED:GOOGLE_UPLOAD_SUCCESS;
}
int google_restore_local(google_backup *b,int (*cancel)(void*),int (*finish)(void*),void *data)
{
    (void)b;
    assert(!mounted && !target && !cancel(data));
    mounted=1;import_calls++;mounted=0;
    if (mode==3 && import_calls==1) return GOOGLE_UPLOAD_FAILED;
    target=1;return finish(data)?GOOGLE_UPLOAD_SUCCESS:GOOGLE_UPLOAD_CANCELLED;
}
int google_replace_start(google_backup *b,const google_replace_io *io)
{
    assert(io->present(io->data,b)==1);
    google_backup rollback=*b;
    if (!io->stage_backup(io->data,&rollback)) return GOOGLE_REPLACE_FAILED;
    if (io->upload_backup(io->data,&rollback)!=GOOGLE_UPLOAD_SUCCESS) return GOOGLE_REPLACE_FAILED;
    assert(!mounted && io->commit(io->data));
    assert(io->delete_target(io->data,b) && io->present(io->data,b)==0);
    if (io->import_archive(io->data,b,0)) return GOOGLE_REPLACE_SUCCESS;
    assert(io->import_archive(io->data,&rollback,1));
    return GOOGLE_REPLACE_ROLLED_BACK;
}
int google_replace_recover(uint32_t user,const google_replace_io *io)
{
    assert(user==42 && !mounted);
    google_backup b={.user=42};strcpy(b.title,"CUSA12345");strcpy(b.directory,"SAVE");
    if (io->present(io->data,&b)==1) assert(io->delete_target(io->data,&b));
    return io->import_archive(io->data,&b,1)?GOOGLE_REPLACE_ROLLED_BACK:GOOGLE_REPLACE_NEEDS_RECOVERY;
}
static int no_cancel(void *p) { (void)p;return 0; }
static int begin(void *p) { (void)p;commit_calls++;return 1; }
int main(void)
{
    google_backup b={.user=42};strcpy(b.title,"CUSA12345");strcpy(b.directory,"SAVE");
    google_upload_io network={0};
    assert(google_replace_local(&b,&network,no_cancel,begin,NULL)==GOOGLE_REPLACE_SUCCESS);
    assert(target && !mounted && upload_calls==1 && delete_calls==1 && import_calls==1 && commit_calls==1);
    mode=1;assert(google_replace_local(&b,&network,no_cancel,begin,NULL)==GOOGLE_REPLACE_FAILED);
    assert(upload_calls==1 && delete_calls==1 && target);
    mode=2;assert(google_replace_local(&b,&network,no_cancel,begin,NULL)==GOOGLE_REPLACE_FAILED);
    assert(upload_calls==2 && delete_calls==1 && target);
    mode=3;import_calls=0;
    assert(google_replace_local(&b,&network,no_cancel,begin,NULL)==GOOGLE_REPLACE_ROLLED_BACK);
    assert(target && !mounted && import_calls==2);
    target=0;mode=0;int blocked=0;
    assert(google_replace_recover_local(42,&blocked)==GOOGLE_REPLACE_ROLLED_BACK && target && !blocked);
    puts("Google PS4 replacement adapter tests passed (unmount before upload, explicit delete/import, recovery).");
    return 0;
}
