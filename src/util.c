#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "util.h"

noreturn void crash(const char *reason, ...)
{
	va_list args;
	va_start(args, reason);
	vfprintf(stderr, reason, args);
	fputc('\n', stderr);
	va_end(args);
	exit(1);
}

void *xmalloc(size_t sz)
{
	void *p = malloc(sz);
	if (!p)
		crash("malloc");
	return p;
}

