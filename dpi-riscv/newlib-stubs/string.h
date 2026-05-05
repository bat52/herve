/* Minimal stub for string.h — syscalls.c defines its own implementations */
#ifndef _STRING_H_
#define _STRING_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *memcpy(void *dest, const void *src, size_t len);
void *memset(void *dest, int byte, size_t len);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t n);
int strcmp(const char *s1, const char *s2);
char *strcpy(char *dest, const char *src);
long atol(const char *str);

#ifdef __cplusplus
}
#endif

#endif /* _STRING_H_ */
