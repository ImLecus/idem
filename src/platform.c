#include "platform.h"

#include <stdio.h>

static int u64_is_zero(const u64 *v)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (v->w[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static unsigned u64_div10(u64 *v)
{
    unsigned long carry;
    int i;

    carry = 0;
    for (i = 3; i >= 0; i--) {
        unsigned long cur;
        cur = (carry << 16) | (unsigned long)v->w[i];
        v->w[i] = (u16)(cur / 10ul);
        carry = cur % 10ul;
    }
    return (unsigned)carry;
}

void u64_print(u64 v)
{
    char buf[32];
    int n;

    n = 0;
    if (u64_is_zero(&v)) {
        putchar('0');
        return;
    }

    while (!u64_is_zero(&v)) {
        unsigned digit;
        digit = u64_div10(&v);
        buf[n++] = (char)('0' + digit);
    }

    while (n > 0) {
        n -= 1;
        putchar(buf[n]);
    }
}
