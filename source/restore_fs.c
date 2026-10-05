#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include "restore_fs.h"
#ifdef __PS4__
#include <orbis/libkernel.h>
static int (*native_lstat)(const char *, struct stat *);
static int (*native_mkdirat)(int, const char *, mode_t);
int restore_fs_init(int *native_error)
{
    *native_error=0;
    if (native_lstat && native_mkdirat) return 1;
    /* libkernel_sys supplies the real BSD calls. Linking libc lstat/mkdirat
       instead selects musl's unconditional ENOSYS implementations. */
    /* loadPrivLibs normally loaded this already. Reuse its handle instead of
       relying on repeated load/start behavior on a particular firmware. */
    OrbisKernelModule modules[256]; size_t count=0;
    int module=-1, code=sceKernelGetModuleList(modules,256,&count);
    if (code || count>256) { *native_error=code; errno=ENOSYS; return 0; }
    for (size_t i=0;i<count;i++) {
        OrbisKernelModuleInfo info; memset(&info,0,sizeof(info)); info.size=sizeof(info);
        code=sceKernelGetModuleInfo(modules[i],&info);
        if (code) { *native_error=code; errno=ENOSYS; return 0; }
        info.name[sizeof(info.name)-1]=0;
        if (!strcmp(info.name,"libkernel_sys") || !strcmp(info.name,"libkernel_sys.sprx")) {
            module=(int)modules[i]; break;
        }
    }
    if (module<0) module=(int)sceKernelLoadStartModule("/system/common/lib/libkernel_sys.sprx",0,NULL,0,NULL,NULL);
    if (module<0) { *native_error=module; errno=ENOSYS; return 0; }
    void *ls=NULL, *mk=NULL;
    code=sceKernelDlsym(module,"lstat",&ls);
    if (!code) code=sceKernelDlsym(module,"mkdirat",&mk);
    if (code || !ls || !mk) { *native_error=code; errno=ENOSYS; return 0; }
    native_lstat=(int (*)(const char *, struct stat *))ls;
    native_mkdirat=(int (*)(int, const char *, mode_t))mk;
    return 1;
}
int restore_fs_lstat(const char *p, struct stat *s)
{
    if (!native_lstat) { errno=ENOSYS; return -1; }
    return native_lstat(p,s);
}
int restore_fs_mkdirat(int fd, const char *p, mode_t mode)
{
    if (!native_mkdirat) { errno=ENOSYS; return -1; }
    return native_mkdirat(fd,p,mode);
}
#else
int restore_fs_init(int *native_error) { *native_error=0; return 1; }
int restore_fs_lstat(const char *p, struct stat *s) { return lstat(p,s); }
int restore_fs_mkdirat(int fd, const char *p, mode_t mode) { return mkdirat(fd,p,mode); }
#endif
