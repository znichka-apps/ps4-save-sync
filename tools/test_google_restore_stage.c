#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <zip.h>
#include "google_restore.h"

static google_backup backup;
static int cancel_after, cancel_calls, absent_calls, target_race, imports, cleanup_failure;
static int cancelled(void *p);
static void put32(unsigned char *p,unsigned v) { for(unsigned i=0;i<4;i++) p[i]=(v>>(8*i))&255; }
static void put16(unsigned char *p,unsigned v) { p[0]=v&255; p[1]=v>>8; }
static unsigned char sfo[2048]; static size_t sfo_size;
static void make_sfo(void) {
    const char *keys[]={"TITLE_ID","SAVEDATA_DIRECTORY","SAVEDATA_BLOCKS","ACCOUNT_ID","PARAMS","MAINTITLE","SUBTITLE","DETAIL","SAVEDATA_LIST_PARAM"};
    memset(sfo,0,sizeof(sfo)); put32(sfo,0x46535000); put32(sfo+4,0x101); put32(sfo+16,9);
    unsigned keyoff=20+9*16,dataoff=512,k=0,v=0; put32(sfo+8,keyoff); put32(sfo+12,dataoff);
    for(unsigned i=0;i<9;i++) {
        unsigned char *e=sfo+20+i*16; unsigned len=4,format=0x404;
        if(i==0||i==1||(i>=5&&i<=7)){len=strlen(i==0?"CUSA12345":i==1?"SAVE":"Fixture")+1;format=0x204;}
        if(i==3){len=8;format=4;} if(i==4){len=0x400;format=4;}
        put16(e,k);put16(e+2,format);put32(e+4,len);put32(e+8,len);put32(e+12,v);
        strcpy((char*)sfo+keyoff+k,keys[i]);k+=strlen(keys[i])+1;
        if(format==0x204)strcpy((char*)sfo+dataoff+v,i==0?"CUSA12345":i==1?"SAVE":"Fixture");
        if(i==2) put32(sfo+dataoff+v,96);
        if(i==4) strcpy((char*)sfo+dataoff+v+0x2c,"CUSA12345");
        v+=len;
    }
    sfo_size=dataoff+v;
}
static void add(zip_t *z,const char *name,const void *data,size_t size,unsigned mode) {
    zip_source_t *s=zip_source_buffer(z,data,size,0);assert(s);zip_int64_t i=zip_file_add(z,name,s,0);assert(i>=0);
    assert(!zip_file_set_external_attributes(z,i,0,ZIP_OPSYS_UNIX,(mode|0600)<<16));
}
static void fixture(const char *extra,unsigned extra_mode,int include_sfo) {
    int err;zip_t *z=zip_open(backup.archive,ZIP_CREATE|ZIP_TRUNCATE,&err);assert(z);make_sfo();
    if(include_sfo)add(z,"SAVE/sce_sys/param.sfo",sfo,sfo_size,0100000);
    add(z,"SAVE/sub/data.bin","payload",7,0100000);
    if(extra)add(z,extra,"bad",3,extra_mode);
    assert(!zip_close(z));
    assert(google_backup_hash(&backup,cancelled,NULL));
    cancel_calls=absent_calls=imports=0;
}
static int cancelled(void *p) { (void)p; return cancel_after && ++cancel_calls>=cancel_after; }
static int absent(void *p,const google_backup *b) {
    (void)p;assert(!strcmp(b->title,"CUSA12345")&&!strcmp(b->directory,"SAVE"));
    absent_calls++;return target_race&&absent_calls>=2?0:1;
}
static int import_staged(void *p,const google_backup *b,const char *stage) {
    (void)p;assert(!strcmp(b->title,"CUSA12345")&&!strcmp(b->directory,"SAVE"));
    char path[1024],data[8]={0};
    snprintf(path,sizeof(path),"%s/PS4/APOLLO/SAVE/sce_sys/param.sfo",stage);assert(!access(path,R_OK));
    snprintf(path,sizeof(path),"%s/PS4/APOLLO/SAVE/sub/data.bin",stage);
    FILE *f=fopen(path,"rb");assert(f);assert(fread(data,1,7,f)==7&&!fclose(f)&&!strcmp(data,"payload"));
    imports++;return 1;
}
static int finish(void *p) { (void)p;return !cancelled(NULL); }
int __real_rmdir(const char *);
int __wrap_rmdir(const char *p) {
    size_t n=strlen(p);
    if(cleanup_failure&&n>=6&&!strcmp(p+n-6,"/stage")){errno=EIO;return -1;}
    return __real_rmdir(p);
}
static void clear_tree(const char *p) {
    struct stat st;if(lstat(p,&st))return;
    if(!S_ISDIR(st.st_mode)){unlink(p);return;}
    DIR *d=opendir(p);assert(d);struct dirent *e;
    while((e=readdir(d)))if(strcmp(e->d_name,".")&&strcmp(e->d_name,"..")){char c[1024];snprintf(c,sizeof(c),"%s/%s",p,e->d_name);clear_tree(c);}
    closedir(d);rmdir(p);
}
static void reset(void) {
    cancel_after=cancel_calls=absent_calls=target_race=imports=cleanup_failure=0;
    clear_tree(backup.temp_dir);strcpy(backup.temp_dir,GOOGLE_BACKUP_CACHE "drive-XXXXXX");assert(mkdtemp(backup.temp_dir));
    snprintf(backup.archive,sizeof(backup.archive),"%s/backup.zip",backup.temp_dir);
    strcpy(backup.title,"CUSA12345");strcpy(backup.directory,"SAVE");backup.user=42;
}
int main(void) {
    clear_tree("/tmp/ps4-save-sync-restore-cache");
    mkdir("/tmp/ps4-save-sync-restore-cache",0700);
    strcpy(backup.temp_dir,GOOGLE_BACKUP_CACHE "drive-XXXXXX");assert(mkdtemp(backup.temp_dir));
    snprintf(backup.archive,sizeof(backup.archive),"%s/backup.zip",backup.temp_dir);
    strcpy(backup.title,"CUSA12345");strcpy(backup.directory,"SAVE");backup.user=42;
    google_restore_io io={NULL,cancelled,absent,import_staged,finish};

    fixture(NULL,0100000,1);
    int first=google_restore_run(&backup,&io);
    if(first!=GOOGLE_UPLOAD_SUCCESS)fprintf(stderr,"restore stage failed: %s\n",backup.diagnostic);
    assert(first==GOOGLE_UPLOAD_SUCCESS);
    assert(imports==1&&absent_calls==2&&access(backup.archive,F_OK)==0);
    assert(access("build/host/cache/no-such-stage",F_OK));

    const char *bad[]={"OTHER/root.bin","SAVE/../escape","/absolute","SAVE/link\\escape"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
        reset();fixture(bad[i],0100000,1);assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED);
        assert(!imports&&access(backup.archive,F_OK)==0);
    }
    reset();fixture("SAVE/symlink",0120000,1);
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED&&!imports&&access(backup.archive,F_OK)==0);
    reset();fixture(NULL,0100000,0);
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED&&!imports&&access(backup.archive,F_OK)==0);
    reset();fixture(NULL,0100000,1);target_race=1;
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED&&!imports&&absent_calls==2&&access(backup.archive,F_OK)==0);
    reset();fixture(NULL,0100000,1);cancel_after=4;
    int cancelled_result=google_restore_run(&backup,&io);
    if(cancelled_result!=GOOGLE_UPLOAD_CANCELLED)fprintf(stderr,"cancel result=%d calls=%d imports=%d diag=%s\n",cancelled_result,cancel_calls,imports,backup.diagnostic);
    assert(cancelled_result==GOOGLE_UPLOAD_CANCELLED&&!imports&&access(backup.archive,F_OK)==0);
    reset();fixture(NULL,0100000,1);cleanup_failure=1;
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED&&imports==1&&access(backup.archive,F_OK)==0);
    cleanup_failure=0;assert(!access(backup.temp_dir,F_OK));
    clear_tree(backup.temp_dir);
    assert(!rmdir("/tmp/ps4-save-sync-restore-cache"));
    puts("Google restore staging tests passed.");
    return 0;
}
