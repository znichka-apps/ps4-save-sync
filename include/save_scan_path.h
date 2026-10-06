#ifndef SAVE_SCAN_PATH_H
#define SAVE_SCAN_PATH_H
#include <stdio.h>

#define SAVE_SCAN_PATH_CAP 512

static inline int save_scan_path(char path[SAVE_SCAN_PATH_CAP], const char *root, const char *suffix)
{
    int n=snprintf(path,SAVE_SCAN_PATH_CAP,"%s%s",root,suffix);
    return n>=0 && n<SAVE_SCAN_PATH_CAP;
}
#endif
