#ifndef IDEM_PLATFORM_H
#define IDEM_PLATFORM_H

#include <stddef.h>

/*
 * Strict C89 integer aliases.  u32 is at least 32 bits; on LP64 it is
 * 64 bits wide, which is harmless for the uses in the compiler.  The
 * u64 type is a portable four-limb representation so the lexer does not
 * depend on a C99 long long.
 */

typedef unsigned char  u8;
typedef signed   char  i8;
typedef unsigned short u16;
typedef signed   short i16;
typedef unsigned long  u32;
typedef signed   long  i32;

typedef struct u64 {
    u16 w[4]; /* little-endian limbs */
} u64;

void u64_print(u64 value);

#endif /* IDEM_PLATFORM_H */
