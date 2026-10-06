#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sqlite3.h>
#include <zip.h>
#include "saves.h"
#include "settings.h"
#include "google_restore.h"

app_config_t apollo_config={.user_id=42};
static google_backup backup;
static int stop, mode, mounts, patches, details_calls, unmounts, writes, archive_stats, fdopens, absent_checks;
#ifdef GOOGLE_RESTORE_OPENAT_PROBE
static int probe_openat_calls, probe_injected_failure;
static int probe_openat_flags[4];
static int probe_path_open_calls, probe_path_open_flags;
static int probe_run_active;
static int probe_path_fd=-1, probe_case;
#endif
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
        if (i==2 || i==3) { len=8; format=4; }
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
static int absent(void *p,const google_backup *b) { (void)p; absent_checks++; assert(b->user==42); return orbis_SaveTargetAbsent(&entry,b->user); }
static int mount_new(void *p,const google_backup *b,uint32_t blocks,char *out,size_t size) {
    (void)p; (void)b; assert(blocks==96); mounts++;
    if (mode==5) return 0;
    assert(!mkdir(target,0700)); snprintf(out,size,"%s",target);
    if (mode==7) { assert(!symlink("../restore-sentinel","build/host/restore-mount/sub")); }
    if (mode==9) stop=1;
    if (mode==11) { assert(!mkdir("build/host/restore-mount/sub",0700)); assert(!symlink("../../restore-sentinel/data.bin","build/host/restore-mount/sub/data.bin")); }
    if (mode==12) { assert(!mkdir("build/host/restore-mount/sub",0700)); assert(!mkfifo("build/host/restore-mount/sub/data.bin",0600)); }
    if (mode==13) { assert(!mkdir("build/host/restore-mount/sub",0700)); assert(!link("build/host/restore-sentinel/data.bin","build/host/restore-mount/sub/data.bin")); }
    return 1;
}
static int ownership(void *p,const char *path) { (void)p; assert(!strcmp(path,target)); patches++; return mode!=2; }
static int details(void *p,const google_backup *b,const char *path) { (void)p;(void)b;(void)path; details_calls++; return mode!=6; }
static int unmount(void *p) { (void)p; unmounts++; if (mode==8) stop=1; return mode!=3; }
static int finish(void *p) { (void)p; if (mode==10) { stop=1; return 0; } return !stop; }
ssize_t __real_write(int,const void*,size_t);
int __real_fstat(int,struct stat*);
int __wrap_fstat(int fd,struct stat *s) {
#ifdef GOOGLE_RESTORE_OPENAT_PROBE
    if (probe_run_active && fd==probe_path_fd && probe_case==2) { errno=EIO; return -1; }
#endif
    if (mode==14 && mounts) { errno=EIO; return -1; }
    int result=__real_fstat(fd,s);
#ifdef GOOGLE_RESTORE_OPENAT_PROBE
    if (!result && probe_run_active && fd==probe_path_fd && probe_case==3) s->st_ino++;
    if (!result && probe_run_active && fd==probe_path_fd && probe_case==4) s->st_mode=(s->st_mode&~S_IFMT)|S_IFIFO;
#endif
    if (!result && S_ISREG(s->st_mode) && !mounts) {
        if (mode==23) { errno=EBADF; return -1; }
        if (mode==24) s->st_mode=(s->st_mode&~S_IFMT)|S_IFIFO;
        if (mode==25) s->st_uid++;
        if (mode==26) s->st_nlink=2;
        if (mode>=24 && mode<=26) errno=ERANGE; /* Must not leak stale errno. */
    }
    if (!result && S_ISREG(s->st_mode) && !mounts && ++archive_stats==2 && (mode==18 || mode==19)) {
        char moved[320]; snprintf(moved,sizeof(moved),"%s.moved",mode==18?backup.archive:backup.temp_dir);
        if (mode==18) {
            assert(!rename(backup.archive,moved));
            FILE *in=fopen(moved,"rb"), *out=fopen(backup.archive,"wb"); assert(in && out);
            unsigned char bytes[4096]; size_t n;
            while ((n=fread(bytes,1,sizeof(bytes),in))) assert(fwrite(bytes,1,n,out)==n);
            assert(!ferror(in) && !fclose(in) && !fclose(out));
        } else {
            assert(!rename(backup.temp_dir,moved));
            const char *base=strrchr(moved,'/'); assert(base);
            assert(!symlink(base+1,backup.temp_dir));
        }
    }
    return result;
}
int __real_ftruncate(int,off_t);
int __wrap_ftruncate(int fd,off_t size) {
    if (mode==15) { errno=EIO; return -1; }
    return __real_ftruncate(fd,size);
}
int __real_openat(int,const char*,int,...);
int __wrap_openat(int fd,const char *p,int flags,...) {
#ifdef GOOGLE_RESTORE_OPENAT_PROBE
    if (!mounts && !strcmp(p,"backup.zip")) {
        static const int expected[]={
            O_RDONLY|O_NOFOLLOW|O_NONBLOCK,
            O_RDONLY|O_NOFOLLOW,
            O_RDONLY|O_NONBLOCK,
            O_RDONLY
        };
        assert(probe_openat_calls<4);
        assert(flags==expected[probe_openat_calls]);
        probe_openat_flags[probe_openat_calls]=flags;
        int probe_index=probe_openat_calls++;
        if (probe_injected_failure==-2 || probe_index==probe_injected_failure) { errno=EINVAL; return -1; }
    }
#else
    assert(flags&O_NOFOLLOW);
    if (!mounts && !strcmp(p,"backup.zip")) {
        assert(flags==(O_RDONLY|O_NOFOLLOW|O_NONBLOCK));
        if (mode==22) { errno=EINVAL; return -1; }
    }
#endif
    mode_t permissions=0;
    if (flags&O_CREAT) { va_list args; va_start(args,flags); permissions=va_arg(args,int); va_end(args); }
    if (mode==16 && mounts) { errno=EIO; return -1; }
    return __real_openat(fd,p,flags,permissions);
}
#ifdef GOOGLE_RESTORE_OPENAT_PROBE
int __real_open(const char*,int,...);
int __wrap_open(const char *p,int flags,...) {
    if (probe_run_active && !strcmp(p,backup.archive)) {
        assert(flags==(O_RDONLY|O_NOFOLLOW|O_NONBLOCK));
        probe_path_open_calls++; probe_path_open_flags=flags;
        if (probe_case==1) { errno=EINVAL; return -1; }
        probe_path_fd=__real_open(p,flags);
        if (probe_case==5) assert(!chmod(backup.temp_dir,0755));
        if (probe_case==6) assert(!chmod(backup.archive,0400));
        return probe_path_fd;
    }
    mode_t permissions=0;
    if (flags&O_CREAT) { va_list args; va_start(args,flags); permissions=va_arg(args,int); va_end(args); }
    return __real_open(p,flags,permissions);
}
#endif
int __real_fsync(int);
int __wrap_fsync(int fd) {
    if (mode==17) { errno=EIO; return -1; }
    return __real_fsync(fd);
}
zip_t *__real_zip_fdopen(int,int,int*);
zip_t *__wrap_zip_fdopen(int fd,int flags,int *error) {
    fdopens++;
    if (mode==20 || (mode==21 && fdopens==2)) { *error=ZIP_ER_OPEN; errno=EBADF; return NULL; }
    return __real_zip_fdopen(fd,flags,error);
}
ssize_t __wrap_write(int fd,const void *p,size_t n) {
    writes++; if (mode==1) { errno=ENOSPC; return -1; }
    ssize_t result=__real_write(fd,p,n); if (mode==4) stop=1; return result;
}
static void reset_target(void) {
    char link_byte;
    if (readlink("build/host/restore-mount/sub",&link_byte,1)<0) unlink("build/host/restore-mount/sub/data.bin");
    unlink("build/host/restore-mount/sub");
    rmdir("build/host/restore-mount/sub"); unlink("build/host/restore-mount/sce_sys/param.sfo");
    rmdir("build/host/restore-mount/sce_sys"); rmdir(target);
    mounts=patches=details_calls=unmounts=writes=stop=mode=archive_stats=fdopens=absent_checks=0; backup.mount_blocked=0;
}
static void hash_fixture(void) { assert(google_backup_hash(&backup,cancel,NULL)); }
int main(void) {
#ifdef __PS4__
    extern int sdk_failure, sdk_stub_calls, sdk_native_calls;
    google_restore_io initial_io={NULL,cancel,absent,mount_new,ownership,details,unmount,finish};
    for (sdk_failure=1;sdk_failure<=7;sdk_failure++) {
        assert(google_restore_run(&backup,&initial_io)==GOOGLE_UPLOAD_FAILED);
        assert(!mounts && !writes && strstr(backup.diagnostic,"op=1") && strstr(backup.diagnostic,"errno="));
        assert(strstr(backup.diagnostic,sdk_failure==1?"native=-1234":sdk_failure==4?"native=0":sdk_failure>=5 && sdk_failure<=6?"native=-9012":"native=-5678"));
    }
    sdk_failure=0;
#endif
    mkdir("build/host/cache",0700); mkdir("build/host/restore-users",0700);
    mkdir("build/host/restore-users/0000002a",0700); mkdir("build/host/restore-users/0000002a/CUSA12345",0700);
    mkdir("build/host/restore-sentinel",0700);
    FILE *sentinel=fopen("build/host/restore-sentinel/data.bin","wb"); assert(sentinel);
    assert(fputs("unchanged",sentinel)>=0 && !fclose(sentinel));
    sqlite3 *db; assert(sqlite3_open("build/host/restore-users/0000002a.db",&db)==SQLITE_OK);
    assert(sqlite3_exec(db,"DROP TABLE IF EXISTS savedata; CREATE TABLE savedata(title_id TEXT, dir_name TEXT)",NULL,NULL,NULL)==SQLITE_OK);
    sqlite3_close(db);
    strcpy(backup.temp_dir,GOOGLE_BACKUP_CACHE "drive-XXXXXX"); assert(mkdtemp(backup.temp_dir));
    snprintf(backup.archive,sizeof(backup.archive),"%s/backup.zip",backup.temp_dir);
    strcpy(backup.title,"CUSA12345"); strcpy(backup.directory,"SAVE"); backup.user=42;
    google_restore_io io={NULL,cancel,absent,mount_new,ownership,details,unmount,finish};
#ifdef GOOGLE_RESTORE_OPENAT_PROBE
    fixture("CUSA12345","SAVE",NULL,0100000,0); hash_fixture();
    probe_openat_calls=0; probe_injected_failure=0; probe_path_open_calls=0; probe_run_active=1;
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED);
    probe_run_active=0;
    assert(probe_openat_calls==4 && probe_path_open_calls==1);
    assert(probe_path_open_flags==(O_RDONLY|O_NOFOLLOW|O_NONBLOCK));
    assert(!absent_checks && !mounts && !writes && !patches && !details_calls && !unmounts);
    fprintf(stderr,"Probe diagnostic fixture: %s\n",backup.diagnostic);
    const int expected_probe_flags[]={
        O_RDONLY|O_NOFOLLOW|O_NONBLOCK,
        O_RDONLY|O_NOFOLLOW,
        O_RDONLY|O_NONBLOCK,
        O_RDONLY
    };
    for (unsigned i=0;i<4;i++) assert(probe_openat_flags[i]==expected_probe_flags[i]);
    char probe_record[48];
    snprintf(probe_record,sizeof(probe_record),"a0=0x%x:-1/%d/0/0",(unsigned)expected_probe_flags[0],EINVAL);
    assert(strstr(backup.diagnostic,probe_record));
    for (unsigned i=1;i<4;i++) {
        snprintf(probe_record,sizeof(probe_record),"a%u=0x%x:",i,(unsigned)expected_probe_flags[i]);
        assert(strstr(backup.diagnostic,probe_record));
    }
    assert(strstr(backup.diagnostic,"PROBE pfd="));
    snprintf(probe_record,sizeof(probe_record),"p=0x%x:",O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    assert(strstr(backup.diagnostic,probe_record));
    assert(strstr(backup.diagnostic,"s=0/0 c=0/0 d0=0/0 b0=0/0 d1=0/0 b1=0/0"));
    assert(!strstr(backup.diagnostic,backup.archive) && !strstr(backup.diagnostic,"restored content"));
    assert(!access(backup.archive,R_OK));
    assert(fcntl(probe_path_fd,F_GETFD)==-1 && errno==EBADF);
    for (probe_case=1;probe_case<=6;probe_case++) {
        assert(!chmod(backup.temp_dir,0700) && !chmod(backup.archive,0600));
        probe_openat_calls=probe_path_open_calls=0; probe_path_fd=-1;
        probe_injected_failure=-2; /* Match console: every openat returns EINVAL. */
        probe_run_active=1;
        assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED);
        probe_run_active=0;
        assert(probe_openat_calls==4 && probe_path_open_calls==1);
        assert(!absent_checks && !mounts && !writes && !patches && !details_calls && !unmounts && !fdopens);
        if (probe_case<=4) assert(strstr(backup.diagnostic,"d0=0/0 b0=0/0 d1=0/0 b1=0/0"));
        if (probe_case<=4) assert(strstr(backup.diagnostic,probe_case==1?"-1/22 s=-2/0 c=-2/0":probe_case==2?"s=-1/5 c=0/0":"s=-1/22 c=0/0"));
        if (probe_case==5) assert(strstr(backup.diagnostic,"d1=-1/22 b1=0/0"));
        if (probe_case==6) assert(strstr(backup.diagnostic,"b1=-1/22"));
        if (probe_path_fd>=0) assert(fcntl(probe_path_fd,F_GETFD)==-1 && errno==EBADF);
        assert(!strstr(backup.diagnostic,backup.archive));
    }
    probe_case=0;
    assert(!chmod(backup.temp_dir,0700) && !chmod(backup.archive,0600));
    char original_md5[33]; strcpy(original_md5,backup.md5);
    hash_fixture(); /* All read-only probe cases preserved archive bytes. */
    assert(!strcmp(original_md5,backup.md5));
    assert(google_backup_cleanup(&backup));
    puts("Temporary restore probe tests passed (no-save checks, four openat variants, protected full-path open, descriptor identity, immediate close, no save activity).");
    return 0;
