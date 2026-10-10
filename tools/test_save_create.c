/* Real O_EXCL/filesystem behavior; platform crypto/image generation are mocked. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include "sd.h"
static int mode, sync_calls, close_calls, kernel_writes;
static const char *volume="build/host/create/volume", *key="build/host/create/key";
static int mock_open(const char *path,int flags,...) {
    if (!strcmp(path,"/dev/sbl_srv")) return 10000;
    return open(path,flags,0600);
}
static int mock_close(int fd) { return fd==10000?0:close(fd); }
static int mock_ioctl(int fd,unsigned long request,...) {
    assert(fd==10000); va_list ap; va_start(ap,request);
    unsigned char *buffer=va_arg(ap,unsigned char*); va_end(ap);
    memset(buffer,0x42,request==0xc0845302?ENC_SEALEDKEY_LEN+DEC_SEALEDKEY_LEN:ENC_SEALEDKEY_LEN); return 0;
}
#define open mock_open
#define close mock_close
#define ioctl mock_ioctl
#include "../source/sd.c"
#undef open
#undef close
#undef ioctl
int sceKernelLoadStartModule(const char *p,int a,void *b,int c,void *d,void *e) { (void)p;(void)a;(void)b;(void)c;(void)d;(void)e;return -1; }
int sceKernelDlsym(int a,const char *b,void **c) { (void)a;(void)b;(void)c;return -1; }
int sceKernelOpen(const char *path,int flags,int permissions) {
    (void)permissions;
    if (mode==8) return -1234;
    if ((mode==1 && !strcmp(path,volume)) || (mode==2 && !strcmp(path,key))) {
        int existing=open(path,O_WRONLY|O_CREAT|O_EXCL,0600); assert(existing>=0);
        assert(write(existing,"existing",8)==8); assert(!close(existing));
    }
    return open(path,flags,0600);
}
int64_t sceKernelWrite(int fd,const void *p,size_t n) { kernel_writes++; return mode==3?0:write(fd,p,n); }
int sceKernelFsync(int fd) { sync_calls++; return mode==4 || (mode==6 && sync_calls==2)?-1:fsync(fd); }
int sceKernelClose(int fd) { close_calls++; int result=close(fd); return mode==5 || (mode==7 && close_calls==3)?-1:result; }
static int allocate(int fd,uint64_t size,uint64_t flags,int ext) { (void)flags;(void)ext; assert(size==((uint64_t)131072<<15)); return mode==9?-777:ftruncate(fd,128); }
static int init(CreatePfsSaveDataOpt *opt) { (void)opt; return 0; }
static int image(CreatePfsSaveDataOpt *opt,const char *path,int id,uint64_t size,uint8_t *secret) {
    (void)opt;(void)id;(void)size;(void)secret;
    int fd=open(path,O_WRONLY); assert(fd>=0); assert(write(fd,"new image",9)==9); return close(fd);
}
static void unchanged(const char *path) {
    int fd=open(path,O_RDONLY); assert(fd>=0); char buffer[9]={0};
    assert(read(fd,buffer,8)==8 && !strcmp(buffer,"existing")); assert(!close(fd));
}
int main(void) {
    mkdir("build/host/create",0700);
    sceFsUfsAllocateSaveData=allocate; sceFsInitCreatePfsSaveDataOpt=init; sceFsCreatePfsSaveDataImage=image;
    for (mode=0;mode<=9;mode++) {
        unlink(key); unlink(volume); sync_calls=close_calls=kernel_writes=0;
        int result=createSaveEmpty(volume,key,131072);
        assert(mode==0?result==0:result<0);
        if (mode==1) unchanged(volume);
        if (mode==2) { unchanged(key); assert(!kernel_writes); }
        if (mode==8) assert(!kernel_writes && !close_calls);
        if (mode==9) {
            char detail[192]; google_restore_last_failure(detail,sizeof(detail));
            assert(strstr(detail,"native=-777 call=sceFsUfsAllocateSaveData"));
            FILE *log=fopen("build/host/google_restore.log","rb");assert(log);
            char line[384];int found=0;
            while(fgets(line,sizeof(line),log))
                if(strstr(line,"stage=save image allocation op=10")&&
                    strstr(line,"native=-777 call=sceFsUfsAllocateSaveData result=failed"))found=1;
            assert(!fclose(log)&&found);
        }
    }
    unlink(key); unlink(volume); rmdir("build/host/create");
    puts("Exclusive save creation tests passed (racing key/volume, negative descriptors, write/sync/close failures, 64-bit sizing).");
    return 0;
}
