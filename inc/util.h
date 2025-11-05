#ifndef GALA_UTIL_H
#define GALA_UTIL_H

#include <stdnoreturn.h>
#include <stddef.h>


#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*(a)))
#define MAX(a, b) ((a)<(b)?(b):(a))
#define MIN(a, b) ((a)<(b)?(a):(b))
#define CLAMP(a, lo, hi) MAX(lo, MIN(hi, a))

__attribute__((noinline))
noreturn void crash(const char *reason, ...);
__attribute__((noinline))
void *xmalloc(size_t sz);

#endif /* GALA_UTIL_H */

