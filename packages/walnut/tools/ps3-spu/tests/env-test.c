/* env-test: lib/env.h's spq_env_num, the one parser of libspeq's SPEQ_* numbers. Unset or empty gives the default;
 * a number in range (decimal, 0x, 0 octal) gives itself; text, a trailing character, a leading space or "+", a sign the
 * range has no use for, a value out of range or too big for a long gives the default and one line on stderr.
 * (make test; the callers' wiring is checked by the Makefile's runs of the other tests with a bad value) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "env.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static long get(const char *val, long def, long min, long max)
{
	if (val)
		setenv("SPQ_ENV_TEST", val, 1);
	else
		unsetenv("SPQ_ENV_TEST");
	return spq_env_num("SPQ_ENV_TEST", def, min, max);
}

int main(void)
{
	static const struct { const char *v; long def, min, max, want; } t[] = {
		{ NULL, 7, 0, 100, 7 }, { "", 7, 0, 100, 7 },			/* not set */
		{ "0", 7, 0, 100, 0 }, { "42", 7, 0, 100, 42 }, { "100", 7, 0, 100, 100 },	/* in range, at both ends */
		{ "0x10", 7, 0, 100, 16 }, { "010", 7, 0, 100, 8 },		/* strtol's bases */
		{ "-1", 5, -1, 100, -1 }, { "-1", 7, 0, 100, 7 },		/* a sign only where the range has it */
		{ "101", 7, 0, 100, 7 }, { "-2", 7, -1, 100, 7 },		/* one past an end */
		{ " 5", 7, 0, 100, 7 }, { "+5", 7, 0, 100, 7 }, { "\t5", 7, 0, 100, 7 }, { "- 5", 7, -1, 100, 7 },
		{ "abc", 7, 0, 100, 7 }, { "12abc", 7, 0, 100, 7 }, { "1 ", 7, 0, 100, 7 }, { "1.5", 7, 0, 100, 7 },
		{ "99999999999999999999", 7, 0, 100, 7 }, { "-99999999999999999999", 7, -1, 100, 7 },	/* overflow */
		{ "4294967297", 7, 0, 100, 7 },					/* a wrapped unsigned would be 1 */
		{ "-4294967295", 7, 0, 100, 7 },				/* a wrapped unsigned would be 1 */
	};

	if (!freopen("/dev/null", "w", stderr))
		return 1;
	for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
		long got = get(t[i].v, t[i].def, t[i].min, t[i].max);
		CHECK(got == t[i].want, "\"%s\" in %ld..%ld (default %ld): %ld, want %ld", t[i].v ? t[i].v : "(unset)", t[i].min,
		      t[i].max, t[i].def, got, t[i].want);
	}
	printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
	return fails != 0;
}