#endif
    reset_target(); fixture("CUSA12345","SAVE",NULL,0100000,0); hash_fixture();
    assert(absent(NULL,&backup)==1);
    int first=google_restore_run(&backup,&io);
    if (first!=GOOGLE_UPLOAD_SUCCESS) fprintf(stderr,"Restore: %s\n",backup.diagnostic);
    assert(first==GOOGLE_UPLOAD_SUCCESS);
    assert(mounts==1 && patches==1 && details_calls==1 && unmounts==1);
    char data[17]={0}; FILE *f=fopen("build/host/restore-mount/sub/data.bin","rb"); assert(f);
    assert(fread(data,1,16,f)==16 && !strcmp(data,"restored content")); assert(!fclose(f));
    assert(!access(backup.archive,R_OK)); reset_target();
    /* Each op=3 branch identifies only a fixed operation and numeric codes.
       Exact strings also ensure no archive path/content enters diagnostics. */
    const char *archive_calls[]={"openat","fstat","type","owner","link"};
    for (int failure=22;failure<=26;failure++) {
        reset_target(); mode=failure;
        assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED);
        char expected[160];
        snprintf(expected,sizeof(expected),"Archive unavailable. [op=3 call=%s errno=%d native=0 zip=0]",
            archive_calls[failure-22],failure==23?EBADF:EINVAL);
        assert(!strcmp(backup.diagnostic,expected));
        assert(!mounts && !writes && !patches && !details_calls && !unmounts);
        assert(!access(backup.archive,R_OK));
    }
    reset_target();
