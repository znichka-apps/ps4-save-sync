/* Google restore adapter: stage a validated decrypted save, then use Apollo's
   normal USB save scanner and HDD copy/resign implementation. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "saves.h"
#include "settings.h"
#include "google_restore.h"
#include "google_replace.h"
#include "google_restore_log.h"

typedef struct {
    uint32_t user;
    int (*cancel)(void*), (*finish)(void*);
    void *data;
} restore_context;

static int cancel(void *p)
{
    restore_context *c=p;
    return c->user!=apollo_config.user_id || c->cancel(c->data);
}

static int finish(void *p)
{
    restore_context *c=p;
    return !cancel(p) && c->finish(c->data);
}

static int absent(void *p,const google_backup *b)
{
    restore_context *c=p;
    save_entry_t save={0};
    save.title_id=(char*)b->title;
    save.dir_name=(char*)b->directory;
    int status=cancel(p) ? -1 : orbis_SaveTargetAbsent(&save,c->user);
    int error=errno;
    google_restore_log("target absence",8,status==1?0:error,0,status,"SaveTargetAbsent",status!=1);
    errno=error;
    return status;
}

static int import_staged(void *p,const google_backup *b,const char *stage)
{
    restore_context *c=p;
    int mount_blocked=0;
    /* IDs in the decrypted SFO are used only for title/directory matching.
       Apollo's HDD copy path resigns the destination to this local user. */
    int allowed=!cancel(p);
    int32_t account_status=0;
    errno=0;
    int account_ok=allowed && account_id_refresh_live(c->user,&apollo_config.account_id,&account_status);
    int account_error=errno;
    google_restore_log("account lookup",10,account_ok?0:account_error,0,account_status,
        "sceUserServiceGetNpAccountId",allowed&&!account_ok);
    if (b->replace_trace && allowed) {
        char detail[80];
        snprintf(detail,sizeof(detail),"result=%d native=%d errno=%d call=sceUserServiceGetNpAccountId",
            account_ok,account_status,account_ok?0:account_error);
        google_replace_phase(b->user,"import account lookup",detail);
    }
    if (allowed && !account_ok) {
        /* The HDD copy writes this ID to both param.sfo and savedata.db.
           An offline user can obtain one through Apollo's account activation,
           but an absent or unreadable ID cannot be inferred from the ZIP. */
        snprintf(((google_backup*)b)->diagnostic,sizeof(b->diagnostic),
            account_status == 0 ?
            "This PS4 user has no account ID. Restore needs one for save ownership. Use User Tools > Activate PS4 Accounts, reboot, then retry [op=10 native=%d]. ZIP retained." :
            "PS4 user account ID lookup failed [op=10 native=%d]. Restore needs it for save ownership. If unactivated, use User Tools > Activate PS4 Accounts, reboot, then retry. ZIP retained.",
            account_status);
    }
    errno=0;
    int ok=allowed && account_ok && orbis_ImportStagedSave(stage,b->title,b->directory,c->user,cancel,p,&mount_blocked,
        b->replace_trace?b->user:0);
    int import_error=errno;
    char import_detail[192]={0};
    if (allowed && account_ok) {
        google_restore_last_failure(import_detail,sizeof(import_detail));
        google_restore_log("PS4 import adapter",10,ok?0:import_error,0,ok,"ImportStagedSave",!ok);
        if (!ok && !((google_backup*)b)->diagnostic[0])
            snprintf(((google_backup*)b)->diagnostic,sizeof(b->diagnostic),"%s",
                import_detail[0]?import_detail:"PS4 import failed [op=10 errno=0 zip=0 native=0 call=ImportStagedSave]; ZIP retained.");
    }
    if (b->replace_trace) {
        char detail[64];
        snprintf(detail,sizeof(detail),"result=%d native=%d errno=%d call=%s",ok,ok,ok?0:import_error,
            !allowed?"cancel":!account_ok?"account_id":"ImportStagedSave");
        google_replace_phase(b->user,"import adapter",detail);
    }
    if (mount_blocked) {
        ((google_backup*)b)->mount_blocked=1;
        snprintf(((google_backup*)b)->diagnostic,sizeof(b->diagnostic),
            "Mount uncertain; stop save operations. %.130s",import_detail);
    }
    errno=import_error;
    return ok && !mount_blocked;
}

int google_restore_local(google_backup *b,int (*cancelled)(void*),int (*completed)(void*),void *data)
{
    google_restore_clear_failure();
    restore_context c={.user=b->user,.cancel=cancelled,.finish=completed,.data=data};
    google_restore_io io={&c,cancel,absent,import_staged,finish};
    return google_restore_run(b,&io);
}
