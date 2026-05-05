/* Minimal stub for math.h — math functions come from -lm */
#ifndef _MATH_H_
#define _MATH_H_

#ifdef __cplusplus
extern "C" {
#endif

double fma(double x, double y, double z);
double fabs(double x);
double floor(double x);
double ceil(double x);
double sqrt(double x);

#ifdef __cplusplus
}
#endif

#endif /* _MATH_H_ */
