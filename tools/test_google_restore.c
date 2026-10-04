#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sqlite3.h>
#include <zip.h>
#include "saves.h"
#include "settings.h"
#include "google_restore.h"

app_config_t apollo_config={.user_id=42};
static google_backup backup;
static int stop, mode, mounts, patches, details_calls, unmounts, writes;
static const char *target="build/host/restore-mount/";
static save_entry_t entry={.title_id="CUSA12345",.dir_name="SAVE"};
void *open_sqlite_db(const char *path) {
    sqlite3 *db=NULL;
    if (sqlite3_open_v2(path,&db,SQLITE_OPEN_READWRITE,NULL)!=SQLITE_OK) { sqlite3_close(db); return NULL; }
    return db;
}
static void put32(unsigned char *p,unsigned v) { for (unsigned i=0;i<4;i++) p[i]=(v>>(8*i))&255; }
static void put16(unsigned char *p,unsigned v) { p[0]=v&255; p[1]=v>>8; }
static unsigned char sfo[2048]; static size_t sfo_size;
static void make_sfo(const char *title,const char *directory,int malformed) {
    const char *keys[]={"TITLE_ID","SAVEDATA_DIRECTORY","SAVEDATA_BLOCKS","ACCOUNT_ID","PARAMS","MAINTITLE","SUBTITLE","DETAIL","SAVEDATA_LIST_PARAM"};
    memset(sfo,0,sizeof(sfo)); put32(sfo,0x46535000); put32(sfo+4,0x101); put32(sfo+16,9);
    unsigned keyoff=20+9*16, dataoff=512, k=0, v=0; put32(sfo+8,keyoff); put32(sfo+12,dataoff);
    for (unsigned i=0;i<9;i++) {
        unsigned char *e=sfo+20+i*16; unsigned len=4, format=0x404;
        if (i==0 || i==1 || (i>=5 && i<=7)) { len=strlen(i==0?title:i==1?directory:"Fixture")+1; format=0x204; }
        if (i==3) { len=8; format=4; }
        if (i==4) { len=0x400; format=4; }
        put16(e,k); put16(e+2,format); put32(e+4,len); put32(e+8,len); put32(e+12,v);
        strcpy((char*)sfo+keyoff+k,keys[i]); k+=strlen(keys[i])+1;
        if (format==0x204) strcpy((char*)sfo+dataoff+v,i==0?title:i==1?directory:"Fixture");
        if (i==2) put32(sfo+dataoff+v,96);
        if (i==4) strcpy((char*)sfo+dataoff+v+0x2c,title);
        v+=len;
    }
    sfo_size=dataoff+v;
    if (malformed) put32(sfo+20+12,0xfffffff0);
}
static void add(zip_t *z,const char *name,const void *data,size_t size,unsigned type) {
    zip_source_t *s=zip_source_buffer(z,data,size,0); assert(s);
    zip_int64_t i=zip_file_add(z,name,s,0); assert(i>=0);
    assert(!zip_file_set_external_attributes(z,i,0,ZIP_OPSYS_UNIX,(type|0600)<<16));
}
static void fixture(const char *title,const char *directory,const char *path,unsigned type,int malformed) {
    int e; zip_t *z=zip_open(backup.archive,ZIP_CREATE|ZIP_TRUNCATE,&e); assert(z);
    make_sfo(title,directory,malformed);
    add(z,"SAVE/sce_sys/param.sfo",sfo,sfo_size,0100000);
    add(z,path?path:"SAVE/sub/data.bin","restored content",16,type);
    assert(!zip_close(z));
    stop=0;
}
static int cancel(void *p) { (void)p; return stop; }
static int absent(void *p,const google_backup *b) { (void)p; assert(b->user==42); return orbis_SaveTargetAbsent(&entry,b->user); }
static int mount_new(void *p,const google_backup *b,uint32_t blocks,char *out,size_t size) {
    (void)p; (void)b; assert(blocks==96); mounts++;
    if (mode==5) return 0;
    assert(!mkdir(target,0700)); snprintf(out,size,"%s",target);
    if (mode==7) { assert(!symlink("../restore-sentinel","build/host/restore-mount/sub")); }
    if (mode==9) stop=1;
    return 1;
}
static int ownership(void *p,const char *path) { (void)p; assert(!strcmp(path,target)); patches++; return mode!=2; }
static int details(void *p,const google_backup *b,const char *path) { (void)p;(void)b;(void)path; details_calls++; return mode!=6; }
static int unmount(void *p) { (void)p; unmounts++; if (mode==8) stop=1; return mode!=3; }
static int finish(void *p) { (void)p; if (mode==10) { stop=1; return 0; } return !stop; }
ssize_t __real_write(int,const void*,size_t);
ssize_t __wrap_write(int fd,const void *p,size_t n) {
    writes++; if (mode==1) { errno=ENOSPC; return -1; }
    ssize_t result=__real_write(fd,p,n); if (mode==4) stop=1; return result;
}
static void reset_target(void) {
    unlink("build/host/restore-mount/sub/data.bin"); unlink("build/host/restore-mount/sub");
    rmdir("build/host/restore-mount/sub"); unlink("build/host/restore-mount/sce_sys/param.sfo");
    rmdir("build/host/restore-mount/sce_sys"); rmdir(target);
    mounts=patches=details_calls=unmounts=writes=stop=mode=0; backup.mount_blocked=0;
}
static void hash_fixture(void) { assert(google_backup_hash(&backup,cancel,NULL)); }
int main(void) {
    mkdir("build/host/cache",0700); mkdir("build/host/restore-users",0700);
    mkdir("build/host/restore-users/0000002a",0700); mkdir("build/host/restore-users/0000002a/CUSA12345",0700);
    mkdir("build/host/restore-sentinel",0700);
    sqlite3 *db; assert(sqlite3_open("build/host/restore-users/0000002a.db",&db)==SQLITE_OK);
    assert(sqlite3_exec(db,"DROP TABLE IF EXISTS savedata; CREATE TABLE savedata(title_id TEXT, dir_name TEXT)",NULL,NULL,NULL)==SQLITE_OK);
    sqlite3_close(db);
    strcpy(backup.temp_dir,GOOGLE_BACKUP_CACHE "drive-XXXXXX"); assert(mkdtemp(backup.temp_dir));
    snprintf(backup.archive,sizeof(backup.archive),"%s/backup.zip",backup.temp_dir);
    strcpy(backup.title,"CUSA12345"); strcpy(backup.directory,"SAVE"); backup.user=42;
    google_restore_io io={NULL,cancel,absent,mount_new,ownership,details,unmount,finish};
    reset_target(); fixture("CUSA12345","SAVE",NULL,0100000,0); hash_fixture();
    assert(absent(NULL,&backup)==1); assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_SUCCESS);
    assert(mounts==1 && patches==1 && details_calls==1 && unmounts==1);
    char data[17]={0}; FILE *f=fopen("build/host/restore-mount/sub/data.bin","rb"); assert(f);
    assert(fread(data,1,16,f)==16 && !strcmp(data,"restored content")); assert(!fclose(f));
    assert(!access(backup.archive,R_OK)); reset_target();
    /* Real absence checks: key, orphan volume, DB-only row, wrong user, errors. */
    const char *paths[]={"build/host/restore-users/0000002a/CUSA12345/SAVE.bin","build/host/restore-users/0000002a/CUSA12345/sdimg_SAVE"};
    for (unsigned i=0;i<2;i++) {
        f=fopen(paths[i],"wb"); assert(f); assert(!fclose(f));
        assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED && mounts==0 && writes==0);
        assert(strstr(backup.diagnostic,"already exists")); assert(!unlink(paths[i]));
    }
    assert(sqlite3_open("build/host/restore-users/0000002a.db",&db)==SQLITE_OK);
    assert(sqlite3_exec(db,"INSERT INTO savedata VALUES('CUSA12345','SAVE')",NULL,NULL,NULL)==SQLITE_OK); sqlite3_close(db);
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED && mounts==0);
    assert(sqlite3_open("build/host/restore-users/0000002a.db",&db)==SQLITE_OK);
    assert(sqlite3_exec(db,"DELETE FROM savedata",NULL,NULL,NULL)==SQLITE_OK); sqlite3_close(db);
    assert(orbis_SaveTargetAbsent(&entry,43)==-1);
    assert(!rename("build/host/restore-users/0000002a.db","build/host/restore-users/hidden.db"));
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED && mounts==0);
    assert(!rename("build/host/restore-users/hidden.db","build/host/restore-users/0000002a.db"));
    const char *badpaths[]={"SAVE/../escape","/SAVE/escape","SAVE/sub\\escape","SAVE/sub:escape","OTHER/file"};
    for (unsigned i=0;i<5;i++) {
        fixture("CUSA12345","SAVE",badpaths[i],0100000,0); hash_fixture();
        assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED && mounts==0 && writes==0);
    }
    fixture("CUSA12345","SAVE",NULL,0120000,0); hash_fixture();
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED && mounts==0);
    for (unsigned i=0;i<3;i++) {
        fixture(i==0?"CUSA99999":"CUSA12345",i==1?"OTHER":"SAVE",NULL,0100000,i==2); hash_fixture();
        assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED && mounts==0 && strstr(backup.diagnostic,"SFO"));
    }
    fixture("CUSA12345","SAVE",NULL,0100000,0); hash_fixture();
    for (int failure=1;failure<=10;failure++) {
        reset_target(); mode=failure;
        int result=google_restore_run(&backup,&io);
        assert(result!=GOOGLE_UPLOAD_SUCCESS && !access(backup.archive,R_OK));
        assert(mounts==1 && unmounts==(failure==5?0:1));
        assert(backup.mount_blocked==(failure==3));
        if (failure==4 || failure==8 || failure==9 || failure==10) assert(result==GOOGLE_UPLOAD_CANCELLED);
        if (failure==2) assert(strstr(backup.diagnostic,"Ownership"));
        if (failure==3) assert(strstr(backup.diagnostic,"Unmount"));
        if (failure==7) assert(access("build/host/restore-sentinel/data.bin",F_OK));
    }
    reset_target(); stop=1;
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_CANCELLED && mounts==0);
    stop=0; /* Changes after download must fail the second hash before creation. */
    f=fopen(backup.archive,"ab"); assert(f); assert(fputc('x',f)!=EOF); assert(!fclose(f));
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED && mounts==0);
    assert(google_backup_cleanup(&backup));
    puts("Google restore host tests passed (empty/existing targets, SFO, paths, writes, cancellation, ownership, details, unmount).");
    return 0;
}
