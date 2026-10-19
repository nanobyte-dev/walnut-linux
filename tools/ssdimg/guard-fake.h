/* guard-fake.h: test builds of ps3-imgmap-boot only (-DPS3_GUARD_FAKE -I<dir of ps3disk_guard.h>;
 * test_ssdimg.py, vm-e2e.sh). A model of the kernel's /sys/block/ps3db/write_guard (local/0045) in
 * place of the sysfs I/O, on the kernel's own range code: ps3disk_guard.h, taken from the patch by
 * linux/tests/guard-header.sh. Like sysfs and the driver: a write of more than 4096 bytes is cut at
 * 4096 before the parser sees it, each write is taken whole or not at all (EINVAL, ENOSPC), the seal
 * sorts and merges, and after it ranges and sealed give EPERM.
 *
 * PS3_GUARD_FAKE: words that set the starting state and the faults:
 *   absent          no write_guard directory (a kernel without the patch)
 *   policy=NAME     what policy reads (default whitelist)
 *   sealed          sealed before the tool runs (with the pending ranges)
 *   pending=N       N ranges registered (unsealed) before the tool runs
 *   short=K         the K-th write to ranges takes only its first line (a short count)
 *   einval=K        the K-th write to ranges fails with EINVAL
 *   sealfail        writing sealed fails with EIO
 *   nr_ranges+=D, nr_sectors+=D   what the seal reports is off by D
 *   capacity=N      ps3db's size in sectors (default 2^40)
 * PS3_GUARD_FAKE_LOG: a file that gets "ranges <bytes> <lines>" or "sealed <text>" per write, then the
 * write's bytes for ranges. */
#include <stdbool.h>

typedef uint64_t u64;
#include "ps3disk_guard.h"

#define FAKE_RANGES	100
#define FAKE_SEALED	101

static struct {
	int ready, absent, sealed, short_at, einval_at, sealfail, writes;
	char policy[32];
	long long d_ranges, d_sectors;
	u64 capacity, sectors;
	unsigned int n;
	struct ps3disk_range r[PS3DISK_GUARD_MAX_RANGES];
	FILE *log;
} fk;

static void fake_init(void)
{
	char cfg[512], *w, *save;
	const char *e = getenv("PS3_GUARD_FAKE"), *l = getenv("PS3_GUARD_FAKE_LOG");
	if (fk.ready)
		return;
	fk.ready = 1;
	strcpy(fk.policy, "whitelist");
	fk.capacity = (u64)1 << 40;
	snprintf(cfg, sizeof(cfg), "%s", e ? e : "");
	for (w = strtok_r(cfg, " ", &save); w; w = strtok_r(NULL, " ", &save)) {
		if (!strcmp(w, "absent"))
			fk.absent = 1;
		else if (!strncmp(w, "policy=", 7))
			snprintf(fk.policy, sizeof(fk.policy), "%s", w + 7);
		else if (!strcmp(w, "sealed"))
			fk.sealed = 1;
		else if (!strncmp(w, "pending=", 8))
			for (int i = atoi(w + 8); i > 0; i--, fk.n++)
				fk.r[fk.n] = (struct ps3disk_range){ 1000000 + 100 * fk.n, 1000000 + 100 * fk.n + 8 };
		else if (!strncmp(w, "short=", 6))
			fk.short_at = atoi(w + 6);
		else if (!strncmp(w, "einval=", 7))
			fk.einval_at = atoi(w + 7);
		else if (!strcmp(w, "sealfail"))
			fk.sealfail = 1;
		else if (!strncmp(w, "nr_ranges+=", 11))
			fk.d_ranges = atoll(w + 11);
		else if (!strncmp(w, "nr_sectors+=", 12))
			fk.d_sectors = atoll(w + 12);
		else if (!strncmp(w, "capacity=", 9))
			fk.capacity = strtoull(w + 9, NULL, 10);
		else
			die("PS3_GUARD_FAKE: unknown word %s", w);
	}
	if (fk.sealed)
		fk.n = ps3disk_guard_normalize(fk.r, fk.n, &fk.sectors);
	if (l && !(fk.log = fopen(l, "ae")))
		die("%s: %s", l, strerror(errno));
}

static int guard_present(void)
{
	fake_init();
	return !fk.absent;
}

static int guard_read(const char *name, char *buf, size_t cap)
{
	fake_init();
	if (fk.absent)
		return -ENOENT;
	if (!strcmp(name, "policy"))
		snprintf(buf, cap, "%s", fk.policy);
	else if (!strcmp(name, "sealed"))
		snprintf(buf, cap, "%d", fk.sealed);
	else if (!strcmp(name, "nr_ranges"))
		snprintf(buf, cap, "%lld", (long long)fk.n + (fk.sealed ? fk.d_ranges : 0));
	else if (!strcmp(name, "nr_sectors"))
		snprintf(buf, cap, "%lld", (long long)(fk.sealed ? fk.sectors + fk.d_sectors : 0));
	else
		return -ENOENT;
	return 0;
}

static int guard_open(const char *name)
{
	fake_init();
	if (fk.absent)
		return -ENOENT;
	if (!strcmp(name, "ranges"))
		return FAKE_RANGES;
	if (!strcmp(name, "sealed"))
		return FAKE_SEALED;
	return -ENOENT;
}

static ssize_t guard_write(int fd, const char *buf, size_t len)
{
	size_t count = len > 4096 ? 4096 : len;	/* sysfs: one page at most, the rest silently dropped */
	unsigned int k;
	int lines = 0, err;
	for (size_t i = 0; i < len; i++)
		lines += buf[i] == '\n';
	if (fd == FAKE_SEALED) {
		if (fk.log)
			fprintf(fk.log, "sealed %.*s", (int)len, buf);
		if (fk.sealfail)
			return -EIO;
		if (len < 1 || buf[0] != '1' || (len > 1 && strspn(buf + 1, "\n") != len - 1))
			return -EINVAL;
		if (fk.sealed)
			return -EPERM;
		fk.n = ps3disk_guard_normalize(fk.r, fk.n, &fk.sectors);
		fk.sealed = 1;
		return len;
	}
	if (fd != FAKE_RANGES)
		return -EBADF;
	fk.writes++;
	if (fk.log) {
		fprintf(fk.log, "ranges %zu %d\n", len, lines);
		fwrite(buf, 1, len, fk.log);
	}
	if (fk.sealed)
		return -EPERM;
	if (fk.writes == fk.einval_at)
		return -EINVAL;
	if (fk.writes == fk.short_at) {	/* as if sysfs had cut the write after its first line */
		const char *nl = memchr(buf, '\n', count);
		if (!nl)
			return -EINVAL;
		count = nl - buf + 1;
	}
	err = ps3disk_guard_parse(buf, count, fk.capacity, 1, fk.r + fk.n, PS3DISK_GUARD_MAX_RANGES - fk.n, &k);
	if (err)
		return err;
	fk.n += k;
	return count;
}

static void guard_close(int fd)
{
	(void)fd;
	if (fk.log)
		fflush(fk.log);
}
