/* Minimal stub for stdlib.h */
#ifndef _STDLIB_H_
#define _STDLIB_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void exit(int status);
void abort(void);
long atol(const char *str);
int atoi(const char *str);
void *malloc(size_t size);
void free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* _STDLIB_H_ */
