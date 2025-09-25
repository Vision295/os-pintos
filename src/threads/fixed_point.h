#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include <stdint.h>

#define F (1 << 14)

static inline int int_to_fp(int n) { return n * F; }
static inline int fp_to_int(int x) { return x / F; }

static inline int fp_to_int_nearest(int x) {
    if (x >= 0)
        return (x + F / 2) / F;
    else
        return (x - F / 2) / F;
}

static inline int fp_add(int x, int y) { return x + y; }
static inline int fp_sub(int x, int y) { return x - y; }
static inline int fp_add_int(int x, int n) { return x + n * F; }
static inline int fp_sub_int(int x, int n) { return x - n * F; }

static inline int fp_mul(int x, int y) { return ((int64_t)x) * y / F; }
static inline int fp_mul_int(int x, int n) { return x * n; }
static inline int fp_div(int x, int y) { return ((int64_t)x) * F / y; }
static inline int fp_div_int(int x, int n) { return x / n; }

#endif /* FIXED_POINT_H */
