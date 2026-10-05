/* CI libc has ENOSYS lstat/fstatat/mkdirat, but libkernel_sys exports
   native BSD implementations. Model symbol resolution, not a stat fallback. */
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include "restore_fs.h"
#include <orbis/libkernel.h>
int sdk_failure, sdk_stub_calls, sdk_native_calls, sdk_operation_failure;
int __real_lstat(const char *, struct stat *);
int __real_mkdirat(int, const char *, mode_t);
int __wrap_lstat(const char *p, struct stat *s) {
    (void)p; (void)s; sdk_stub_calls++; errno=ENOSYS; return -1;
}
int __wrap_fstatat(int fd, const char *p, struct stat *s, int flags) {
    (void)fd; (void)p; (void)s; (void)flags; sdk_stub_calls++; errno=ENOSYS; return -1;
}
int __wrap_mkdirat(int fd, const char *p, mode_t mode) {
    (void)fd; (void)p; (void)mode; sdk_stub_calls++; errno=ENOSYS; return -1;
}
static int kernel_lstat(const char *p, struct stat *s) {
    if (sdk_operation_failure==1) { errno=ENOSYS; return -1; }
    sdk_native_calls++; return __real_lstat(p,s);
}
static int kernel_mkdirat(int fd, const char *p, mode_t mode) {
    if (sdk_operation_failure==2) { errno=ENOSYS; return -1; }
    sdk_native_calls++; return __real_mkdirat(fd,p,mode);
}
int sceKernelLoadStartModule(const char *p,int a,void *b,int c,void *d,void *e) {
    (void)a; (void)b; (void)c; (void)d; (void)e;
    assert(!strcmp(p,"/system/common/lib/libkernel_sys.sprx"));
    assert(sdk_failure==1); return -1234;
}
int sceKernelGetModuleList(OrbisKernelModule *modules,size_t capacity,size_t *count) {
    assert(capacity==256);
    if (sdk_failure==5) return -9012;
    *count=sdk_failure==1?0:1;
    modules[0]=7; return 0;
}
int sceKernelGetModuleInfo(OrbisKernelModule module,OrbisKernelModuleInfo *info) {
    assert(module==7 && info->size==sizeof(*info));
    if (sdk_failure==6) return -9012;
    strcpy(info->name,"libkernel_sys"); return 0;
}
int sceKernelDlsym(int module,const char *name,void **out) {
    assert(module==7);
    if (sdk_failure==2 || (sdk_failure==3 && !strcmp(name,"mkdirat"))) return -5678;
    if (sdk_failure==4) { *out=NULL; return 0; }
    if (!strcmp(name,"lstat")) *out=(void*)kernel_lstat;
    else { assert(!strcmp(name,"mkdirat")); *out=(void*)kernel_mkdirat; }
    return 0;
}
