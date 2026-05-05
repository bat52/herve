/* Test soft-float library against host double operations */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

/* Include the soft-float implementation directly */
/* Use our own type names to avoid conflicts with system headers */
typedef unsigned long long my_u64;
typedef unsigned int my_u32;
typedef int my_i32;
typedef long long my_i64;

#define EXP_BIAS 1023
#define EXP_MASK 0x7FFULL
#define MANT_MASK 0x000FFFFFFFFFFFFFULL
#define SIGN_MASK 0x8000000000000000ULL

static inline int get_exp(uint64_t x) { return (int)((x >> 52) & EXP_MASK); }
static inline uint64_t get_mant(uint64_t x) { return x & MANT_MASK; }
static inline int get_sign(uint64_t x) { return (int)(x >> 63); }
static inline uint64_t set_exp(uint64_t x, int e) {
    return (x & ~(EXP_MASK << 52)) | ((uint64_t)(e & EXP_MASK) << 52);
}
static inline uint64_t set_sign(uint64_t x, int s) {
    return (x & ~SIGN_MASK) | ((uint64_t)(s & 1) << 63);
}

/* Round and normalize a 106-bit (2x53) mantissa to IEEE double format.
 *
 * The 106-bit product {m_hi[52:0], m_lo[52:0]} represents:
 *   value = m_hi * 2^53 + m_lo
 *
 * The IEEE mantissa (with implicit leading 1) should be a 53-bit value
 * in the range [2^52, 2^53), i.e., bit 52 set.
 *
 * We compute the 54-bit IEEE mantissa value as:
 *   mr = (m_hi << 1) | (m_lo >> 52)   [bits 53:0]
 * with guard/round/sticky in m_lo[51:0].
 *
 * Then normalize mr so the leading 1 is at bit 52. */
static uint64_t normalize(uint64_t m_hi, uint64_t m_lo, int exp, int sign) {
    if (m_hi == 0 && m_lo == 0) return set_sign(0, sign);

    /* Compute the 54-bit IEEE mantissa value: mr = (m_hi << 1) | (m_lo >> 52) */
    uint64_t mr = (m_hi << 1) | (m_lo >> 52);
    /* Guard/round/sticky bits from m_lo[51:0] */
    uint64_t guard = (m_lo >> 51) & 1;
    uint64_t round_bit = (m_lo >> 50) & 1;
    uint64_t sticky = (m_lo & ((1ULL << 50) - 1)) != 0;

    if (mr == 0) {
        /* Product is less than 2^52, need to shift left (subnormal or zero) */
        /* This can happen when m_hi == 0 and m_lo < 2^52 */
        /* Shift m_lo left until bit 52 is set */
        int shift = 0;
        while (!(m_lo & 0x0010000000000000ULL) && shift < 63) {
            m_lo <<= 1;
            shift++;
        }
        exp -= shift;
        uint64_t mant = m_lo & MANT_MASK;
        guard = (m_lo >> 52) & 1;
        round_bit = (m_lo >> 51) & 1;
        sticky = (m_lo & ((1ULL << 51) - 1)) != 0;
        if (guard && (round_bit || sticky)) {
            mant++;
            if (mant & 0x0020000000000000ULL) {
                mant >>= 1;
                exp++;
            }
        }
        mant &= MANT_MASK;
        if (exp <= 0) return set_sign(0, sign);
        if (exp >= 2047) return set_sign(EXP_MASK << 52, sign);
        return set_sign(((uint64_t)exp << 52) | mant, sign);
    }

    /* mr is in range [2^52, 2^54). Normalize so leading 1 is at bit 52. */
    if (mr & 0x0020000000000000ULL) {
        /* mr >= 2^53: bit 53 set. Shift right by 1, increment exponent.
         * The LSB of mr becomes the guard bit. */
        guard = mr & 1;
        round_bit = 0;
        sticky = 0;
        mr >>= 1;
        exp++;
    }
    /* Now mr is in [2^52, 2^53). Bit 52 is set. */
    uint64_t mant = mr & MANT_MASK; /* explicit mantissa = lower 52 bits */
    if (guard && (round_bit || sticky)) {
        mant++;
        if (mant & 0x0020000000000000ULL) {
            mant >>= 1;
            exp++;
        }
    }
    mant &= MANT_MASK;
    if (exp <= 0) return set_sign(0, sign);
    if (exp >= 2047) return set_sign(EXP_MASK << 52, sign);
    return set_sign(((uint64_t)exp << 52) | mant, sign);
}

