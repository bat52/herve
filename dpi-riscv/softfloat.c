/* Minimal soft-float library for RV32 (no FPU).
 *
 * Provides double-precision operations needed by the mm benchmark:
 *   fma(), fabs(), __adddf3, __subdf3, __muldf3, __divdf3,
 *   comparison functions, and int<->double conversions.
 *
 * IEEE 754 double-precision format:
 *   sign: 1 bit (bit 63)
 *   exponent: 11 bits (bits 62:52), bias 1023
 *   mantissa: 52 bits (bits 51:0), with implicit leading 1
 */

typedef unsigned long long uint64_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef long long int64_t;

#define EXP_BIAS 1023
#define EXP_MASK 0x7FFULL
#define MANT_MASK 0x000FFFFFFFFFFFFFULL
#define SIGN_MASK 0x8000000000000000ULL

static inline uint32_t lo(uint64_t x) { return (uint32_t)(x); }
static inline uint32_t hi(uint64_t x) { return (uint32_t)(x >> 32); }
static inline uint64_t make64(uint32_t hi, uint32_t lo) {
    return ((uint64_t)hi << 32) | lo;
}

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

/* Double addition */
uint64_t __adddf3(uint64_t a, uint64_t b) {
    int sa = get_sign(a), sb = get_sign(b);
    int ea = get_exp(a), eb = get_exp(b);
    uint64_t ma = get_mant(a), mb = get_mant(b);

    if (ea == 2047) return a; /* NaN/Inf */
    if (eb == 2047) return b;

    /* Handle zero */
    if (ea == 0 && ma == 0) return b;
    if (eb == 0 && mb == 0) return a;

    /* Add implicit leading 1 */
    if (ea != 0) ma |= 0x0010000000000000ULL;
    if (eb != 0) mb |= 0x0010000000000000ULL;

    /* Align exponents */
    if (ea < eb) {
        uint64_t tmp; int t;
        tmp = ma; ma = mb; mb = tmp;
        t = ea; ea = eb; eb = t;
        t = sa; sa = sb; sb = t;
    }

    int exp_diff = ea - eb;
    if (exp_diff > 63) {
        mb = 0; /* b is negligible */
    } else if (exp_diff > 0) {
        /* Shift mb right, keeping sticky bit.
         * mb is 53 bits (with implicit leading 1). When exp_diff > 53,
         * ALL bits of mb are shifted out, so sticky = (mb != 0).
         * When exp_diff <= 53, the lower exp_diff bits become the sticky. */
        uint64_t sticky = 0;
        if (exp_diff > 53) {
            sticky = (mb != 0);
        } else {
            sticky = (mb & ((1ULL << exp_diff) - 1)) != 0;
        }
        mb = (mb >> exp_diff);
        if (sticky) mb |= 1; /* set LSB as sticky */
    }

    uint64_t result_mant;
    int result_exp = ea;
    int result_sign;

    if (sa == sb) {
        /* Same sign: add */
        result_mant = ma + mb;
        result_sign = sa;
        /* Handle overflow of mantissa (bit 53 set, i.e. 0x0020000000000000).
         * Shift right by 1, preserving the LSB as guard bit for rounding. */
        if (result_mant & 0x0020000000000000ULL) {
            uint64_t guard = result_mant & 1;
            result_mant >>= 1;
            result_exp++;
            /* Round to nearest, ties to even */
            if (guard) {
                /* If guard is set, we need to round up.
                 * Since we shifted right by 1, the guard bit is the bit that
                 * was shifted out. If it's 1, round up (add 1 to LSB).
                 * Check for overflow after rounding. */
                result_mant++;
                if (result_mant & 0x0020000000000000ULL) {
                    result_mant >>= 1;
                    result_exp++;
                }
            }
        }
    } else {
        /* Different signs: subtract */
        if (ma >= mb) {
            result_mant = ma - mb;
            result_sign = sa;
        } else {
            result_mant = mb - ma;
            result_sign = sb;
        }
        /* Normalize */
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

/* Double subtraction: a - b = a + (-b) */
uint64_t __subdf3(uint64_t a, uint64_t b) {
    return __adddf3(a, b ^ SIGN_MASK);
}

/* Double multiplication */
uint64_t __muldf3(uint64_t a, uint64_t b) {
    int sa = get_sign(a), sb = get_sign(b);
    int ea = get_exp(a), eb = get_exp(b);
    uint64_t ma = get_mant(a), mb = get_mant(b);

    if (ea == 2047 || eb == 2047) {
        if ((ea == 2047 && ma != 0) || (eb == 2047 && mb != 0))
            return 0x7FF8000000000000ULL; /* NaN */
        return set_sign(EXP_MASK << 52, sa ^ sb); /* Inf */
    }

    if ((ea == 0 && ma == 0) || (eb == 0 && mb == 0))
        return set_sign(0, sa ^ sb); /* zero */

    if (ea != 0) ma |= 0x0010000000000000ULL;
    if (eb != 0) mb |= 0x0010000000000000ULL;

    int result_exp = ea + eb - EXP_BIAS;
    int result_sign = sa ^ sb;

    /* 106-bit product: ma * mb
     *
     * ma and mb are 53-bit values (bits 52:0, implicit leading 1 at bit 52).
     * Split each into 26-bit halves for 32-bit multiplication:
     *   a_hi = bits 52:26 (27 bits), a_lo = bits 25:0 (26 bits)
     *   b_hi = bits 52:26 (27 bits), b_lo = bits 25:0 (26 bits)
     *
     * Product decomposition (106 bits total, bits 105:0):
     *   p00 = a_lo * b_lo  (52 bits, contributes to bits 0-51)
     *   p01 = a_lo * b_hi  (53 bits, contributes to bits 26-78)
     *   p10 = a_hi * b_lo  (53 bits, contributes to bits 26-78)
     *   p11 = a_hi * b_hi  (54 bits, contributes to bits 52-105)
     *
     * We build the 106-bit result as {m_hi[52:0], m_lo[52:0]} where:
     *   m_hi = bits 105:53 (53 bits)
     *   m_lo = bits 52:0  (53 bits)
     */
    uint32_t a_lo = (uint32_t)(ma & 0x3FFFFFF);
    uint32_t a_hi = (uint32_t)(ma >> 26);
    uint32_t b_lo = (uint32_t)(mb & 0x3FFFFFF);
    uint32_t b_hi = (uint32_t)(mb >> 26);

    uint64_t p00 = (uint64_t)a_lo * b_lo;
    uint64_t p01 = (uint64_t)a_lo * b_hi;
    uint64_t p10 = (uint64_t)a_hi * b_lo;
    uint64_t p11 = (uint64_t)a_hi * b_hi;

    /* Build the 106-bit product using 64-bit arithmetic.
     * We accumulate into a 128-bit value {hi64, lo64} then extract m_hi and m_lo.
     *
     * p00 contributes to bits 0-51  → goes into lo64
     * p01 << 26 contributes to bits 26-78 → goes into lo64 (lower 64 bits)
     * p10 << 26 contributes to bits 26-78 → goes into lo64
     * p11 << 52 contributes to bits 52-105 → goes into hi64:lo64
     *
     * We use a simpler approach: compute the full 128-bit intermediate.
     */

    /* Start with p00 in the low word */
    uint64_t lo64 = p00;
    uint64_t hi64 = 0;

    /* Add p01 << 26 */
    uint64_t p01_shifted = p01 << 26;
    uint64_t old_lo = lo64;
    lo64 += p01_shifted;
    if (lo64 < old_lo) hi64++;  /* carry */

    /* Add p10 << 26 */
    uint64_t p10_shifted = p10 << 26;
    old_lo = lo64;
    lo64 += p10_shifted;
    if (lo64 < old_lo) hi64++;  /* carry */

    /* Add p11 << 52 (p11 is up to 54 bits, shifted by 52 = up to 106 bits) */
    uint64_t p11_lo = (p11 & 0x000FFFFFFFFFFFFFULL) << 52;  /* lower 52 bits of p11, shifted by 52 */
    uint64_t p11_hi = p11 >> 12;                             /* upper bits of p11 (bits 53:12 = 42 bits) */

    old_lo = lo64;
    lo64 += p11_lo;
    if (lo64 < old_lo) hi64++;  /* carry from low addition */

    hi64 += p11_hi;  /* add upper part of p11 */

    /* Also add the upper parts of p01 and p10 that went beyond bit 63 */
    /* p01 >> 38 gives bits 38-52 of p01 (15 bits), which contribute to bits 64-78 */
    hi64 += (p01 >> 38);
    /* p10 >> 38 gives bits 38-52 of p10 (15 bits), which contribute to bits 64-78 */
    hi64 += (p10 >> 38);

    /* Now extract m_hi (bits 105:53) and m_lo (bits 52:0) from {hi64, lo64} */
    /* lo64 has bits 63:0, hi64 has bits 127:64 */
    /* Combined 128-bit value = {hi64[63:0], lo64[63:0]} */
    /* We need bits 105:0 → that's hi64[41:0] concatenated with lo64[63:0] */
    /* m_lo = bits 52:0 = lo64[52:0] */
    uint64_t m_lo = lo64 & 0x001FFFFFFFFFFFFFULL;  /* lower 53 bits */
    /* m_hi = bits 105:53 = hi64[41:0] concatenated with lo64[63:53] */
    uint64_t m_hi = (hi64 << 11) | (lo64 >> 53);
    /* Mask m_hi to 53 bits */
    m_hi &= 0x001FFFFFFFFFFFFFULL;

    return normalize(m_hi, m_lo, result_exp, result_sign);
}

/* Double division */
uint64_t __divdf3(uint64_t a, uint64_t b) {
    /* libgcc already provides this, but implement for completeness */
    int sa = get_sign(a), sb = get_sign(b);
    int ea = get_exp(a), eb = get_exp(b);
    uint64_t ma = get_mant(a), mb = get_mant(b);

    if (ea == 2047 || eb == 2047) {
        if ((ea == 2047 && ma != 0) || (eb == 2047 && mb != 0))
            return 0x7FF8000000000000ULL;
        return set_sign(EXP_MASK << 52, sa ^ sb);
    }

    if ((ea == 0 && ma == 0) || (eb == 0 && mb == 0))
        return set_sign(EXP_MASK << 52, sa ^ sb); /* div by zero or zero/zero */

    if (ea != 0) ma |= 0x0010000000000000ULL;
    if (eb != 0) mb |= 0x0010000000000000ULL;

    int result_exp = ea - eb + EXP_BIAS;
    int result_sign = sa ^ sb;

    /* Long division: compute ma / mb with 53+ bits of precision */
    uint64_t remainder = ma;
    uint64_t quotient = 0;
    int bits = 0;

    while (bits < 106 && remainder != 0) {
        remainder <<= 1;
        quotient <<= 1;
        if (remainder >= mb) {
            remainder -= mb;
            quotient |= 1;
        }
        bits++;
    }
    quotient <<= (106 - bits);

    uint64_t m_hi = quotient >> 53;
    uint64_t m_lo = quotient & 0x001FFFFFFFFFFFFFULL;

    return normalize(m_hi, m_lo, result_exp, result_sign);
}

/* Compare: return 0 if equal, 1 if a > b, -1 if a < b */
static int cmpdf3(uint64_t a, uint64_t b) {
    if (a == b) return 0;
    int sa = get_sign(a), sb = get_sign(b);
    if (sa != sb) return sa ? -1 : 1;
    /* Same sign: compare as signed magnitude */
    uint64_t mag_a = a & ~SIGN_MASK;
    uint64_t mag_b = b & ~SIGN_MASK;
    if (mag_a == mag_b) return 0;
    if (sa) return (mag_a > mag_b) ? -1 : 1;
    return (mag_a > mag_b) ? 1 : -1;
}

int __eqdf2(uint64_t a, uint64_t b) { return cmpdf3(a, b) == 0 ? 1 : 0; }
int __nedf2(uint64_t a, uint64_t b) { return cmpdf3(a, b) != 0 ? 1 : 0; }
int __gtdf2(uint64_t a, uint64_t b) { return cmpdf3(a, b) > 0 ? 1 : 0; }
int __gedf2(uint64_t a, uint64_t b) { return cmpdf3(a, b) >= 0 ? 1 : 0; }
int __ltdf2(uint64_t a, uint64_t b) { return cmpdf3(a, b) < 0 ? 1 : 0; }
int __ledf2(uint64_t a, uint64_t b) { return cmpdf3(a, b) <= 0 ? 1 : 0; }

/* Double to signed int */
int32_t __fixdfsi(uint64_t a) {
    int ea = get_exp(a);
    uint64_t ma = get_mant(a);
    int sa = get_sign(a);

    if (ea == 0 && ma == 0) return 0;
    if (ea == 2047) return 0x80000000; /* NaN/Inf -> INT_MIN */

    ma |= 0x0010000000000000ULL;
    int shift = 52 - (ea - EXP_BIAS);
    if (shift < 0) return sa ? 0x80000000 : 0x7FFFFFFF; /* overflow */
    uint32_t result = (uint32_t)(ma >> shift);
    if (sa) result = -result;
    return (int32_t)result;
}

/* Unsigned int to double */
uint64_t __floatunsidf(uint32_t a) {
    if (a == 0) return 0;
    int exp = EXP_BIAS + 31;
    uint64_t mant = (uint64_t)a;
    while (!(mant & 0x0010000000000000ULL)) {
        mant <<= 1;
        exp--;
    }
    return ((uint64_t)exp << 52) | (mant & MANT_MASK);
}

/* Signed int to double */
uint64_t __floatsidf(int32_t a) {
    if (a == 0) return 0;
    if (a < 0) return __floatunsidf((uint32_t)(-a)) | SIGN_MASK;
    return __floatunsidf((uint32_t)a);
}

/* Float to double */
uint64_t __extendsfdf2(uint32_t a) {
    int sa = (int)(a >> 31);
    int ea = (int)((a >> 23) & 0xFF);
    uint32_t ma = a & 0x7FFFFF;

    if (ea == 0 && ma == 0) return (uint64_t)sa << 63;
    if (ea == 255) {
        if (ma == 0) return ((uint64_t)sa << 63) | (EXP_MASK << 52);
        return 0x7FF8000000000000ULL; /* NaN */
    }

    uint64_t result_ma = (uint64_t)ma << 29;
    int result_exp = ea + (EXP_BIAS - 127);
    return ((uint64_t)sa << 63) | ((uint64_t)result_exp << 52) | result_ma;
}

/* Double to float */
uint32_t __truncdfsf2(uint64_t a) {
    int sa = get_sign(a);
    int ea = get_exp(a);
    uint64_t ma = get_mant(a);

    if (ea == 2047) {
        if (ma == 0) return ((uint32_t)sa << 31) | 0x7F800000;
        return 0x7FC00000; /* NaN */
    }
    if (ea == 0 && ma == 0) return (uint32_t)sa << 31;

    ma |= 0x0010000000000000ULL;
    int new_exp = ea - EXP_BIAS + 127;
    if (new_exp >= 255) return ((uint32_t)sa << 31) | 0x7F800000; /* overflow to inf */
    if (new_exp <= 0) return (uint32_t)sa << 31; /* underflow to zero */

    uint32_t result_ma = (uint32_t)(ma >> 29);
    uint32_t extra = (uint32_t)(ma & 0x1FFFFFFF);
    uint32_t guard = (extra >> 28) & 1;
    uint32_t round_bit = (extra >> 27) & 1;
    uint32_t sticky = (extra & 0x7FFFFFF) != 0;
    if (guard && (round_bit || sticky)) result_ma++;
    if (result_ma & 0x1000000) { result_ma >>= 1; new_exp++; }

    return ((uint32_t)sa << 31) | ((uint32_t)new_exp << 23) | (result_ma & 0x7FFFFF);
}

/* fma: fused multiply-add: a * b + c */
uint64_t fma(uint64_t a, uint64_t b, uint64_t c) {
    uint64_t product = __muldf3(a, b);
    return __adddf3(product, c);
}

/* fabs: absolute value */
uint64_t fabs(uint64_t x) {
    return x & ~SIGN_MASK;
}
