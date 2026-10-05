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
static int cancel_after, cancel_calls, absent_calls, target_reject_at, target_status, imports, cleanup_failure, expect_root_file;
enum { SFO_OK, SFO_SHORT, SFO_MAGIC, SFO_VERSION, SFO_COUNT, SFO_TABLE, SFO_KEY_OFFSET, SFO_INDEX_OFFSET,
    SFO_AGGREGATE, SFO_LONG_KEY, SFO_DUPLICATE,
    SFO_STRING_END, SFO_TITLE_MISMATCH, SFO_DIRECTORY_MISMATCH, SFO_BLOCKS_LOW, SFO_BLOCKS_HIGH,
    SFO_BLOCKS_UPPER, SFO_TITLE_FORMAT, SFO_BLOCKS_FORMAT, SFO_BLOCKS_SHORT,
    SFO_BLOCKS_LONG, SFO_BLOCKS_MAX_MISMATCH, SFO_BLOCKS_VALUE_BOUNDS,
    SFO_ACCOUNT_SIZE, SFO_PARAMS_SIZE, SFO_PARAMS_TITLE,
    SFO_DETAIL_FORMAT, SFO_LIST_FORMAT, SFO_MISSING_LIST_PARAM,
    SFO_BLOCKS_MAX_VALID, SFO_FORMAT_TERMINATOR_IN_MAX, SFO_FORMAT_NO_TERMINATOR,
    SFO_FORMAT_MAX_BOUNDS, SFO_UNKNOWN_LENGTH, SFO_UNKNOWN_OFFSET };