uint64_t __adddf3(uint64_t a, uint64_t b) {
    int sa = get_sign(a), sb = get_sign(b);
    int ea = get_exp(a), eb = get_exp(b);
    uint64_t ma = get_mant(a), mb = get_mant(b);

    if (ea == 2047) return a;
    if (eb == 2047) return b;

    if (ea == 0 && ma == 0) return b;
    if (eb == 0 && mb == 0) return a;

    if (ea != 0) ma |= 0x0010000000000000ULL;
    if (eb != 0) mb |= 0x0010000000000000ULL;

    if (ea < eb) {
        uint64_t tmp; int t;
        tmp = ma; ma = mb; mb = tmp;
        t = ea; ea = eb; eb = t;
        t = sa; sa = sb; sb = t;
    }

    int exp_diff = ea - eb;
    if (exp_diff > 63) {
        mb = 0;
    } else if (exp_diff > 0) {
        uint64_t sticky = 0;
        if (exp_diff > 53) {
            sticky = (mb != 0);
        } else {
            sticky = (mb & ((1ULL << exp_diff) - 1)) != 0;
        }
        mb = (mb >> exp_diff);
        if (sticky) mb |= 1;
    }

    uint64_t result_mant;
    int result_exp = ea;
    int result_sign;

    if (sa == sb) {
        result_mant = ma + mb;
        result_sign = sa;
        if (result_mant & 0x0020000000000000ULL) {
            uint64_t guard = result_mant & 1;
            result_mant >>= 1;
            result_exp++;
            if (guard) {
                result_mant++;
                if (result_mant & 0x0020000000000000ULL) {
                    result_mant >>= 1;
                    result_exp++;
                }
            }
        }
    } else {
        if (ma >= mb) {
            result_mant = ma - mb;
            result_sign = sa;
        } else {
            result_mant = mb - ma;
            result_sign = sb;
        }
        if (result_mant == 0) return 0;
        while (!(result_mant & 0x0010000000000000ULL) && result_exp > 0) {
            result_mant <<= 1;
            result_exp--;
        }
    }

    if (result_exp >= 2047) return set_sign(EXP_MASK << 52, result_sign);
    if (result_exp <= 0) return set_sign(0, result_sign);

    return set_sign(((uint64_t)result_exp << 52) | (result_mant & MANT_MASK), result_sign);
}

uint64_t __subdf3(uint64_t a, uint64_t b) {
    return __adddf3(a, b ^ SIGN_MASK);
}

uint64_t __muldf3(uint64_t a, uint64_t b) {
    int sa = get_sign(a), sb = get_sign(b);
    int ea = get_exp(a), eb = get_exp(b);
    uint64_t ma = get_mant(a), mb = get_mant(b);

    if (ea == 2047 || eb == 2047) {
        if ((ea == 2047 && ma != 0) || (eb == 2047 && mb != 0))
            return 0x7FF8000000000000ULL;
        return set_sign(EXP_MASK << 52, sa ^ sb);
    }

    if ((ea == 0 && ma == 0) || (eb == 0 && mb == 0))
        return set_sign(0, sa ^ sb);

    if (ea != 0) ma |= 0x0010000000000000ULL;
    if (eb != 0) mb |= 0x0010000000000000ULL;

    int result_exp = ea + eb - EXP_BIAS;
    int result_sign = sa ^ sb;

    uint32_t a_lo = (uint32_t)(ma & 0x3FFFFFF);
    uint32_t a_hi = (uint32_t)(ma >> 26);
    uint32_t b_lo = (uint32_t)(mb & 0x3FFFFFF);
    uint32_t b_hi = (uint32_t)(mb >> 26);

    uint64_t p00 = (uint64_t)a_lo * b_lo;
    uint64_t p01 = (uint64_t)a_lo * b_hi;
    uint64_t p10 = (uint64_t)a_hi * b_lo;
    uint64_t p11 = (uint64_t)a_hi * b_hi;

    uint64_t lo64 = p00;
    uint64_t hi64 = 0;

    uint64_t p01_shifted = p01 << 26;
    uint64_t old_lo = lo64;
    lo64 += p01_shifted;
    if (lo64 < old_lo) hi64++;

    uint64_t p10_shifted = p10 << 26;
    old_lo = lo64;
    lo64 += p10_shifted;
    if (lo64 < old_lo) hi64++;

    uint64_t p11_lo = (p11 & 0x000FFFFFFFFFFFFFULL) << 52;
    uint64_t p11_hi = p11 >> 12;

    old_lo = lo64;
    lo64 += p11_lo;
    if (lo64 < old_lo) hi64++;

    hi64 += p11_hi;
    hi64 += (p01 >> 38);
    hi64 += (p10 >> 38);

    uint64_t m_lo = lo64 & 0x001FFFFFFFFFFFFFULL;
    uint64_t m_hi = (hi64 << 11) | (lo64 >> 53);
    m_hi &= 0x001FFFFFFFFFFFFFULL;

    return normalize(m_hi, m_lo, result_exp, result_sign);
}

