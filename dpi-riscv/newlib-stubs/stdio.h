/* Minimal stub for stdio.h — syscalls.c defines its own implementations */
#ifndef _STDIO_H_
#define _STDIO_H_

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int printf(const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int putchar(int ch);

#ifdef __cplusplus
}
#endif

#endif /* _STDIO_H_ */