static int sfo_case;
static int cancelled(void *p);
static void put32(unsigned char *p,unsigned v) { for(unsigned i=0;i<4;i++) p[i]=(v>>(8*i))&255; }
static void put16(unsigned char *p,unsigned v) { p[0]=v&255; p[1]=v>>8; }
static unsigned char sfo[2048]; static size_t sfo_size;
static void make_sfo(void) {
    const char *keys[]={"TITLE_ID","SAVEDATA_DIRECTORY","SAVEDATA_BLOCKS","ACCOUNT_ID","PARAMS","MAINTITLE","SUBTITLE","DETAIL","SAVEDATA_LIST_PARAM"};
    int unknown=sfo_case==SFO_FORMAT_TERMINATOR_IN_MAX||sfo_case==SFO_FORMAT_NO_TERMINATOR||
        sfo_case==SFO_FORMAT_MAX_BOUNDS||sfo_case==SFO_UNKNOWN_LENGTH||sfo_case==SFO_UNKNOWN_OFFSET;
    memset(sfo,0,sizeof(sfo)); put32(sfo,0x46535000); put32(sfo+4,0x101); put32(sfo+16,9+unknown);
    unsigned keyoff=20+(9+unknown)*16,dataoff=512,k=0,v=0; put32(sfo+8,keyoff); put32(sfo+12,dataoff);
    for(unsigned i=0;i<9;i++) {
        unsigned char *e=sfo+20+i*16; unsigned len=4,format=0x404;
        if(i==0||i==1||(i>=5&&i<=7)){len=strlen(i==0?"CUSA12345":i==1?"SAVE":"Fixture")+1;format=0x204;}
        if(i==2||i==3){len=8;format=4;} if(i==4){len=0x400;format=4;}
        put16(e,k);put16(e+2,format);put32(e+4,len);put32(e+8,len);put32(e+12,v);
        strcpy((char*)sfo+keyoff+k,keys[i]);k+=strlen(keys[i])+1;
        if(format==0x204)strcpy((char*)sfo+dataoff+v,i==0?"CUSA12345":i==1?"SAVE":"Fixture");
        if(i==2) put32(sfo+dataoff+v,96);
        if(i==4) strcpy((char*)sfo+dataoff+v+0x2c,"CUSA12345");
        v+=len;
    }
    if(unknown) {
        unsigned char *e=sfo+20+9*16;
        put16(e,k);put16(e+2,0x204);put32(e+4,3);put32(e+8,4);put32(e+12,v);
        strcpy((char*)sfo+keyoff+k,"FORMAT");
        memcpy(sfo+dataoff+v,"PS4",3);v+=4;
    }
    sfo_size=dataoff+v;
    switch(sfo_case) {
        case SFO_SHORT: sfo_size=19; break;
        case SFO_MAGIC: put32(sfo,0); break;
        case SFO_VERSION: put32(sfo+4,0x102); break;
        case SFO_COUNT: put32(sfo+16,0); break;
        case SFO_TABLE: put32(sfo+8,0xfffffff0); break;
        case SFO_KEY_OFFSET: put16(sfo+20,0xffff); break;
        case SFO_INDEX_OFFSET: put32(sfo+20+12,0xfffffff0); break;
        case SFO_AGGREGATE: put32(sfo+20+8,11); break;
        case SFO_LONG_KEY: memset(sfo+keyoff+strlen("TITLE_ID")+1+strlen("SAVEDATA_DIRECTORY")+1+strlen("SAVEDATA_BLOCKS")+1+strlen("ACCOUNT_ID")+1+strlen("PARAMS")+1, 'A', 64); break;
        case SFO_DUPLICATE: put16(sfo+20+16,0); break;
        case SFO_STRING_END: sfo[dataoff+strlen("CUSA12345")]='X'; break;
        case SFO_TITLE_MISMATCH: strcpy((char*)sfo+dataoff,"CUSA99999"); break;
        case SFO_TITLE_FORMAT: put16(sfo+20+2,0x404); break;
        case SFO_DIRECTORY_MISMATCH: strcpy((char*)sfo+dataoff+10,"MINE"); break;
        case SFO_BLOCKS_LOW: put32(sfo+dataoff+15,95); break;
        case SFO_BLOCKS_HIGH: put32(sfo+dataoff+15,524289); break;
        case SFO_BLOCKS_UPPER: put32(sfo+dataoff+15+4,1); break;
        case SFO_BLOCKS_FORMAT: put16(sfo+20+2*16+2,0x404); break;
        case SFO_BLOCKS_SHORT: put32(sfo+20+2*16+4,4); put32(sfo+20+2*16+8,4); break;
        case SFO_BLOCKS_LONG: put32(sfo+20+2*16+4,12); put32(sfo+20+2*16+8,12); break;
        case SFO_BLOCKS_MAX_MISMATCH: put32(sfo+20+2*16+8,12); break;
        case SFO_BLOCKS_VALUE_BOUNDS: put32(sfo+20+2*16+12,v-4); break;
        case SFO_ACCOUNT_SIZE: put32(sfo+20+3*16+4,7); put32(sfo+20+3*16+8,7); break;
        case SFO_PARAMS_SIZE: put32(sfo+20+4*16+4,0x53); put32(sfo+20+4*16+8,0x53); break;
        case SFO_PARAMS_TITLE: strcpy((char*)sfo+dataoff+31+0x2c,"CUSA99999"); break;
        case SFO_DETAIL_FORMAT: put16(sfo+20+7*16+2,0x404); break;
        case SFO_LIST_FORMAT: put16(sfo+20+8*16+2,0x204); break;
        case SFO_MISSING_LIST_PARAM: sfo[keyoff+strlen("TITLE_ID")+1+strlen("SAVEDATA_DIRECTORY")+1+strlen("SAVEDATA_BLOCKS")+1+strlen("ACCOUNT_ID")+1+strlen("PARAMS")+1+strlen("MAINTITLE")+1+strlen("SUBTITLE")+1+strlen("DETAIL")+1]='X'; break;
        case SFO_BLOCKS_MAX_VALID: put32(sfo+dataoff+15,524288); break;
        case SFO_FORMAT_NO_TERMINATOR: sfo[dataoff+v-1]='X'; break;
        case SFO_FORMAT_MAX_BOUNDS: put32(sfo+20+9*16+8,5); break;
        case SFO_UNKNOWN_LENGTH: put32(sfo+20+9*16+4,5); break;
        case SFO_UNKNOWN_OFFSET: put32(sfo+20+9*16+12,0xfffffff0); break;
        default: break;
    }
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
    absent_calls++;return target_reject_at&&absent_calls>=target_reject_at?target_status:1;
}
static int import_staged(void *p,const google_backup *b,const char *stage) {
    (void)p;assert(!strcmp(b->title,"CUSA12345")&&!strcmp(b->directory,"SAVE"));
    char path[1024],data[8]={0};
    snprintf(path,sizeof(path),"%s/PS4/APOLLO/SAVE/sce_sys/param.sfo",stage);assert(!access(path,R_OK));
    snprintf(path,sizeof(path),"%s/PS4/APOLLO/SAVE/sub/data.bin",stage);
    FILE *f=fopen(path,"rb");assert(f);assert(fread(data,1,7,f)==7&&!fclose(f)&&!strcmp(data,"payload"));
    if(expect_root_file) {
        snprintf(path,sizeof(path),"%s/PS4/APOLLO/SAVE/root.bin",stage);
        f=fopen(path,"rb");assert(f);assert(fread(data,1,3,f)==3&&!fclose(f)&&!strncmp(data,"bad",3));
    }
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
    cancel_after=cancel_calls=absent_calls=target_reject_at=imports=cleanup_failure=expect_root_file=0;
    target_status=1;
    sfo_case=SFO_OK;
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

    /* A file directly below the archive root needs no new parent directory.
       Creating one from bytes past the parent's terminator occupies the file
       path and makes the exclusive open fail with EEXIST. */
    reset();expect_root_file=1;fixture("SAVE/root.bin",0100000,1);
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_SUCCESS&&imports==1);
    assert(access(backup.archive,F_OK)==0);
    char root_stage[320];snprintf(root_stage,sizeof(root_stage),"%s/stage",backup.temp_dir);
    assert(access(root_stage,F_OK)!=0);

    /* A max-sized but supported allocation is accepted. The following cases
       each pin one fixed, content-free diagnostic check and SFO field. */
    reset();sfo_case=SFO_BLOCKS_MAX_VALID;fixture(NULL,0100000,1);
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_SUCCESS&&imports==1);
    /* FORMAT has len=3, max=4, and its only NUL is at allocation offset 3. */
    reset();sfo_case=SFO_FORMAT_TERMINATOR_IN_MAX;fixture(NULL,0100000,1);
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_SUCCESS&&imports==1);
    struct { int kind; const char *check,*field; } bad_sfo[]={
        {SFO_SHORT,"header too short","SFO header"},
        {SFO_MAGIC,"invalid magic","SFO header"},
        {SFO_VERSION,"unsupported version","SFO header"},
        {SFO_COUNT,"invalid key/value table offsets or entry count","SFO header"},
        {SFO_TABLE,"invalid key/value table offsets or entry count","SFO header"},
        {SFO_KEY_OFFSET,"invalid or unterminated key offset","index entry 0"},
        {SFO_INDEX_OFFSET,"invalid value length or offset","TITLE_ID"},
        {SFO_AGGREGATE,"aggregate value lengths exceed data table","SAVEDATA_LIST_PARAM"},
        {SFO_LONG_KEY,"key exceeds 63 bytes","unrecognized field"},
        {SFO_DUPLICATE,"duplicate key","TITLE_ID"},
        {SFO_STRING_END,"unterminated string value","TITLE_ID"},
        {SFO_TITLE_MISMATCH,"does not match selected backup","TITLE_ID"},
        {SFO_TITLE_FORMAT,"expected string format","TITLE_ID"},
        {SFO_DIRECTORY_MISMATCH,"does not match selected backup","SAVEDATA_DIRECTORY"},
        {SFO_BLOCKS_LOW,"block count outside supported range","SAVEDATA_BLOCKS"},
        {SFO_BLOCKS_HIGH,"block count outside supported range","SAVEDATA_BLOCKS"},
        /* Structurally valid uint64_t: its upper half must not be discarded. */
        {SFO_BLOCKS_UPPER,"block count outside supported range","SAVEDATA_BLOCKS"},
        {SFO_BLOCKS_FORMAT,"expected 8-byte integer","SAVEDATA_BLOCKS"},
        {SFO_BLOCKS_SHORT,"expected 8-byte integer","SAVEDATA_BLOCKS"},
        {SFO_BLOCKS_LONG,"expected 8-byte integer","SAVEDATA_BLOCKS"},
        {SFO_BLOCKS_MAX_MISMATCH,"expected 8-byte integer","SAVEDATA_BLOCKS"},
        {SFO_BLOCKS_VALUE_BOUNDS,"invalid value length or offset","SAVEDATA_BLOCKS"},
        {SFO_ACCOUNT_SIZE,"expected 8-byte value","ACCOUNT_ID"},
        {SFO_PARAMS_SIZE,"value shorter than title identity structure","PARAMS"},
        {SFO_PARAMS_TITLE,"embedded title ID does not match selected backup","PARAMS"},
        {SFO_DETAIL_FORMAT,"expected string format","DETAIL"},
        {SFO_LIST_FORMAT,"expected 4-byte integer","SAVEDATA_LIST_PARAM"},
        {SFO_MISSING_LIST_PARAM,"required field missing","SAVEDATA_LIST_PARAM"},
        {SFO_FORMAT_NO_TERMINATOR,"unterminated string value","unrecognized field"},
        {SFO_FORMAT_MAX_BOUNDS,"invalid value length or offset","unrecognized field"},
        {SFO_UNKNOWN_LENGTH,"invalid value length or offset","unrecognized field"},
        {SFO_UNKNOWN_OFFSET,"invalid value length or offset","unrecognized field"}
    };
    for(unsigned i=0;i<sizeof(bad_sfo)/sizeof(bad_sfo[0]);i++) {
        reset();sfo_case=bad_sfo[i].kind;fixture(NULL,0100000,1);
        assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED);
        assert(strstr(backup.diagnostic,"Google restore failed [op=7 errno=22 zip=0]")&&
            strstr(backup.diagnostic,bad_sfo[i].check)&&strstr(backup.diagnostic,"Download retained"));
        char expected_field[80];snprintf(expected_field,sizeof(expected_field),"field=%s",bad_sfo[i].field);
        assert(strstr(backup.diagnostic,expected_field));
        assert(access(backup.archive,F_OK)==0&&!imports&&!absent_calls);
    }
    reset();fixture(NULL,0100000,0);
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED);
    assert(strstr(backup.diagnostic,"Google restore failed [op=4")&&
        strstr(backup.diagnostic,"Download retained"));
    assert(access(backup.archive,F_OK)==0&&!imports&&!absent_calls);

    const char *bad[]={"OTHER/root.bin","SAVE/../escape","/absolute","SAVE/link\\escape"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
        reset();fixture(bad[i],0100000,1);assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED);
        assert(!imports&&access(backup.archive,F_OK)==0);
    }
    reset();fixture("SAVE/symlink",0120000,1);
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED&&!imports&&access(backup.archive,F_OK)==0);
    reset();fixture(NULL,0100000,0);
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED&&!imports&&access(backup.archive,F_OK)==0);
    /* An occupied source/target slot is refused before staging, and a racing
       target is refused after staging. Neither failure consumes the ZIP. */
    for (int check=1;check<=2;check++) {
        reset();fixture(NULL,0100000,1);target_reject_at=check;target_status=0;
        assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED&&!imports&&absent_calls==check);
        char expected[96];snprintf(expected,sizeof(expected),"op=%d errno=%d zip=0",check==1?8:9,EEXIST);
        assert(strstr(backup.diagnostic,expected)&&strstr(backup.diagnostic,"Save already exists")&&
            strstr(backup.diagnostic,"Download retained")&&access(backup.archive,F_OK)==0);
        char stage[320];snprintf(stage,sizeof(stage),"%s/stage",backup.temp_dir);
        assert(access(stage,F_OK)!=0);
    }
    reset();fixture(NULL,0100000,1);target_reject_at=1;target_status=-1;
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED&&!imports&&absent_calls==1);
    char unknown[96];snprintf(unknown,sizeof(unknown),"op=8 errno=%d zip=0",EIO);
    assert(strstr(backup.diagnostic,unknown)&&
        strstr(backup.diagnostic,"Save target could not be checked")&&
        !strstr(backup.diagnostic,"Save already exists")&&access(backup.archive,F_OK)==0);
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