uint64_t my_fma(uint64_t a, uint64_t b, uint64_t c) {
    uint64_t product = __muldf3(a, b);
    return __adddf3(product, c);
}

uint64_t my_fabs(uint64_t x) {
    return x & ~SIGN_MASK;
}

/* Convert double to uint64_t bit pattern */
static uint64_t d2b(double x) {
    uint64_t result;
    memcpy(&result, &x, sizeof(result));
    return result;
}

static double b2d(uint64_t x) {
    double result;
    memcpy(&result, &x, sizeof(result));
    return result;
}

static int test_count = 0;
static int pass_count = 0;

static void test_add(double a, double b) {
    test_count++;
    uint64_t expected = d2b(a + b);
    uint64_t actual = __adddf3(d2b(a), d2b(b));
    if (expected != actual) {
        printf("FAIL: %g + %g = %g (expected 0x%016llx, got 0x%016llx)\n",
               a, b, b2d(actual), (unsigned long long)expected, (unsigned long long)actual);
    } else {
        pass_count++;
    }
}

static void test_sub(double a, double b) {
    test_count++;
    uint64_t expected = d2b(a - b);
    uint64_t actual = __subdf3(d2b(a), d2b(b));
    if (expected != actual) {
        printf("FAIL: %g - %g = %g (expected 0x%016llx, got 0x%016llx)\n",
               a, b, b2d(actual), (unsigned long long)expected, (unsigned long long)actual);
    } else {
        pass_count++;
    }
}

static void test_mul(double a, double b) {
    test_count++;
    uint64_t expected = d2b(a * b);
    uint64_t actual = __muldf3(d2b(a), d2b(b));
    if (expected != actual) {
        printf("FAIL: %g * %g = %g (expected 0x%016llx, got 0x%016llx)\n",
               a, b, b2d(actual), (unsigned long long)expected, (unsigned long long)actual);
    } else {
        pass_count++;
    }
}

static void test_fma(double a, double b, double c) {
    test_count++;
    uint64_t expected = d2b(a * b + c);
    uint64_t actual = my_fma(d2b(a), d2b(b), d2b(c));
    if (expected != actual) {
        printf("FAIL: %g * %g + %g = %g (expected 0x%016llx, got 0x%016llx)\n",
               a, b, c, b2d(actual), (unsigned long long)expected, (unsigned long long)actual);
    } else {
        pass_count++;
    }
}

int main() {
    printf("Testing soft-float library...\n\n");

    /* Basic tests */
    test_add(1.0, 2.0);
    test_add(0.0, 0.0);
    test_add(-1.0, 1.0);
    test_add(1.5, 2.5);
    test_add(1e10, 1e-10);
    test_add(1e100, 1e100);
    test_add(-1e100, 1e100);
    test_add(1.0, -2.0);
    test_add(0.1, 0.2);  /* Famous inexact case */

    test_sub(5.0, 3.0);
    test_sub(1.0, 1.0);
    test_sub(0.0, 1.0);
    test_sub(1e10, 1e-10);

    test_mul(2.0, 3.0);
    test_mul(0.0, 5.0);
    test_mul(-2.0, 3.0);
    test_mul(1.5, 2.0);
    test_mul(1e100, 1e-100);
    test_mul(1.0, 1.0);
    test_mul(0.5, 0.5);
    test_mul(3.141592653589793, 2.718281828459045);

    test_fma(2.0, 3.0, 1.0);
    test_fma(1.5, 2.0, 0.5);
    test_fma(1e10, 1e-10, 1.0);

    /* Random-ish tests */
    srand(42);
    for (int i = 0; i < 1000; i++) {
        double a = (double)rand() / (double)RAND_MAX * 1000.0 - 500.0;
        double b = (double)rand() / (double)RAND_MAX * 1000.0 - 500.0;
        double c = (double)rand() / (double)RAND_MAX * 1000.0 - 500.0;
        test_add(a, b);
        test_sub(a, b);
        test_mul(a, b);
        test_fma(a, b, c);
    }

    printf("\nResults: %d/%d passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
