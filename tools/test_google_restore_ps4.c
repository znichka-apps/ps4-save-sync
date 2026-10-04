/* Adapter ownership contract: mock SFO I/O, actual HMAC and adapter code. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "saves.h"
#include "sfo.h"
#include "google_restore.h"
#include "../source/google_restore_ps4.c"
app_config_t apollo_config={.user_id=42,.account_id=12345,.psid={987,654}};
struct sfo_context_s { int unused; };
static unsigned char account[8], params[0x400];
static int fail, mount_calls;
sfo_context_t *sfo_alloc(void) { return calloc(1,sizeof(sfo_context_t)); }
void sfo_free(sfo_context_t *s) { free(s); }
int sfo_read(sfo_context_t *s,const char *p) { (void)s;(void)p; return fail==5?-1:0; }
uint8_t *sfo_get_param_value(sfo_context_t *s,const char *name) {
    (void)s;
    if (!strcmp(name,"ACCOUNT_ID")) return fail==6?NULL:account;
    if (!strcmp(name,"PARAMS")) return params;
    return NULL;
}
int patch_sfo(const char *p,sfo_patch_t *patch) {
    assert(!strcmp(p,"mount/sce_sys/param.sfo"));
    assert(patch->user_id==42 && patch->account_id==12345 && patch->psid==(uint8_t*)apollo_config.psid);
    if (fail==1) return -1;
    memcpy(account,&patch->account_id,8); memcpy(params+4,&patch->user_id,4);
    static const unsigned char key[]={0x13,0xd1,0xdf,0x06,0x75,0xc9,0xfd,0x95,0x0a,0x17,0xe5,0x64,0xc2,0x77,0x7f,0x2c};
    assert(!mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),key,sizeof(key),patch->psid,16,params+8));
    if (fail==2) account[0]^=1;
    if (fail==3) params[4]^=1;
    if (fail==4) params[8]^=1;
    return 0;
}
int orbis_SaveTargetAbsent(const save_entry_t *s,uint32_t user) { (void)s; return user==42?1:-1; }
int orbis_SaveMountEmpty(const save_entry_t *s,uint32_t user,char *out) {
    assert(user==42 && s->blocks==96 && !strcmp(s->title_id,"CUSA12345") && !strcmp(s->dir_name,"SAVE"));
    assert(s->flags==(SAVE_FLAG_PS4|SAVE_FLAG_HDD)); mount_calls++; strcpy(out,"SAVE"); return 1;
}
int orbis_SaveUmount(const char *path) { assert(!strcmp(path,"SAVE")); return fail!=7; }
int orbis_UpdateSaveParams(const save_entry_t *s,const char *a,const char *b,const char *c,uint32_t d) { (void)s;(void)a;(void)b;(void)c;(void)d; return 1; }
int google_restore_run(google_backup *b,const google_restore_io *io) { (void)b; return io->cancelled(io->data)?GOOGLE_UPLOAD_CANCELLED:GOOGLE_UPLOAD_FAILED; }
static int never_cancel(void *p) { (void)p;return 0; }
static int completed(void *p) { (void)p;return 1; }
int main(void) {
    restore_context c={.user=42,.cancel=never_cancel,.finish=completed};
    strcpy(c.mount,"SAVE");
    for (fail=0;fail<=6;fail++) assert(ownership(&c,"mount/")==!fail);
    fail=7; assert(!unmount(&c)); fail=0;
    google_backup b={.user=42}; strcpy(b.title,"CUSA12345"); strcpy(b.directory,"SAVE"); char path[256];
    assert(create(&c,&b,96,path,sizeof(path)) && mount_calls==1);
    assert(!strcmp(path,"build/host/restore-mount/SAVE/"));
    apollo_config.account_id=0; assert(!create(&c,&b,96,path,sizeof(path)) && mount_calls==1);
    apollo_config.user_id=43; assert(cancel(&c) && !finish(&c));
    assert(google_restore_local(&b,never_cancel,completed,NULL)==GOOGLE_UPLOAD_CANCELLED);
    puts("PS4 restore adapter host tests passed (local ownership fields/HMAC verified, patch/read errors, zero account, user change, unmount).");
    return 0;
}
