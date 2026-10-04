/* PS4 adapter runs inside the existing exclusive Google worker. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mbedtls/md.h>
#include "saves.h"
#include "settings.h"
#include "sfo.h"
#include "google_restore.h"

typedef struct {
    uint32_t user;
    char mount[32];
    int (*cancel)(void*), (*finish)(void*);
    void *data;
} restore_context;
static void entry(const google_backup *b, save_entry_t *s)
{
    memset(s,0,sizeof(*s)); s->name=(char*)b->game;
    s->title_id=(char*)b->title; s->dir_name=(char*)b->directory;
    s->flags=SAVE_FLAG_PS4|SAVE_FLAG_HDD; s->type=FILE_TYPE_PS4;
}
static int cancel(void *p) {
    restore_context *c=p;
    return c->user!=apollo_config.user_id || c->cancel(c->data);
}
static int finish(void *p) {
    restore_context *c=p; return !cancel(p) && c->finish(c->data);
}
static int absent(void *p, const google_backup *b) {
    restore_context *c=p; save_entry_t s; entry(b,&s);
    return orbis_SaveTargetAbsent(&s,c->user);
}
static int create(void *p, const google_backup *b, uint32_t blocks, char *path, size_t cap)
{
    restore_context *c=p; save_entry_t s; entry(b,&s); s.blocks=blocks;
    /* Apollo skips zero account/user fields. Reject rather than retain foreign ownership. */
    if (cancel(p) || !apollo_config.account_id || !c->user) return 0;
    if (!orbis_SaveMountEmpty(&s,c->user,c->mount)) return 0;
    snprintf(path,cap,APOLLO_SANDBOX_PATH,c->mount); return 1;
}
static int ownership(void *p, const char *mount)
{
    restore_context *c=p; char path[288];
    snprintf(path,sizeof(path),"%ssce_sys/param.sfo",mount);
    sfo_patch_t patch={.user_id=c->user, .account_id=apollo_config.account_id, .psid=(uint8_t*)apollo_config.psid};
    /* Verify all fields after Apollo patches; its older void PSID helper logs
       HMAC errors, so a return code alone cannot establish ownership. */
    static const unsigned char key[]={0x13,0xd1,0xdf,0x06,0x75,0xc9,0xfd,0x95,0x0a,0x17,0xe5,0x64,0xc2,0x77,0x7f,0x2c};
    unsigned char expected[32];
    if (cancel(p) || mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),key,sizeof(key),patch.psid,16,expected) ||
        patch_sfo(path,&patch)<0) return 0;
    sfo_context_t *s=sfo_alloc(); if (!s) return 0;
    int ok=0;
    if (sfo_read(s,path)>=0) {
        unsigned char *account=sfo_get_param_value(s,"ACCOUNT_ID"), *params=sfo_get_param_value(s,"PARAMS");
        ok=account && params && !memcmp(account,&patch.account_id,8) &&
            !memcmp(params+4,&patch.user_id,4) && !memcmp(params+8,expected,32);
    }
    sfo_free(s); return ok;
}
static int details(void *p, const google_backup *b, const char *mount)
{
    if (cancel(p)) return 0;
    char path[288]; snprintf(path,sizeof(path),"%ssce_sys/param.sfo",mount);
    save_entry_t save; entry(b,&save);
    sfo_context_t *s=sfo_alloc(); if (!s) return 0;
    int ok=0;
    if (sfo_read(s,path)>=0) {
        char *title=(char*)sfo_get_param_value(s,"MAINTITLE"), *subtitle=(char*)sfo_get_param_value(s,"SUBTITLE"),
             *detail=(char*)sfo_get_param_value(s,"DETAIL");
        uint32_t *param=(uint32_t*)sfo_get_param_value(s,"SAVEDATA_LIST_PARAM");
        if (title && subtitle && detail && param) ok=orbis_UpdateSaveParams(&save,title,subtitle,detail,*param);
    }
    sfo_free(s); return ok;
}
static int unmount(void *p) { return orbis_SaveUmount(((restore_context*)p)->mount); }
int google_restore_local(google_backup *b, int (*cancelled)(void*), int (*completed)(void*), void *data)
{
    restore_context c={.user=b->user,.cancel=cancelled,.finish=completed,.data=data};
    google_restore_io io={&c,cancel,absent,create,ownership,details,unmount,finish};
    return google_restore_run(b,&io);
}
