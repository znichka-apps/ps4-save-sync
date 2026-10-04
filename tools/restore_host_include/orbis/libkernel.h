#include <stddef.h>
#include <stdint.h>
int sceKernelOpen(const char*,int,int);
int64_t sceKernelWrite(int,const void*,size_t);
int sceKernelClose(int);
int sceKernelFsync(int);
int sceKernelLoadStartModule(const char*,int,void*,int,void*,void*);
int sceKernelDlsym(int,const char*,void**);
