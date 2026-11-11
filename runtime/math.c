#include "value.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#define IDEM_INT_MAX 9223372036854775807L
#define IDEM_INT_MIN (-IDEM_INT_MAX - 1L)

static void idem_overflow(void)
{
    idem_panic_str("integer overflow");
}

Value idem_int_add(Value a, Value b)
{
    IdemInt x;
    IdemInt y;
    x = a.as.i;
    y = b.as.i;
    if (y > 0 && x > IDEM_INT_MAX - y) {
        idem_overflow();
    }
    if (y < 0 && x < IDEM_INT_MIN - y) {
        idem_overflow();
    }
    return idem_int(x + y);
}

Value idem_int_sub(Value a, Value b)
{
    IdemInt x;
    IdemInt y;
    x = a.as.i;
    y = b.as.i;
    if (y < 0 && x > IDEM_INT_MAX + y) {
        idem_overflow();
    }
    if (y > 0 && x < IDEM_INT_MIN + y) {
        idem_overflow();
    }
    return idem_int(x - y);
}

Value idem_int_mul(Value a, Value b)
{
    IdemInt x;
    IdemInt y;
    IdemUInt ux;
    IdemUInt uy;
    IdemUInt limit;
    IdemUInt product;
    int negative;

    x = a.as.i;
    y = b.as.i;

    if (x == 0 || y == 0) {
        return idem_int(0);
    }

    if ((x == IDEM_INT_MIN && y == -1) ||
        (y == IDEM_INT_MIN && x == -1)) {
        idem_overflow();
    }

    negative = (x < 0) != (y < 0);
    ux = x < 0 ? (IdemUInt)(-(x + 1)) + 1 : (IdemUInt)x;
    uy = y < 0 ? (IdemUInt)(-(y + 1)) + 1 : (IdemUInt)y;
    limit = (IdemUInt)IDEM_INT_MAX + (negative ? 1 : 0);

    if (ux > limit / uy) {
        idem_overflow();
    }

    product = ux * uy;
    if (negative) {
        if (product == (IdemUInt)IDEM_INT_MAX + 1) {
            return idem_int(IDEM_INT_MIN);
        }
        return idem_int(-(IdemInt)product);
    }
    return idem_int((IdemInt)product);
}

Value idem_int_div(Value a, Value b)
{
    IdemInt x;
    IdemInt y;
    x = a.as.i;
    y = b.as.i;
    if (y == 0) {
        idem_panic_str("division by zero");
    }
    if (x == IDEM_INT_MIN && y == -1) {
        idem_overflow();
    }
    return idem_int(x / y);
}

Value idem_int_mod(Value a, Value b)
{
    IdemInt x;
    IdemInt y;
    x = a.as.i;
    y = b.as.i;
    if (y == 0) {
        idem_panic_str("modulo by zero");
    }
    if (x == IDEM_INT_MIN && y == -1) {
        idem_overflow();
    }
    return idem_int(x % y);
}

Value idem_int_neg(Value a)
{
    if (a.as.i == IDEM_INT_MIN) {
        idem_overflow();
    }
    return idem_int(-a.as.i);
}

Value idem_int_and(Value a, Value b)
{
    return idem_int(a.as.i & b.as.i);
}

Value idem_int_or(Value a, Value b)
{
    return idem_int(a.as.i | b.as.i);
}

Value idem_int_xor(Value a, Value b)
{
    return idem_int(a.as.i ^ b.as.i);
}

Value idem_int_not(Value a)
{
    return idem_int(~a.as.i);
}

Value idem_int_shl(Value a, Value b)
{
    IdemInt count;
    count = b.as.i;
    if (count < 0 || count > 63) {
        idem_panic_str("shift count out of range");
    }
    return idem_int((IdemInt)((IdemUInt)a.as.i << (unsigned int)count));
}

Value idem_int_shr(Value a, Value b)
{
    IdemInt count;
    count = b.as.i;
    if (count < 0 || count > 63) {
        idem_panic_str("shift count out of range");
    }
    return idem_int(a.as.i >> (unsigned int)count);
}

Value idem_float_add(Value a, Value b)
{
    return idem_float(a.as.f + b.as.f);
}

Value idem_float_sub(Value a, Value b)
{
    return idem_float(a.as.f - b.as.f);
}

Value idem_float_mul(Value a, Value b)
{
    return idem_float(a.as.f * b.as.f);
}

Value idem_float_div(Value a, Value b)
{
    return idem_float(a.as.f / b.as.f);
}

Value idem_float_mod(Value a, Value b)
{
    return idem_float(fmod(a.as.f, b.as.f));
}

Value idem_float_neg(Value a)
{
    return idem_float(-a.as.f);
}

Value idem_abs_int(Value value)
{
    if (value.as.i == IDEM_INT_MIN) {
        idem_overflow();
    }
    if (value.as.i < 0) {
        return idem_int(-value.as.i);
    }
    return value;
}

Value idem_abs_float(Value value)
{
    return idem_float(fabs(value.as.f));
}

static int idem_is_nan(double value)
{
    return value != value;
}

Value idem_min_int(Value a, Value b)
{
    return a.as.i <= b.as.i ? a : b;
}

Value idem_max_int(Value a, Value b)
{
    return a.as.i >= b.as.i ? a : b;
}

Value idem_min_float(Value a, Value b)
{
    if (idem_is_nan(a.as.f) && idem_is_nan(b.as.f)) {
        return a;
    }
    if (idem_is_nan(a.as.f)) {
        return b;
    }
    if (idem_is_nan(b.as.f)) {
        return a;
    }
    return a.as.f <= b.as.f ? a : b;
}

Value idem_max_float(Value a, Value b)
{
    if (idem_is_nan(a.as.f) && idem_is_nan(b.as.f)) {
        return a;
    }
    if (idem_is_nan(a.as.f)) {
        return b;
    }
    if (idem_is_nan(b.as.f)) {
        return a;
    }
    return a.as.f >= b.as.f ? a : b;
}

Value idem_sqrt(Value value)
{
    return idem_float(sqrt(value.as.f));
}

Value idem_floor(Value value)
{
    return idem_float(floor(value.as.f));
}

Value idem_ceil(Value value)
{
    return idem_float(ceil(value.as.f));
}

Value idem_pow(Value a, Value b)
{
    return idem_float(pow(a.as.f, b.as.f));
}

Value idem_random(void)
{
    static int seeded = 0;
    IdemUInt high;
    IdemUInt low;

    if (!seeded) {
        srand((unsigned int)((unsigned long)time(NULL) ^
                             (unsigned long)clock() ^
                             (unsigned long)&seeded));
        seeded = 1;
    }

    high = (IdemUInt)(rand() & 0x7FFF);
    low = (IdemUInt)(rand() & 0x7FFF);
    return idem_int((IdemInt)((high << 30) | (low << 15) |
                              (IdemUInt)(rand() & 0x7FFF)));
}

Value idem_now(void)
{
    time_t seconds;
    seconds = time(NULL);
    return idem_int((IdemInt)seconds * 1000L);
}
