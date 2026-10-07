#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "saves.h"
#include "google_restore.h"
#include "account_identity.h"
#include "../source/google_restore_ps4.c"

app_config_t apollo_config={.user_id=42};
static int target_absent=1, import_calls, import_fail, unmount_fail;
static int lookup_calls;
static int32_t lookup_status;
static uint64_t lookup_id=123;
static uint32_t expected_trace_user;
static char last_phase[112];
static char last_lookup_phase[112];
void google_replace_phase(uint32_t user,const char *phase,const char *step) {
    assert(user==42);
    snprintf(last_phase,sizeof(last_phase),"%s: %s",phase,step);
    if (!strcmp(phase,"import account lookup"))
        snprintf(last_lookup_phase,sizeof(last_lookup_phase),"%s: %s",phase,step);
}
static int32_t fake_lookup(int32_t user,uint64_t *account_id) {
    assert(user==42);lookup_calls++;
    *account_id=lookup_id;
    return lookup_status;
}
int account_id_refresh_live(uint32_t user,uint64_t *account_id,int32_t *status) {
    return account_identity_refresh(user,account_id,fake_lookup,status);
}
int orbis_SaveTargetAbsent(const save_entry_t *s,uint32_t user) {
    assert(user==42&&!strcmp(s->title_id,"CUSA12345")&&!strcmp(s->dir_name,"SAVE"));
    return target_absent;
}
int orbis_ImportStagedSave(const char *stage,const char *title,const char *directory,uint32_t user,
    int (*cancelled)(void*),void *data,int *mount_blocked,uint32_t trace_user) {
    assert(!strcmp(stage,"stagepath")&&!strcmp(title,"CUSA12345")&&!strcmp(directory,"SAVE")&&user==42);
    assert(trace_user==expected_trace_user);
    assert(apollo_config.account_id==123);
    assert(!cancelled(data));if(mount_blocked)*mount_blocked=0;import_calls++;
    if (unmount_fail) { *mount_blocked=1; errno=ENOENT; return 0; }
    if (import_fail) { errno=ENOENT; return 0; }
    return 1;
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
    b.replace_trace=1;expected_trace_user=42;
    assert(google_restore_local(&b,never_cancel,completed,NULL)==GOOGLE_UPLOAD_SUCCESS&&import_calls==2);
    lookup_status=-1234;
    assert(google_restore_local(&b,never_cancel,completed,NULL)==GOOGLE_UPLOAD_FAILED&&import_calls==2);
    assert(apollo_config.account_id==0 && strstr(b.diagnostic,"native=-1234 call=sceUserServiceGetNpAccountId"));
    FILE *log=fopen("build/host/google_restore.log","rb");assert(log);
    char line[512];int logged=0;
    while(fgets(line,sizeof(line),log))if(strstr(line,"stage=account lookup op=10 errno=0 zip=0 native=-1234"))logged=1;
    assert(!fclose(log)&&logged);
    assert(!strcmp(last_lookup_phase,"import account lookup: result=0 native=-1234 errno=0 call=sceUserServiceGetNpAccountId"));
    assert(!strcmp(last_phase,"import adapter: result=0 native=0 errno=0 call=account_id"));
    lookup_status=0;lookup_id=0;
    assert(google_restore_local(&b,never_cancel,completed,NULL)==GOOGLE_UPLOAD_FAILED&&import_calls==2);
    assert(apollo_config.account_id==0 && strstr(b.diagnostic,"native=0 call=sceUserServiceGetNpAccountId"));
    assert(!strcmp(last_lookup_phase,"import account lookup: result=0 native=0 errno=0 call=sceUserServiceGetNpAccountId"));
    lookup_id=123;
    import_fail=1;
    assert(google_restore_local(&b,never_cancel,completed,NULL)==GOOGLE_UPLOAD_FAILED&&import_calls==3);
    assert(!strcmp(last_phase,"import adapter: result=0 native=0 errno=2 call=ImportStagedSave"));
    assert(errno==ENOENT);
    import_fail=0;
    unmount_fail=1;
    assert(google_restore_local(&b,never_cancel,completed,NULL)==GOOGLE_UPLOAD_FAILED&&import_calls==4);
    assert(b.mount_blocked && errno==ENOENT);
    assert(!strcmp(last_phase,"import adapter: result=0 native=0 errno=2 call=ImportStagedSave"));
    unmount_fail=0; b.mount_blocked=0;
    b.replace_trace=0;expected_trace_user=0;
    target_absent=0;assert(google_restore_local(&b,never_cancel,completed,NULL)==GOOGLE_UPLOAD_FAILED&&import_calls==4);
    target_absent=1;apollo_config.user_id=43;
    assert(google_restore_local(&b,never_cancel,completed,NULL)==GOOGLE_UPLOAD_FAILED&&import_calls==4);
    assert(lookup_calls>=6);
    puts("Google PS4 restore adapter tests passed.");return 0;
}
