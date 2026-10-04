#ifndef SAVE_ZIP_H
#define SAVE_ZIP_H
int zip_directory(const char*, const char*, const char*);
int zip_directory_cancel(const char*, const char*, const char*, int (*)(void*), void*);
#endif