#ifdef __PS4__
    extern int sdk_operation_failure;
    for (sdk_operation_failure=1;sdk_operation_failure<=2;sdk_operation_failure++) {
        assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED);
        assert(mounts==(sdk_operation_failure==2));
        assert(strstr(backup.diagnostic,sdk_operation_failure==1?"op=5":"op=102"));
        char error[24]; snprintf(error,sizeof(error),"errno=%d",ENOSYS); assert(strstr(backup.diagnostic,error));
        reset_target();
    }
    sdk_operation_failure=0;
#endif
    /* Private path/type/ownership policy fails before target creation. */
    assert(!chmod(backup.temp_dir,0777));
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED && !mounts && !writes);
    assert(strstr(backup.diagnostic,"op=2")); assert(!chmod(backup.temp_dir,0700));
    char moved[320]; snprintf(moved,sizeof(moved),"%s.moved",backup.archive);
    assert(!rename(backup.archive,moved)); assert(!symlink("backup.zip.moved",backup.archive));
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED && !mounts && !writes);
    assert(strstr(backup.diagnostic,"op=3")); assert(!unlink(backup.archive)); assert(!rename(moved,backup.archive));
    assert(!link(backup.archive,moved));
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED && !mounts && !writes);
    assert(strstr(backup.diagnostic,"op=3")); assert(!unlink(moved));
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
    for (int failure=1;failure<=17;failure++) {
        reset_target(); mode=failure;
        int result=google_restore_run(&backup,&io);
        assert(result!=GOOGLE_UPLOAD_SUCCESS && !access(backup.archive,R_OK));
        assert(mounts==1 && unmounts==(failure==5?0:1));
        assert(backup.mount_blocked==(failure==3));
        if (failure==4 || failure==8 || failure==9 || failure==10) assert(result==GOOGLE_UPLOAD_CANCELLED);
        if (failure==2) assert(strstr(backup.diagnostic,"Ownership"));
        if (failure==3) assert(strstr(backup.diagnostic,"Unmount"));
        if (failure==7 || failure==11 || failure==12 || failure==13) {
            char contents[10]={0}; f=fopen("build/host/restore-sentinel/data.bin","rb"); assert(f);
            assert(fread(contents,1,9,f)==9 && !strcmp(contents,"unchanged") && !fclose(f));
        }
        if (failure>=14) {
            char operation[20]; snprintf(operation,sizeof(operation),"op=%d",failure==14?105:failure==15?106:failure==16?103:108);
            assert(strstr(backup.diagnostic,operation));
            char error[24]; snprintf(error,sizeof(error),"errno=%d",EIO); assert(strstr(backup.diagnostic,error));
        }
    }
    reset_target(); stop=1;
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_CANCELLED && mounts==0);
    for (int race=18;race<=19;race++) {
        reset_target(); mode=race;
        assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED && !mounts && !writes);
        assert(strstr(backup.diagnostic,"op=5"));
        snprintf(moved,sizeof(moved),"%s.moved",race==18?backup.archive:backup.temp_dir);
        assert(!unlink(race==18?backup.archive:backup.temp_dir));
        assert(!rename(moved,race==18?backup.archive:backup.temp_dir));
    }
    for (int failure=20;failure<=21;failure++) {
        reset_target(); mode=failure;
        assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED && !mounts && !writes);
        assert(strstr(backup.diagnostic,failure==20?"op=4":"op=6"));
        char code[24]; snprintf(code,sizeof(code),"zip=%d",ZIP_ER_OPEN); assert(strstr(backup.diagnostic,code));
        snprintf(code,sizeof(code),"errno=%d",EBADF); assert(strstr(backup.diagnostic,code));
    }
    reset_target();
    stop=0; /* Changes after download must fail the second hash before creation. */
    f=fopen(backup.archive,"ab"); assert(f); assert(fputc('x',f)!=EOF); assert(!fclose(f));
    assert(google_restore_run(&backup,&io)==GOOGLE_UPLOAD_FAILED && mounts==0);
    assert(google_backup_cleanup(&backup));
#ifdef __PS4__
    assert(!sdk_stub_calls && sdk_native_calls>0);
#endif
    puts("Google restore host tests passed (empty/existing targets, SFO, paths, writes, cancellation, ownership, details, unmount).");
    return 0;
}
