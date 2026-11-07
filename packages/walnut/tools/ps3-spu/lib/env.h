/* A number from the environment (SPEQ_*): the whole string, decimal, 0x hex or 0 octal, inside [min, max]. Unset or
 * empty is the default; anything else (text after the number, a minus sign where the range has none, a value out
 * of range or too big for a long) says so on stderr and is the default too, instead of becoming a wrapped or
 * truncated number that a timeout or a worker count then trusts. */
#ifndef SPEQ_ENV_H
#define SPEQ_ENV_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static inline long spq_env_num(const char *name, long def, long min, long max)
{
	const char *s = getenv(name);
	char *end;
	long v;

	if (!s || !*s)
		return def;
	/* strtol would skip leading white space and take a '+'; the value is a number as written */
	if (!(*s == '-' ? s[1] >= '0' && s[1] <= '9' : *s >= '0' && *s <= '9')) {
		fprintf(stderr, "speq: %s=%s is not a number from %ld to %ld, using %ld\n", name, s, min, max, def);
		return def;
	}
	errno = 0;
	v = strtol(s, &end, 0);
	if (end == s || *end || errno || v < min || v > max) {
		fprintf(stderr, "speq: %s=%s is not a number from %ld to %ld, using %ld\n", name, s, min, max, def);
		return def;
	}
	return v;
}

/* the ranges: a time in microseconds up to 1000 s, in milliseconds up to an hour, a count of jobs or items up to
 * 16 Mi, the SPEs a service may use up to the 8 of a Cell */
#define SPQ_ENV_US_MAX	1000000000L
#define SPQ_ENV_MS_MAX	3600000L
#define SPQ_ENV_COUNT_MAX (16L << 20)
#define SPQ_ENV_SPES_MAX 8L

#endif
