/* Runs on an SPE: proves the spu-elf toolchain (compiler, newlib, crt0) on real hardware.
 * The result is left in local store for spurun to read. No stdio: newlib's SPU printf family
 * hands the formatting to the PPE (stop 0x2100, the PPE-assisted call libspe2 used to serve). */
#include <string.h>

char message[64];
volatile unsigned int checksum;

static char *utoa(unsigned int v, char *p)
{
	char tmp[12];
	int n = 0;

	do
		tmp[n++] = '0' + v % 10;
	while (v /= 10);
	while (n)
		*p++ = tmp[--n];
	return p;
}

int main(void)
{
	unsigned int i, s = 0;
	char *p;

	for (i = 1; i <= 1000; i++)	/* 500500 */
		s += i;
	checksum = s;
	strcpy(message, "hello from an SPE, sum ");	/* newlib */
	p = utoa(s, message + strlen(message));
	*p = 0;
	return s == 500500 ? 0 : 1;
}
