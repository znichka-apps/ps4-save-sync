#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zip.h>
#include "save_zip.h"

static const char *inject;
static int unknown_type, cancel_now, cancel_on_close;
static int is(const char *op) { return inject && !strcmp(inject, op); }
static int zip_fail(zip_t *z) {
    zip_error_set(zip_get_error(z), ZIP_ER_MEMORY, 0); errno = EIO; return -1;
}
zip_t *__real_zip_open(const char*, int, int*);
zip_t *__wrap_zip_open(const char *p, int f, int *e) {
    if (is("zip_open")) { if (e) *e = ZIP_ER_OPEN; errno = EACCES; return NULL; }
    return __real_zip_open(p,f,e);
}
int __real_zip_register_cancel_callback_with_state(zip_t*,zip_cancel_callback,void (*)(void*),void*);
int __wrap_zip_register_cancel_callback_with_state(zip_t *z,zip_cancel_callback cb,void (*free_cb)(void*),void *p) {
    return is("cancellation callback registration") ? zip_fail(z) : __real_zip_register_cancel_callback_with_state(z,cb,free_cb,p);
}
DIR *__real_opendir(const char*);
DIR *__wrap_opendir(const char *p) {
    if (is("opendir")) { errno=EACCES; return NULL; } return __real_opendir(p);
}
struct dirent *__real_readdir(DIR*);
struct dirent *__wrap_readdir(DIR *d) {
    if (is("readdir")) { errno=EIO; return NULL; }
    struct dirent *e=__real_readdir(d); if (e && unknown_type) e->d_type=DT_UNKNOWN; return e;
}
int __real_lstat(const char*,struct stat*);
int __wrap_lstat(const char *p,struct stat *s) {
    if (is("lstat")) { errno=ENOSYS; return -1; } return __real_lstat(p,s);
}
zip_int64_t __real_zip_add_dir(zip_t*,const char*);
zip_int64_t __wrap_zip_add_dir(zip_t *z,const char *p) {
    return is("zip_add_dir") ? zip_fail(z) : __real_zip_add_dir(z,p);
}
zip_source_t *__real_zip_source_file(zip_t*,const char*,zip_uint64_t,zip_int64_t);
zip_source_t *__wrap_zip_source_file(zip_t *z,const char *p,zip_uint64_t s,zip_int64_t n) {
    if (is("zip_source_file")) { zip_fail(z); return NULL; } return __real_zip_source_file(z,p,s,n);
}
zip_int64_t __real_zip_add(zip_t*,const char*,zip_source_t*);
zip_int64_t __wrap_zip_add(zip_t *z,const char *p,zip_source_t *s) {
    return is("zip_add") ? zip_fail(z) : __real_zip_add(z,p,s);
}
int __real_zip_file_set_external_attributes(zip_t*,zip_uint64_t,zip_flags_t,zip_uint8_t,zip_uint32_t);
int __wrap_zip_file_set_external_attributes(zip_t *z,zip_uint64_t i,zip_flags_t f,zip_uint8_t o,zip_uint32_t a) {
    return is("external attributes") ? zip_fail(z) : __real_zip_file_set_external_attributes(z,i,f,o,a);
}
int __real_closedir(DIR*);
int __wrap_closedir(DIR *d) {
    int result=__real_closedir(d);
    if (is("closedir")) { errno=EIO; return -1; }
    /* Deliberately clobber errno on cleanup: the first diagnostic must survive. */
    errno=EBADF; return result;
}
int __real_zip_close(zip_t*);
int __wrap_zip_close(zip_t *z) {
    if (cancel_on_close) cancel_now=1;
    return is("zip_close") ? zip_fail(z) : __real_zip_close(z);
}
static int cancelled(void *p) { (void)p; return cancel_now; }

int main(void)
{
    char base[]="build/host/zip-test-XXXXXX"; assert(mkdtemp(base));
    char input[256], file[256], output[256], link[256], diagnostic[SAVE_ZIP_DIAGNOSTIC_SIZE];
    snprintf(input,sizeof(input),"%s/save/",base); assert(!mkdir(input,0700));
    snprintf(file,sizeof(file),"%s/save/data",base);
    snprintf(link,sizeof(link),"%s/save/link",base);
    snprintf(output,sizeof(output),"%s/result.zip",base);
    FILE *fp=fopen(file,"wb"); assert(fp); assert(fputs("source unchanged",fp)>=0); assert(!fclose(fp));
    const char *operations[]={"zip_open","cancellation callback registration","opendir","readdir",
        "zip_add_dir","zip_source_file","zip_add","external attributes","closedir","zip_close","lstat"};
    for (size_t i=0;i<sizeof(operations)/sizeof(*operations);i++) {
        inject=operations[i]; unknown_type=is("lstat");
        assert(!zip_directory_diagnostic(base,input,output,cancelled,NULL,diagnostic,sizeof(diagnostic)));
        assert(strstr(diagnostic,inject)); assert(strstr(diagnostic,"errno="));
        if (is("lstat")) { char expected[32]; snprintf(expected,sizeof(expected),"errno=%d",ENOSYS); assert(strstr(diagnostic,expected)); }
        if (is("zip_close") || is("zip_add")) { char expected[32]; snprintf(expected,sizeof(expected),"zip=%d",ZIP_ER_MEMORY); assert(strstr(diagnostic,expected)); }
        assert(!strstr(diagnostic,base) && access(output,F_OK)!=0);
    }
    inject=NULL; unknown_type=0;
    assert(!zip_directory_diagnostic(NULL,input,output,NULL,NULL,diagnostic,sizeof(diagnostic)));
    assert(strstr(diagnostic,"path validation"));
    cancel_now=1;
    assert(!zip_directory_diagnostic(base,input,output,cancelled,NULL,diagnostic,sizeof(diagnostic)));
    assert(strstr(diagnostic,"cancelled")); cancel_now=0;
    cancel_on_close=1;
    assert(!zip_directory_diagnostic(base,input,output,cancelled,NULL,diagnostic,sizeof(diagnostic)));
    assert(strstr(diagnostic,"zip_close"));
    char cancel_code[32]; snprintf(cancel_code,sizeof(cancel_code),"zip=%d",ZIP_ER_CANCELLED);
    assert(strstr(diagnostic,cancel_code) && access(output,F_OK)!=0);
    cancel_on_close=cancel_now=0;
    assert(!symlink("../result.zip",link));
    assert(!zip_directory_diagnostic(base,input,output,NULL,NULL,diagnostic,sizeof(diagnostic)));
    assert(strstr(diagnostic,"unsupported file type") && access(output,F_OK)!=0); assert(!unlink(link));
#ifdef __PS4__
    /* Typed getdents entries work even when libc lstat is ENOSYS. */
    inject="lstat";
#endif
    assert(zip_directory_diagnostic(base,input,output,NULL,NULL,diagnostic,sizeof(diagnostic)) && !*diagnostic);
    inject=NULL;
    zip_t *z=zip_open(output,ZIP_RDONLY|ZIP_CHECKCONS,NULL); assert(z);
    zip_file_t *f=zip_fopen(z,"save/data",0); assert(f);
    char bytes[32]={0}; assert(zip_fread(f,bytes,sizeof(bytes))==16 && !strcmp(bytes,"source unchanged"));
    assert(!zip_fclose(f) && !zip_close(z));
    assert(!unlink(output) && !unlink(file) && !rmdir(input) && !rmdir(base));
    puts("ZIP diagnostics tests passed (injected operations, cleanup, cancellation, symlinks, checked archive).");
    return 0;
}
