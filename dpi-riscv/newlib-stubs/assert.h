/* Minimal stub for assert.h */
#ifndef _ASSERT_H_
#define _ASSERT_H_

#ifdef __cplusplus
extern "C" {
#endif

void abort(void);

#ifdef __cplusplus
}
#endif

#define assert(expr) ((void)((expr) ? 0 : (abort(), 0)))

#endif /* _ASSERT_H_ */
