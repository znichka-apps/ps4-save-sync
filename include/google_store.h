#ifndef GOOGLE_STORE_H
#define GOOGLE_STORE_H
#include <stdint.h>
#include <stddef.h>
enum { GOOGLE_STORE_READ, GOOGLE_STORE_WRITE, GOOGLE_STORE_CLEAR };
int google_store(uint32_t user, int operation, char *token, size_t capacity);
#endif
