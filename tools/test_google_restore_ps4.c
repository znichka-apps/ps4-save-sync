#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "saves.h"
#include "google_restore.h"
#include "../source/google_restore_ps4.c"

app_config_t apollo_config={.user_id=42};
static int target_absent=1, import_calls;
int orbis_SaveTargetAbsent(const save_entry_t *s,uint32_t user) {
    assert(user==42&&!strcmp(s->title_id,"CUSA12345")&&!strcmp(s->dir_name,"SAVE"));
    return target_absent;
}
int orbis_ImportStagedSave(const char *stage,const char *title,const char *directory,uint32_t user,
    int (*cancelled)(void*),void *data,int *mount_blocked) {
    assert(!strcmp(stage,"stagepath")&&!strcmp(title,"CUSA12345")&&!strcmp(directory,"SAVE")&&user==42);
    assert(!cancelled(data));if(mount_blocked)*mount_blocked=0;import_calls++;return 1;
}
int google_restore_run(google_backup *b,const google_restore_io *io) {
    if(io->cancelled(io->data)||io->absent(io->data,b)!=1)return GOOGLE_UPLOAD_FAILED;
    if(!io->import_staged(io->data,b,"stagepath"))return GOOGLE_UPLOAD_FAILED;
    return io->finish(io->data)?GOOGLE_UPLOAD_SUCCESS:GOOGLE_UPLOAD_CANCELLED;
}
static int never_cancel(void *p){(void)p;return 0;}
static int completed(void *p){(void)p;return 1;}
int main(void) {
    google_backup b={.user=42};strcpy(b.title,"CUSA12345");strcpy(b.directory,"SAVE");
    assert(google_restore_local(&b,never_cancel,completed,NULL)==GOOGLE_UPLOAD_SUCCESS&&import_calls==1);
    target_absent=0;assert(google_restore_local(&b,never_cancel,completed,NULL)==GOOGLE_UPLOAD_FAILED&&import_calls==1);
    target_absent=1;apollo_config.user_id=43;
    assert(google_restore_local(&b,never_cancel,completed,NULL)==GOOGLE_UPLOAD_FAILED&&import_calls==1);
    puts("Google PS4 restore adapter tests passed.");return 0;
}
