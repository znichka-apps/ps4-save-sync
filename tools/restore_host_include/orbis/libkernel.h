#include <stddef.h>
#include <stdint.h>
int sceKernelOpen(const char*,int,int);
int64_t sceKernelWrite(int,const void*,size_t);
int sceKernelClose(int);
int sceKernelFsync(int);
int sceKernelLoadStartModule(const char*,int,void*,int,void*,void*);
int sceKernelDlsym(int,const char*,void**);
typedef uint32_t OrbisKernelModule;
typedef struct { size_t size; char name[256]; } OrbisKernelModuleInfo;
int sceKernelGetModuleList(OrbisKernelModule*,size_t,size_t*);
int sceKernelGetModuleInfo(OrbisKernelModule,OrbisKernelModuleInfo*);
