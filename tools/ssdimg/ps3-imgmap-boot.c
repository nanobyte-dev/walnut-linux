/* ps3-imgmap-boot: the SSD root's activation in the initramfs (docs/GAMEOS-HDD.md "Phase B").
 * The boot-time half of tools/ssdimg/ps3-imgmap, in C (static, no Python, no dmsetup, no udev):
 *
 *   ps3-imgmap-boot check MAP
 *       Reads the boot map (ps3-imgmap export-boot writes it from a verified, created map) and checks
 *       it: its own sha256 line, the extent list's sha256 (the same digest ps3-imgmap keeps), the
 *       extents (in order, no overlap, inside the device past the UFS2 super block), the layout.
 *       Touches no device.
 *   ps3-imgmap-boot tables MAP root|swap
 *       Prints the dm-linear table of the root (the image after its 1 MiB header area, minus the swap
 *       tail) or of the swap (the last swap_sectors), exactly as ps3-imgmap create builds them.
 *   ps3-imgmap-boot recheck MAP MOUNTPOINT [--run DIR]
 *       MOUNTPOINT is /dev/ps3db mounted read-only with ufstype=ufs2. Opens MOUNTPOINT/<image>, checks
 *       the mount (ufs, ro, ufstype=ufs2, from /dev/ps3db), the UFS2 super block's geometry, FIGETBSZ,
 *       statvfs, the device's size, the file's size and inode and its first stamp, then FIBMAPs the
 *       first and last fragment of every block and compares the merged extents with the map's. A
 *       match is recorded in DIR/rechecked for this boot (create needs it). Any difference: refused.
 *   ps3-imgmap-boot create MAP [--root-name ps3root] [--swap-name ps3swap] [--run DIR] [--guard auto|required]
 *       Needs the recheck of this map in this boot and /dev/ps3db not mounted. Checks the kernel's
 *       write guard (local/0045, /sys/block/ps3db/write_guard: policy whitelist, not sealed, no
 *       ranges; none there: a log line and on, unless --guard required). BLKROSET on /dev/ps3da
 *       (and checks it took), reads the header area's stamps back through the extents (O_DIRECT, the
 *       image's first MiB, never mapped), then creates the two dm-linear devices with the
 *       device-mapper ioctls, reads each table back from the kernel and compares it, checks the
 *       sizes, and makes /dev/mapper/<name>. With the guard: registers the device side of every
 *       target read back (never the header area) in whole-line writes of at most 4096 bytes, seals,
 *       and checks nr_ranges and nr_sectors. On any failure the devices made so far are removed (a
 *       failure after the first registered range can't be undone in this boot: reboot).
 *   ps3-imgmap-boot map MOUNTPOINT IMAGE --uuid U --size BYTES --swap-mib N --out MAP [--run DIR]
 *       The internal install's first boot (docs/GAMEOS-HDD.md "The internal install"): the boot map of
 *       a freshly uploaded image, made in the console. recheck's mount and geometry checks, then the
 *       size and UUID the PC's install manifest promised and the image's first MiB (uploaded last)
 *       all stamps, FIBMAP, and the map text ps3-imgmap export-boot writes, checked as check does. It
 *       is this boot's FIBMAP, so it stands for this boot's recheck (create's ticket).
 *   ps3-imgmap-boot verify MAP
 *       Every 4 KiB block of the image read from /dev/ps3db (O_DIRECT) through the map's extents and
 *       compared with its stamp (ps3-imgmap verify in C). Any block that isn't: refused. An image that
 *       holds anything but the PC's stamps (an earlier install's root, say) can't pass.
 *   ps3-imgmap-boot disks
 *       /dev/ps3db has a UFS2 super block and /dev/ps3dc a FAT boot sector that fits it, before
 *       /init mounts either (and before dev_hdd1 is ever written). Reads two sectors.
 *   ps3-imgmap-boot install-conf FILE
 *       Checks the PC's install manifest (dev_hdd1:/linux/install/install.conf: its closing sha256
 *       line, the fields, paths, sizes) and prints it as key=value lines for /init.
 *
 * Exit status: 0 done, 2 refused (with the reason on stderr), 1 usage or a system error.
 * Never opens /dev/ps3db, /dev/ps3da or /dev/ps3dc for writing (map writes only its OUT file and the record): reads go through O_RDONLY descriptors, and only
 * what is written to /dev/mapper/<name> afterwards lands inside the image file's own blocks.
 * No test seams: the device names are fixed (the KVM test, tools/ssdimg/vm-e2e.sh, makes /dev/ps3db
 * and /dev/ps3da nodes for its virtual disks instead, or loads a module that gives them the guard).
 * Test builds only: -DPS3_GUARD_FAKE swaps the guard's sysfs I/O for a model of the kernel's side
 * (guard-fake.h) and adds "guard-test MAP" (test_ssdimg.py). FIBMAP, FIGETBSZ, BLKROSET and the dm ioctls
 * come from the kernel headers, so their numbers are the build target's (powerpc's _IO differs from
 * x86's: FIBMAP is 0x20000001 there). */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <linux/fs.h>
#include <linux/dm-ioctl.h>

#define FORMAT		"ps3-imgmap-boot 1"
#define DIGEST_VERSION	"ps3-imgmap 1"
#define DEVICE		"/dev/ps3db"
#define WHOLE		"/dev/ps3da"
#define CACHE		"/dev/ps3dc"	/* dev_hdd1: only read here, never written */
#define MOUNTINFO	"/proc/self/mountinfo"
#define BOOT_ID		"/proc/sys/kernel/random/boot_id"
#define DM_CONTROL	"/dev/mapper/control"
#define RUN_DEFAULT	"/run/ps3ssd"
#define BLOCK		4096
#define STAMP_LEN	36
#define HEADER_SECTORS	2048		/* the image's first MiB: its header area, never mapped */
#define UFS2_SBLOCK	65536
#define UFS2_MAGIC	0x19540119u
#define MAX_EXTENTS	(1 << 20)
#define MAGIC		"PS3IMGv1"

static const char *prog = "ps3-imgmap-boot";

/* The long steps' progress: one line on the console, rewritten in place (\r) every few seconds;
 * the next other message ends it first (progress_end) */
static int progress_open;

static void progress(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "\r>> ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "   ");
	va_end(ap);
	fflush(stderr);
	progress_open = 1;
}

static void progress_end(void)
{
	if (progress_open)
		fprintf(stderr, "\n");
	progress_open = 0;
}

static void say(const char *fmt, ...)
{
	va_list ap;
	progress_end();
	va_start(ap, fmt);
	fprintf(stderr, ">> ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

static void __attribute__((noreturn)) refuse(const char *fmt, ...)
{
	va_list ap;
	progress_end();
	va_start(ap, fmt);
	fprintf(stderr, "!! %s refused: ", prog);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(2);
}

static void __attribute__((noreturn)) die(const char *fmt, ...)
{
	va_list ap;
	progress_end();
	va_start(ap, fmt);
	fprintf(stderr, "!! %s: ", prog);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}

/* --- SHA-256 (FIPS 180-4) ------------------------------------------------------------------ */
struct sha256 {
	uint32_t h[8];
	uint64_t len;
	unsigned char buf[64];
	size_t n;
};

static const uint32_t K256[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(struct sha256 *s, const unsigned char *p)
{
	uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
	int i;
	for (i = 0; i < 16; i++)
		w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
	for (; i < 64; i++)
		w[i] = (ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10)) + w[i - 7] +
		       (ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 16];
	a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
	for (i = 0; i < 64; i++) {
		t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
		t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
	}
	s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void sha256_init(struct sha256 *s)
{
	static const uint32_t h0[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
					0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	memcpy(s->h, h0, sizeof(h0));
	s->len = 0;
	s->n = 0;
}

static void sha256_add(struct sha256 *s, const void *data, size_t len)
{
	const unsigned char *p = data;
	s->len += len;
	while (len) {
		size_t k = 64 - s->n < len ? 64 - s->n : len;
		memcpy(s->buf + s->n, p, k);
		s->n += k; p += k; len -= k;
		if (s->n == 64) {
			sha256_block(s, s->buf);
			s->n = 0;
		}
	}
}

static void sha256_hex(struct sha256 *s, char out[65])
{
	uint64_t bits = s->len * 8;
	unsigned char pad = 0x80, z = 0, l[8];
	int i;
	sha256_add(s, &pad, 1);
	while (s->n != 56)
		sha256_add(s, &z, 1);
	for (i = 0; i < 8; i++)
		l[i] = bits >> (56 - 8 * i);
	sha256_add(s, l, 8);
	for (i = 0; i < 8; i++)
		sprintf(out + 8 * i, "%08x", s->h[i]);
	out[64] = 0;
}

/* --- CRC-32 (zlib's) and the stamps ------------------------------------------------------- */
static uint32_t crc32_ieee(const unsigned char *p, size_t n)
{
	uint32_t c = 0xffffffffu;
	while (n--) {
		c ^= *p++;
		for (int k = 0; k < 8; k++)
			c = c & 1 ? (c >> 1) ^ 0xedb88320u : c >> 1;
	}
	return ~c;
}

static void put_be64(unsigned char *p, uint64_t v)
{
	for (int i = 0; i < 8; i++)
		p[i] = v >> (56 - 8 * i);
}

static uint64_t get_be(const unsigned char *p, int n)
{
	uint64_t v = 0;
	for (int i = 0; i < n; i++)
		v = v << 8 | p[i];
	return v;
}

/* the 4 KiB block the image should hold at OFF: the stamp, then zeros */
static void expected_block(const unsigned char uid[16], uint64_t off, unsigned char *b)
{
	uint32_t crc;
	memset(b, 0, BLOCK);
	memcpy(b, MAGIC, 8);
	memcpy(b + 8, uid, 16);
	put_be64(b + 24, off);
	crc = crc32_ieee(b, 32);
	b[32] = crc >> 24; b[33] = crc >> 16; b[34] = crc >> 8; b[35] = crc;
}

/* --- the boot map ------------------------------------------------------------------------- */
struct extent {
	int64_t l, p, n;	/* image sector, device sector, sectors */
};

struct map {
	char image[256];
	char device[256];
	int64_t device_sectors, inode, file_size, fragment_size, block_size, swap_sectors;
	char byte_order[32];
	char uuid[40];
	unsigned char uid[16];
	char extents_sha256[65];
	int nextents;
	struct extent *ext;
};

static int parse_uuid(const char *s, unsigned char out[16])
{
	int k = 0;
	if (strlen(s) != 36)
		return -1;
	for (int i = 0; i < 36; i++) {
		if (i == 8 || i == 13 || i == 18 || i == 23) {
			if (s[i] != '-')
				return -1;
			continue;
		}
		if (!isxdigit((unsigned char)s[i]) || isupper((unsigned char)s[i]))
			return -1;
		int v = isdigit((unsigned char)s[i]) ? s[i] - '0' : s[i] - 'a' + 10;
		if (k % 2 == 0)
			out[k / 2] = v << 4;
		else
			out[k / 2] |= v;
		k++;
	}
	return 0;
}

static int is_hex64(const char *s)
{
	if (strlen(s) != 64)
		return 0;
	for (; *s; s++)
		if (!isxdigit((unsigned char)*s) || isupper((unsigned char)*s))
			return 0;
	return 1;
}

static int good_path(const char *s)
{
	if (s[0] != '/' || strstr(s, "/../") || strstr(s, "/./") || strstr(s, "//") ||
	    (strlen(s) >= 3 && !strcmp(s + strlen(s) - 3, "/..")))
		return 0;
	for (; *s; s++)
		if (!isalnum((unsigned char)*s) && !strchr("/._-+", *s))
			return 0;
	return 1;
}

/* the next line of BUF at *POS (without the newline) into LINE; 0 at the end */
static int next_line(const char *buf, size_t len, size_t *pos, char *line, size_t cap, const char *path)
{
	size_t a = *pos, e = a;
	if (a >= len)
		return 0;
	while (e < len && buf[e] != '\n')
		e++;
	if (e == len)
		refuse("%s: the last line has no newline (truncated)", path);
	if (e - a >= cap)
		refuse("%s: a line is too long", path);
	memcpy(line, buf + a, e - a);
	line[e - a] = 0;
	*pos = e + 1;
	return 1;
}

static int64_t num(const char *s, const char *what, const char *path)
{
	char *end;
	errno = 0;
	if (!*s || !isdigit((unsigned char)*s))
		refuse("%s: %s: not a number: %s", path, what, s);
	long long v = strtoll(s, &end, 10);
	if (errno || *end)
		refuse("%s: %s: not a number: %s", path, what, s);
	return v;
}

/* "key value" with exactly that key */
static const char *field(const char *line, const char *key, const char *path)
{
	size_t k = strlen(key);
	if (strncmp(line, key, k) || line[k] != ' ' || !line[k + 1])
		refuse("%s: expected \"%s <value>\", got \"%.60s\"", path, key, line);
	return line + k + 1;
}

static int by_device(const void *a, const void *b)
{
	const struct extent *x = a, *y = b;
	return x->p < y->p ? -1 : x->p > y->p;
}

static void check_extents(const struct map *m, const char *path)
{
	int64_t want = 0;
	for (int i = 0; i < m->nextents; i++) {
		const struct extent *e = &m->ext[i];
		if (e->l != want || e->n <= 0)
			refuse("%s: the extents don't cover the file in order at sector %" PRId64, path, want);
		if (e->p < (UFS2_SBLOCK + 8192) / 512 || e->p + e->n > m->device_sectors)
			refuse("%s: extent at device sector %" PRId64 "+%" PRId64 " is outside the data area", path, e->p, e->n);
		want += e->n;
	}
	if (want * 512 != m->file_size)
		refuse("%s: the extents cover %" PRId64 " bytes, the file has %" PRId64, path, want * 512, m->file_size);
	/* no two extents overlap on the device (sorted by device sector) */
	struct extent *s = malloc(sizeof(*s) * m->nextents);
	if (!s)
		die("out of memory");
	memcpy(s, m->ext, sizeof(*s) * m->nextents);
	qsort(s, m->nextents, sizeof(*s), by_device);
	for (int i = 1; i < m->nextents; i++)
		if (s[i - 1].p + s[i - 1].n > s[i].p)
			refuse("%s: extents overlap at device sector %" PRId64, path, s[i].p);
	free(s);
}

static void extents_digest(const struct map *m, const struct extent *ext, int n, char out[65])
{
	struct sha256 h;
	char line[128];
	sha256_init(&h);
	snprintf(line, sizeof(line), "%s\nsize %" PRId64 "\nuuid %s\n", DIGEST_VERSION, m->file_size, m->uuid);
	sha256_add(&h, line, strlen(line));
	for (int i = 0; i < n; i++) {
		snprintf(line, sizeof(line), "%" PRId64 " %" PRId64 " %" PRId64 "\n", ext[i].l, ext[i].p, ext[i].n);
		sha256_add(&h, line, strlen(line));
	}
	sha256_hex(&h, out);
}

/* PATH's text into *BUF; its last line is "sha256 <hex of everything before it>", checked; *END is
 * where that line starts */
static void read_signed(const char *path, char **bufp, size_t *endp)
{
	char *buf;
	size_t len = 0, cap = 1 << 16, end;
	ssize_t r;
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		refuse("%s: %s", path, strerror(errno));
	buf = malloc(cap);
	if (!buf)
		die("out of memory");
	while ((r = read(fd, buf + len, cap - len)) > 0) {
		len += r;
		if (len == cap) {
			if (cap >= (64u << 20))
				refuse("%s: too big", path);
			buf = realloc(buf, cap *= 2);
			if (!buf)
				die("out of memory");
		}
	}
	if (r < 0)
		refuse("%s: %s", path, strerror(errno));
	close(fd);
	if (len < 72 || buf[len - 1] != '\n')
		refuse("%s: no closing sha256 line (truncated, or not this kind of file)", path);
	end = len - 1;
	while (end > 0 && buf[end - 1] != '\n')
		end--;
	{
		char want[65], got[65];
		struct sha256 h;
		if (len - end != 72 || strncmp(buf + end, "sha256 ", 7))
			refuse("%s: no closing sha256 line (truncated, or not this kind of file)", path);
		memcpy(want, buf + end + 7, 64);
		want[64] = 0;
		sha256_init(&h);
		sha256_add(&h, buf, end);
		sha256_hex(&h, got);
		if (strcmp(want, got))
			refuse("%s: its sha256 line doesn't match its contents (edited or damaged)", path);
	}
	*bufp = buf;
	*endp = end;
}

static void load_map(const char *path, struct map *m)
{
	char line[512], *buf;
	size_t pos = 0, end;
	read_signed(path, &buf, &end);
	memset(m, 0, sizeof(*m));
	if (!next_line(buf, end, &pos, line, sizeof(line), path) || strcmp(line, FORMAT))
		refuse("%s: not a \"%s\" map", path, FORMAT);
#define STR(key, dst) do { \
		if (!next_line(buf, end, &pos, line, sizeof(line), path)) refuse("%s: ends before %s", path, key); \
		const char *v = field(line, key, path); \
		if (strlen(v) >= sizeof(dst)) refuse("%s: %s too long", path, key); \
		strcpy(dst, v); } while (0)
#define NUM(key, dst) do { \
		if (!next_line(buf, end, &pos, line, sizeof(line), path)) refuse("%s: ends before %s", path, key); \
		dst = num(field(line, key, path), key, path); } while (0)
	STR("image", m->image);
	STR("device", m->device);
	NUM("device_sectors", m->device_sectors);
	NUM("inode", m->inode);
	NUM("file_size", m->file_size);
	NUM("fragment_size", m->fragment_size);
	NUM("block_size", m->block_size);
	STR("byte_order", m->byte_order);
	STR("uuid", m->uuid);
	STR("extents_sha256", m->extents_sha256);
	NUM("swap_sectors", m->swap_sectors);
	int64_t n;
	NUM("extents", n);
#undef STR
#undef NUM
	if (!good_path(m->image))
		refuse("%s: image path \"%s\": absolute, letters, digits, / . _ - + only", path, m->image);
	if (parse_uuid(m->uuid, m->uid))
		refuse("%s: bad uuid %s", path, m->uuid);
	if (!is_hex64(m->extents_sha256))
		refuse("%s: bad extents_sha256", path);
	if (strcmp(m->byte_order, "big-endian") && strcmp(m->byte_order, "little-endian"))
		refuse("%s: bad byte_order %s", path, m->byte_order);
	if (n <= 0 || n > MAX_EXTENTS)
		refuse("%s: %" PRId64 " extents", path, n);
	m->nextents = n;
	m->ext = calloc(n, sizeof(*m->ext));
	if (!m->ext)
		die("out of memory");
	for (int i = 0; i < n; i++) {
		char a[32], b[32], c[32], x;
		if (!next_line(buf, end, &pos, line, sizeof(line), path))
			refuse("%s: %d of %" PRId64 " extents", path, i, n);
		if (sscanf(line, "%31s %31s %31s %c", a, b, c, &x) != 3)
			refuse("%s: bad extent line \"%.60s\"", path, line);
		m->ext[i].l = num(a, "extent", path);
		m->ext[i].p = num(b, "extent", path);
		m->ext[i].n = num(c, "extent", path);
	}
	if (pos != end)
		refuse("%s: lines after the extents", path);
	free(buf);
	/* the same checks as ps3-imgmap's load_map, and the layout create uses */
	char d[65];
	extents_digest(m, m->ext, m->nextents, d);
	if (strcmp(d, m->extents_sha256))
		refuse("%s: the extent list's sha256 doesn't match its extents", path);
	if (m->fragment_size < 512 || m->fragment_size % 512 || m->block_size < m->fragment_size ||
	    m->block_size > 65536 || m->block_size % m->fragment_size)
		refuse("%s: odd UFS geometry: block %" PRId64 ", fragment %" PRId64, path, m->block_size, m->fragment_size);
	if (m->file_size < 2 * HEADER_SECTORS * 512 || m->file_size % BLOCK || m->file_size % m->block_size)
		refuse("%s: file size %" PRId64 " is not a stamped image of whole UFS blocks", path, m->file_size);
	if (m->device_sectors <= 0 || m->inode <= 0)
		refuse("%s: bad device_sectors or inode", path);
	if (m->swap_sectors < 0 || m->swap_sectors % 2048 ||
	    m->file_size / 512 - HEADER_SECTORS - m->swap_sectors < 2048 * 2)
		refuse("%s: swap_sectors %" PRId64 " leaves less than 2 MiB for the root", path, m->swap_sectors);
	check_extents(m, path);
}

/* dm-linear lines for image sectors [start, end): target start, length, device sector */
struct target {
	int64_t start, len, dev;
};

static int table(const struct map *m, int64_t start, int64_t end, struct target **out)
{
	int k = 0;
	int64_t t = 0;
	*out = calloc(m->nextents, sizeof(**out));
	if (!*out)
		die("out of memory");
	for (int i = 0; i < m->nextents; i++) {
		const struct extent *e = &m->ext[i];
		int64_t a = e->l > start ? e->l : start;
		int64_t b = e->l + e->n < end ? e->l + e->n : end;
		if (a < b) {
			(*out)[k].start = t;
			(*out)[k].len = b - a;
			(*out)[k].dev = e->p + (a - e->l);
			t += b - a;
			k++;
		}
	}
	return k;
}

static void parts(const struct map *m, int64_t *rs, int64_t *re, int64_t *ss, int64_t *se)
{
	int64_t total = m->file_size / 512;
	*rs = HEADER_SECTORS;
	*re = total - m->swap_sectors;
	*ss = total - m->swap_sectors;
	*se = total;
}

/* --- mounts, devices --------------------------------------------------------------------- */
static int has_opt(const char *opts, const char *o)
{
	size_t k = strlen(o);
	for (const char *p = opts; *p;) {
		const char *c = strchr(p, ',');
		size_t n = c ? (size_t)(c - p) : strlen(p);
		if (n == k && !strncmp(p, o, k))
			return 1;
		if (!c)
			break;
		p = c + 1;
	}
	return 0;
}

struct mnt {
	char dev[32], point[512], mopts[512], fstype[64], source[512], sopts[512];
};

/* the mountinfo entries of device "MAJ:MIN": the longest mount point into *BEST; returns how many */
static int mounts_of(const char *devnum, struct mnt *best)
{
	char line[4096];
	int count = 0;
	FILE *f = fopen(MOUNTINFO, "re");
	if (!f)
		die("%s: %s", MOUNTINFO, strerror(errno));
	if (best)
		memset(best, 0, sizeof(*best));
	while (fgets(line, sizeof(line), f)) {
		struct mnt m;
		char *sep = strstr(line, " - ");
		if (!sep)
			continue;
		*sep = 0;
		if (sscanf(line, "%*s %*s %31s %*s %511s %511s", m.dev, m.point, m.mopts) != 3)
			continue;
		if (sscanf(sep + 3, "%63s %511s %511s", m.fstype, m.source, m.sopts) != 3)
			continue;
		if (strcmp(m.dev, devnum))
			continue;
		count++;
		if (best && strlen(m.point) >= strlen(best->point))
			*best = m;
	}
	fclose(f);
	return count;
}

static dev_t blockdev_rdev(const char *path)
{
	struct stat st;
	if (stat(path, &st) < 0)
		refuse("%s: %s", path, strerror(errno));
	if (!S_ISBLK(st.st_mode))
		refuse("%s is not a block device", path);
	return st.st_rdev;
}

static void devnum_str(dev_t d, char *out, size_t n)
{
	snprintf(out, n, "%u:%u", major(d), minor(d));
}

/* the size of PATH in sectors, -1 if it can't be read */
static int64_t try_sectors(const char *path)
{
	uint64_t bytes;
	int fd = open(path, O_RDONLY | O_CLOEXEC), r;
	if (fd < 0)
		return -1;
	r = ioctl(fd, BLKGETSIZE64, &bytes);
	close(fd);
	return r < 0 ? -1 : (int64_t)(bytes / 512);
}

static int64_t device_sectors(const char *path)
{
	uint64_t bytes;
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		refuse("%s: %s", path, strerror(errno));
	if (ioctl(fd, BLKGETSIZE64, &bytes) < 0)
		refuse("%s: BLKGETSIZE64: %s", path, strerror(errno));
	close(fd);
	return bytes / 512;
}

static void read_boot_id(char *out, size_t n)
{
	FILE *f = fopen(BOOT_ID, "re");
	if (!f || !fgets(out, n, f))
		die("%s: %s", BOOT_ID, strerror(errno));
	fclose(f);
	out[strcspn(out, "\n")] = 0;
}

static void ufs_super(int64_t *bsize, int64_t *fsize, const char **order)
{
	unsigned char sb[4096];
	int fd = open(DEVICE, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		refuse("%s: %s", DEVICE, strerror(errno));
	if (pread(fd, sb, sizeof(sb), UFS2_SBLOCK) != (ssize_t)sizeof(sb))
		refuse("%s: can't read the super block at %d", DEVICE, UFS2_SBLOCK);
	close(fd);
	if (get_be(sb + 1372, 4) == UFS2_MAGIC) {
		*bsize = (int32_t)get_be(sb + 48, 4);
		*fsize = (int32_t)get_be(sb + 52, 4);
		*order = "big-endian";
		return;
	}
	uint32_t le = sb[1372] | sb[1373] << 8 | sb[1374] << 16 | (uint32_t)sb[1375] << 24;
	if (le == UFS2_MAGIC) {
		*bsize = (int32_t)(sb[48] | sb[49] << 8 | sb[50] << 16 | (uint32_t)sb[51] << 24);
		*fsize = (int32_t)(sb[52] | sb[53] << 8 | sb[54] << 16 | (uint32_t)sb[55] << 24);
		*order = "little-endian";
		return;
	}
	refuse("%s: no UFS2 super block at %d", DEVICE, UFS2_SBLOCK);
}

static double now_s(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static int fibmap(int fd, int64_t n)
{
	int b = (int)n;
	if (n > INT32_MAX)
		refuse("fragment %" PRId64 " is beyond FIBMAP's int", n);
	if (ioctl(fd, FIBMAP, &b) < 0)
		refuse("FIBMAP of fragment %" PRId64 ": %s%s", n, strerror(errno), errno == EPERM ? " (needs root)" : "");
	return b;
}

static void record_path(const char *run, char *out, size_t n)
{
	snprintf(out, n, "%s/rechecked", run);
}

/* --- the image file on the read-only UFS2 mount: recheck and map ------------------------------ */
/* what the file system says about the image file: geometry, identity, its first MiB, FIBMAP */
struct scan {
	char path[1024];
	int64_t bsize, fsize, device_sectors, size, inode;
	const char *order;
	int n;
	struct extent *ext;
};

/* MOUNTPOINT/IMAGE opened read-only, its mount checked (ufs, ro, ufstype=ufs2, from /dev/ps3db),
 * the geometry cross-checked (super block, FIGETBSZ, statvfs); returns the descriptor */
static int scan_open(const char *mountpoint, const char *image, struct scan *sc)
{
	char devnum[32];
	struct stat st;
	struct statvfs vs;
	struct mnt mnt;
	int bsz;

	memset(sc, 0, sizeof(*sc));
	if (snprintf(sc->path, sizeof(sc->path), "%s%s", mountpoint, image) >= (int)sizeof(sc->path))
		refuse("path too long");
	int fd = open(sc->path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		refuse("%s: %s", sc->path, strerror(errno));
	if (fstat(fd, &st) < 0)
		die("fstat: %s", strerror(errno));
	if (!S_ISREG(st.st_mode))
		refuse("%s is not a regular file", sc->path);
	devnum_str(st.st_dev, devnum, sizeof(devnum));
	if (!mounts_of(devnum, &mnt))
		refuse("%s: its file system (%s) is not in %s", sc->path, devnum, MOUNTINFO);
	if (strcmp(mnt.fstype, "ufs"))
		refuse("%s is on %s (%s), not GameOS's UFS2", sc->path, mnt.point, mnt.fstype);
	if (!has_opt(mnt.mopts, "ro") || !has_opt(mnt.sopts, "ro"))
		refuse("%s: %s is not mounted read-only (%s / %s)", sc->path, mnt.point, mnt.mopts, mnt.sopts);
	if (!has_opt(mnt.sopts, "ufstype=ufs2") && !has_opt(mnt.mopts, "ufstype=ufs2"))
		refuse("%s: %s is not mounted with ufstype=ufs2 (%s)", sc->path, mnt.point, mnt.sopts);
	if (blockdev_rdev(DEVICE) != st.st_dev)
		refuse("%s: its device (%s, %s) is not %s", sc->path, mnt.source, devnum, DEVICE);
	ufs_super(&sc->bsize, &sc->fsize, &sc->order);
	if (ioctl(fd, FIGETBSZ, &bsz) < 0)
		refuse("FIGETBSZ: %s", strerror(errno));
	if (fstatvfs(fd, &vs) < 0)
		die("fstatvfs: %s", strerror(errno));
	if (bsz != sc->fsize || (int64_t)vs.f_bsize != sc->fsize)
		refuse("FIGETBSZ %d, statvfs %lu, the super block's fs_fsize %" PRId64 ": they should be equal "
		       "(the unit assumption is wrong: stop)", bsz, (unsigned long)vs.f_bsize, sc->fsize);
	sc->device_sectors = device_sectors(DEVICE);
	sc->size = st.st_size;
	sc->inode = st.st_ino;
	return fd;
}

/* FIBMAP: the first and last fragment of every block, merged into extents of sectors */
static void scan_fibmap(int fd, struct scan *sc, const char *what)
{
	int64_t fpb = sc->bsize / sc->fsize, k = sc->fsize / 512, nblocks = sc->size / sc->bsize;
	struct extent *ext = malloc(sizeof(*ext) * MAX_EXTENTS);
	int n = 0;
	double last = now_s();
	if (!ext)
		die("out of memory");
	say("%s: FIBMAP of %" PRId64 " blocks of %s", what, nblocks, sc->path);
	for (int64_t b = 0; b < nblocks; b++) {
		int64_t f0 = b * fpb;
		int64_t p0 = fibmap(fd, f0);
		if (p0 == 0)
			refuse("CHANGED: hole at byte %" PRId64 " (fragment %" PRId64 ")", f0 * sc->fsize, f0);
		if (fpb > 1) {
			int64_t p = fibmap(fd, f0 + fpb - 1);
			if (p != p0 + fpb - 1)
				refuse("block %" PRId64 ": last fragment at %" PRId64 ", first at %" PRId64 ": not contiguous "
				       "(the unit assumption is wrong: stop)", b, p, p0);
		}
		int64_t l = f0 * k, p = p0 * k, len = fpb * k;
		if (n && ext[n - 1].l + ext[n - 1].n == l && ext[n - 1].p + ext[n - 1].n == p)
			ext[n - 1].n += len;
		else {
			if (n == MAX_EXTENTS)
				refuse("CHANGED: more than %d extents", MAX_EXTENTS);
			ext[n].l = l; ext[n].p = p; ext[n].n = len;
			n++;
		}
		if (now_s() - last >= 5) {
			last = now_s();
			progress("%s: %" PRId64 " of %" PRId64 " blocks (%.0f%%)", what, b + 1, nblocks,
				 100.0 * (b + 1) / nblocks);
		}
	}
	sc->ext = ext;
	sc->n = n;
}

/* create's ticket: this map's extents were FIBMAPped in this boot */
static void record_recheck(const char *run, const char *sha)
{
	char bid[64], rec[1100];
	read_boot_id(bid, sizeof(bid));
	if (mkdir(run, 0755) < 0 && errno != EEXIST)
		die("%s: %s", run, strerror(errno));
	record_path(run, rec, sizeof(rec));
	FILE *f = fopen(rec, "we");
	if (!f || fprintf(f, "%s %s\n", sha, bid) < 0 || fclose(f))
		die("%s: %s", rec, strerror(errno));
}

static int cmd_recheck(const char *mappath, const char *mountpoint, const char *run)
{
	struct map m;
	struct scan sc;
	char whybuf[256];
	const char *why = NULL;
	double t0 = now_s();

	load_map(mappath, &m);
	if (strcmp(m.device, DEVICE))
		refuse("%s: made for %s, the boot uses %s", mappath, m.device, DEVICE);
	int fd = scan_open(mountpoint, m.image, &sc);
	if (sc.fsize != m.fragment_size || sc.bsize != m.block_size || strcmp(sc.order, m.byte_order)) {
		snprintf(whybuf, sizeof(whybuf), "UFS geometry %" PRId64 "/%" PRId64 " %s, the map's %" PRId64 "/%" PRId64 " %s",
			 sc.bsize, sc.fsize, sc.order, m.block_size, m.fragment_size, m.byte_order);
		why = whybuf;
	} else if (sc.device_sectors != m.device_sectors) {
		snprintf(whybuf, sizeof(whybuf), "%s has %" PRId64 " sectors, the map says %" PRId64,
			 DEVICE, sc.device_sectors, m.device_sectors);
		why = whybuf;
	} else if (sc.size != m.file_size) {
		snprintf(whybuf, sizeof(whybuf), "size %" PRId64 ", map %" PRId64, sc.size, m.file_size);
		why = whybuf;
	} else if (sc.inode != m.inode) {
		snprintf(whybuf, sizeof(whybuf), "inode %" PRId64 ", map %" PRId64 " (the file was replaced)", sc.inode, m.inode);
		why = whybuf;
	} else {
		/* the first block through the file system: still the stamp of offset 0 (the header area
		 * is never mapped, so nothing of ours writes there) */
		unsigned char want[BLOCK], got[BLOCK];
		expected_block(m.uid, 0, want);
		if (pread(fd, got, BLOCK, 0) != BLOCK || memcmp(got, want, BLOCK))
			why = "the file's first block is not this image's stamp of offset 0";
	}
	if (why)
		refuse("CHANGED: %s. Not activating the SSD image.", why);
	scan_fibmap(fd, &sc, "recheck");
	close(fd);
	if (sc.n != m.nextents)
		refuse("CHANGED: the block list differs (%d extents, the map has %d). Not activating the SSD image.",
		       sc.n, m.nextents);
	for (int i = 0; i < sc.n; i++)
		if (sc.ext[i].l != m.ext[i].l || sc.ext[i].p != m.ext[i].p || sc.ext[i].n != m.ext[i].n)
			refuse("CHANGED: the block list differs at extent %d (image sector %" PRId64 "). Not activating "
			       "the SSD image.", i, m.ext[i].l);
	char d[65];
	extents_digest(&m, sc.ext, sc.n, d);
	if (strcmp(d, m.extents_sha256))
		refuse("CHANGED: the block list's sha256 differs");
	free(sc.ext);
	record_recheck(run, m.extents_sha256);
	say("recheck: %s: the block list is the map's (%d extents, sha256 %.12s..., %.1f s)",
	    sc.path, sc.n, m.extents_sha256, now_s() - t0);
	return 0;
}

/* a growing text buffer */
struct text {
	char *s;
	size_t len, cap;
};

static void __attribute__((format(printf, 2, 3))) text_add(struct text *t, const char *fmt, ...)
{
	va_list ap;
	int k;
	for (;;) {
		va_start(ap, fmt);
		k = vsnprintf(t->s ? t->s + t->len : NULL, t->s ? t->cap - t->len : 0, fmt, ap);
		va_end(ap);
		if (k < 0)
			die("vsnprintf");
		if (t->s && t->len + k < t->cap)
			break;
		t->cap = (t->cap ? t->cap : 4096) * 2 + k;
		t->s = realloc(t->s, t->cap);
		if (!t->s)
			die("out of memory");
	}
	t->len += k;
}

/* map MOUNTPOINT IMAGE --uuid U --size N --swap-mib S --out FILE: the boot map of a freshly uploaded
 * image (the internal install's first boot, docs/GAMEOS-HDD.md): the same checks as recheck, then the
 * identity the PC promised (size and UUID; the image's first MiB, uploaded last, all stamps), FIBMAP,
 * and the boot map ps3-imgmap export-boot would write for it. The map is this boot's FIBMAP, so it
 * counts as this boot's recheck for create. Nothing is written but OUT and the record in RUN. */
static int cmd_map(const char *mountpoint, const char *image, const char *uuid, int64_t size, int64_t swap_mib,
		   const char *out, const char *run)
{
	static const unsigned char zero[BLOCK];
	struct scan sc;
	struct map m;
	unsigned char uid[16], want[BLOCK], got[BLOCK];
	char d[65], hex[65], tmp[1100];
	struct text t = { 0 };

	if (!good_path(image))
		refuse("image path \"%s\": absolute, letters, digits, / . _ - + only", image);
	if (!uuid || parse_uuid(uuid, uid))
		refuse("--uuid: a lower-case UUID");
	if (size <= 0 || swap_mib < 0 || !out)
		refuse("map needs --uuid, --size, --swap-mib and --out");
	int fd = scan_open(mountpoint, image, &sc);
	if (sc.size != size)
		refuse("%s has %" PRId64 " bytes, the install says %" PRId64 ": the upload is incomplete, or this is another "
		       "file. Not installing.", sc.path, sc.size, size);
	if (size % BLOCK || size % sc.bsize || size < 2 * HEADER_SECTORS * 512)
		refuse("%s: %" PRId64 " bytes is not a stamped image of whole %" PRId64 "-byte UFS blocks", sc.path, size, sc.bsize);
	/* the header area, through the file system: the PC uploads it last, so an interrupted upload
	 * leaves it unstamped (zeros) and stops here */
	for (int64_t off = 0; off < HEADER_SECTORS * 512; off += BLOCK) {
		expected_block(uid, off, want);
		if (pread(fd, got, BLOCK, off) != BLOCK)
			refuse("%s: can't read byte %" PRId64 ": %s", sc.path, off, strerror(errno));
		if (memcmp(got, want, BLOCK))
			refuse("%s: the header area (its first MiB) isn't image %s's stamps at byte %" PRId64 " (%s): the upload "
			       "is incomplete (the header goes last), or this is another image, or one in use. Not installing.",
			       sc.path, uuid, off, !memcmp(got, zero, BLOCK) ? "zeros" :
			       memcmp(got, MAGIC, 8) ? "no stamp" : "another stamp");
	}
	scan_fibmap(fd, &sc, "map");
	close(fd);
	memset(&m, 0, sizeof(m));
	m.file_size = size;
	snprintf(m.uuid, sizeof(m.uuid), "%s", uuid);
	extents_digest(&m, sc.ext, sc.n, d);
	text_add(&t, "%s\nimage %s\ndevice %s\ndevice_sectors %" PRId64 "\ninode %" PRId64 "\nfile_size %" PRId64
		 "\nfragment_size %" PRId64 "\nblock_size %" PRId64 "\nbyte_order %s\nuuid %s\nextents_sha256 %s\n"
		 "swap_sectors %" PRId64 "\nextents %d\n", FORMAT, image, DEVICE, sc.device_sectors, sc.inode, size,
		 sc.fsize, sc.bsize, sc.order, uuid, d, swap_mib * 2048, sc.n);
	for (int i = 0; i < sc.n; i++)
		text_add(&t, "%" PRId64 " %" PRId64 " %" PRId64 "\n", sc.ext[i].l, sc.ext[i].p, sc.ext[i].n);
	{
		struct sha256 h;
		sha256_init(&h);
		sha256_add(&h, t.s, t.len);
		sha256_hex(&h, hex);
	}
	text_add(&t, "sha256 %s\n", hex);
	snprintf(tmp, sizeof(tmp), "%s.tmp", out);
	FILE *f = fopen(tmp, "we");
	if (!f || fwrite(t.s, 1, t.len, f) != t.len || fflush(f) || fsync(fileno(f)) || fclose(f))
		die("%s: %s", tmp, strerror(errno));
	if (rename(tmp, out) < 0)
		die("%s: %s", out, strerror(errno));
	/* the map read back with every check a boot applies (extents in order, no overlap, past the super
	 * block, the layout) */
	load_map(out, &m);
	record_recheck(run, m.extents_sha256);
	int64_t rs, re, ss, se;
	parts(&m, &rs, &re, &ss, &se);
	say("map: %s: %d extents, root %" PRId64 " MiB, swap %" PRId64 " MiB, sha256 %.12s... -> %s", sc.path, sc.n,
	    (re - rs) / 2048, (se - ss) / 2048, d, out);
	free(sc.ext);
	free(t.s);
	return 0;
}

/* what is in the 4 KiB block B that should be image UID's block at OFF */
static const char *describe(const unsigned char uid[16], uint64_t off, const unsigned char *b, char *buf, size_t n)
{
	static const unsigned char zero[BLOCK];
	unsigned char want[BLOCK];
	if (!memcmp(b, zero, BLOCK))
		return "zeros";
	if (memcmp(b, MAGIC, 8))
		return "no stamp (other data)";
	uint32_t crc = crc32_ieee(b, 32);
	if (get_be(b + 32, 4) != crc)
		return "a stamp with a bad CRC";
	if (memcmp(b + 8, uid, 16))
		return "a stamp of another image";
	uint64_t o = get_be(b + 24, 8);
	if (o != off) {
		snprintf(buf, n, "the stamp of this image's block at %" PRIu64 " (off by %+" PRId64 ")", o, (int64_t)(o - off));
		return buf;
	}
	expected_block(uid, off, want);
	return memcmp(b, want, BLOCK) ? "the stamp right, the tail not zero" : "right";
}

/* verify MAP: every 4 KiB block of the image read straight from /dev/ps3db (O_DIRECT) through the
 * map's extents and compared with its stamp: magic, UUID, offset, CRC, zero tail (Python
 * ps3-imgmap verify). Only a file that is still entirely the PC's stamps passes, so an install can
 * never format an image that holds anything else: one written by an earlier install included. */
#define VCHUNK (4 << 20)
static int cmd_verify(const char *mappath)
{
	static const unsigned char zero[BLOCK];
	struct map m;
	unsigned char *buf, want[BLOCK], stamp[STAMP_LEN];
	int64_t bad = 0, done = 0, nblocks;
	double t0 = now_s(), last = t0;

	load_map(mappath, &m);
	if (strcmp(m.device, DEVICE))
		refuse("%s: made for %s, the boot uses %s", mappath, m.device, DEVICE);
	if (device_sectors(DEVICE) != m.device_sectors)
		refuse("%s has %" PRId64 " sectors, the map says %" PRId64, DEVICE, device_sectors(DEVICE), m.device_sectors);
	nblocks = m.file_size / BLOCK;
	int fd = open(DEVICE, O_RDONLY | O_DIRECT | O_CLOEXEC);
	if (fd < 0)
		refuse("%s: %s", DEVICE, strerror(errno));
	if (posix_memalign((void **)&buf, BLOCK, VCHUNK))
		die("out of memory");
	say("verify: %" PRId64 " blocks of 4 KiB (%" PRId64 " MiB) through %d extents", nblocks, m.file_size >> 20, m.nextents);
	for (int i = 0; i < m.nextents; i++) {
		int64_t lo = m.ext[i].l * 512, end = lo + m.ext[i].n * 512, dev = m.ext[i].p * 512;
		while (lo < end) {
			int64_t k = end - lo < VCHUNK ? end - lo : VCHUNK;
			ssize_t r = pread(fd, buf, k, dev);
			if (r != k)
				refuse("%s: read of %" PRId64 " bytes at byte %" PRId64 ": %s", DEVICE, k, dev,
				       r < 0 ? strerror(errno) : "short");
			/* each 4 KiB image block in [lo, lo + k), whole or the part of it this extent holds */
			for (int64_t bs = lo - lo % BLOCK; bs < lo + k; bs += BLOCK) {
				int64_t a = bs > lo ? bs : lo, e = bs + BLOCK < lo + k ? bs + BLOCK : lo + k;
				const unsigned char *got = buf + (a - lo);
				int ok;
				if (a == bs && e == bs + BLOCK) {
					memcpy(stamp, MAGIC, 8);
					memcpy(stamp + 8, m.uid, 16);
					put_be64(stamp + 24, bs);
					uint32_t crc = crc32_ieee(stamp, 32);
					stamp[32] = crc >> 24; stamp[33] = crc >> 16; stamp[34] = crc >> 8; stamp[35] = crc;
					ok = !memcmp(got, stamp, STAMP_LEN) && !memcmp(got + STAMP_LEN, zero, BLOCK - STAMP_LEN);
				} else {
					expected_block(m.uid, bs, want);
					ok = !memcmp(got, want + (a - bs), e - a);
				}
				if (!ok) {
					if (bad < 10) {
						char why[96];
						progress_end();
						fprintf(stderr, "!! image byte %" PRId64 " (device byte %" PRId64 "): %s\n", bs,
							dev + (bs - lo), a == bs && e == bs + BLOCK ?
							describe(m.uid, bs, got, why, sizeof(why)) : "a partial block differs");
					}
					bad++;
				}
			}
			lo += k;
			dev += k;
			done += k;
			if (now_s() - last >= 5) {
				last = now_s();
				progress("verify: %" PRId64 " of %" PRId64 " MiB (%.0f%%), %.1f MB/s, %" PRId64 " bad",
					 done >> 20, m.file_size >> 20, 100.0 * done / m.file_size,
					 done / (last - t0) / 1048576, bad);
			}
		}
	}
	close(fd);
	free(buf);
	if (bad)
		refuse("verify: %" PRId64 " of %" PRId64 " blocks are not this image's stamps: the file holds something "
		       "else (an earlier install, or another file). Not installing onto it.", bad, nblocks);
	say("verify: all %" PRId64 " blocks are image %s's stamps (%.0f s, %.1f MB/s)", nblocks, m.uuid,
	    now_s() - t0, m.file_size / (now_s() - t0 + 1e-9) / 1048576);
	return 0;
}

/* --- GameOS's two partitions, by content (docs/release/ALPHA.md M1, R gap 9) ----------------- */
static uint32_t le(const unsigned char *p, int n)
{
	uint32_t v = 0;
	for (int i = n - 1; i >= 0; i--)
		v = v << 8 | p[i];
	return v;
}

/* NULL when /dev/ps3dc holds a FAT file system that fits the device (the checks of Linux's
 * fat_read_bpb plus a FAT type string, as blkid's vfat probe wants one), else why not */
static const char *fat_check(char *info, size_t n)
{
	static char why[160];
	unsigned char b[512];
	int fd = open(CACHE, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		snprintf(why, sizeof(why), "%s: %s", CACHE, strerror(errno));
		return why;
	}
	ssize_t r = pread(fd, b, sizeof(b), 0);
	close(fd);
	if (r != (ssize_t)sizeof(b))
		return "can't read its first sector";
	uint32_t bps = le(b + 11, 2), spc = b[13], rsvd = le(b + 14, 2), nfats = b[16], media = b[21];
	uint32_t tot = le(b + 19, 2) ? le(b + 19, 2) : le(b + 32, 4), fatsz16 = le(b + 22, 2);
	int fat32 = fatsz16 == 0;
	if (bps < 512 || bps > 4096 || (bps & (bps - 1)))
		return "no FAT boot sector (bytes per sector)";
	if (!spc || (spc & (spc - 1)))
		return "no FAT boot sector (sectors per cluster)";
	if (!rsvd || !nfats || nfats > 2)
		return "no FAT boot sector (reserved sectors or FAT count)";
	if (media != 0xf0 && media < 0xf8)
		return "no FAT boot sector (media byte)";
	if (fat32 ? memcmp(b + 82, "FAT32   ", 8) && memcmp(b + 82, "MSWIN", 5) : memcmp(b + 54, "FAT", 3))
		return "no FAT type string in the boot sector";
	if (fat32 && !le(b + 36, 4))
		return "FAT32 with no FAT size";
	int64_t dev = try_sectors(CACHE) * 512;
	if (dev <= 0 || (int64_t)tot * bps > dev || (int64_t)tot * bps < dev / 2) {
		snprintf(why, sizeof(why), "its FAT says %" PRId64 " bytes, the device has %" PRId64, (int64_t)tot * bps, dev);
		return why;
	}
	snprintf(info, n, "%s, %" PRId64 " MiB", fat32 ? "FAT32" : "FAT12/16", (int64_t)tot * bps >> 20);
	return NULL;
}

/* disks: dev_hdd0 (/dev/ps3db) is UFS2 and dev_hdd1 (/dev/ps3dc) is FAT, before anything mounts them
 * or writes dev_hdd1. Reads two sectors, writes nothing. */
static int cmd_disks(void)
{
	int64_t bsize, fsize;
	const char *order, *why;
	char info[96];
	ufs_super(&bsize, &fsize, &order);
	say("%s: UFS2 %s, block %" PRId64 ", fragment %" PRId64 " (dev_hdd0)", DEVICE, order, bsize, fsize);
	if ((why = fat_check(info, sizeof(info))))
		refuse("%s is not GameOS's dev_hdd1 (FAT): %s. Nothing is written to it.", CACHE, why);
	say("%s: %s (dev_hdd1)", CACHE, info);
	return 0;
}

/* --- the install manifest (dev_hdd1:/linux/install/install.conf, written by the PC last) -------- */
#define INSTALL_FORMAT	"walnut-install 1"

/* install-conf FILE: checks the PC's install manifest (its closing sha256 line, the fields in order,
 * paths and numbers) and prints it as key=value lines for /init (values: letters, digits, / . _ - +) */
static int cmd_install_conf(const char *path)
{
	char *buf, line[512], image[256], rootfs[256], uuid[40], sha[65];
	size_t end, pos = 0;
	int64_t image_size, swap_mib, rootfs_size, tar_size;
	unsigned char uid[16];
	read_signed(path, &buf, &end);
	if (!next_line(buf, end, &pos, line, sizeof(line), path) || strcmp(line, INSTALL_FORMAT))
		refuse("%s: not a \"%s\" install manifest", path, INSTALL_FORMAT);
#define STR(key, dst) do { \
		if (!next_line(buf, end, &pos, line, sizeof(line), path)) refuse("%s: ends before %s", path, key); \
		const char *v = field(line, key, path); \
		if (strlen(v) >= sizeof(dst)) refuse("%s: %s too long", path, key); \
		strcpy(dst, v); } while (0)
#define NUM(key, dst) do { \
		if (!next_line(buf, end, &pos, line, sizeof(line), path)) refuse("%s: ends before %s", path, key); \
		dst = num(field(line, key, path), key, path); } while (0)
	STR("image", image);
	STR("uuid", uuid);
	NUM("image_size", image_size);
	NUM("swap_mib", swap_mib);
	STR("rootfs", rootfs);
	NUM("rootfs_size", rootfs_size);
	STR("rootfs_sha256", sha);
	NUM("tar_size", tar_size);
#undef STR
#undef NUM
	if (pos != end)
		refuse("%s: lines after tar_size", path);
	if (!good_path(image) || !good_path(rootfs))
		refuse("%s: paths: absolute, letters, digits, / . _ - + only", path);
	if (strncmp(rootfs, "/linux/install/", 15))
		refuse("%s: rootfs %s is not in dev_hdd1's /linux/install/", path, rootfs);
	if (parse_uuid(uuid, uid))
		refuse("%s: bad uuid %s", path, uuid);
	if (!is_hex64(sha))
		refuse("%s: bad rootfs_sha256", path);
	if (image_size <= 0 || image_size % (1 << 20) || swap_mib < 0 || rootfs_size <= 0 || tar_size <= 0)
		refuse("%s: sizes", path);
	if (image_size / 512 - HEADER_SECTORS - swap_mib * 2048 < 2048 * 2)
		refuse("%s: swap_mib %" PRId64 " leaves less than 2 MiB for the root", path, swap_mib);
	printf("image=%s\nuuid=%s\nimage_size=%" PRId64 "\nswap_mib=%" PRId64 "\nrootfs=%s\nrootfs_size=%" PRId64
	       "\nrootfs_sha256=%s\ntar_size=%" PRId64 "\n", image, uuid, image_size, swap_mib, rootfs, rootfs_size, sha,
	       tar_size);
	free(buf);
	return 0;
}

/* --- the header area, read back through the extents (O_DIRECT) --------------------------- */
static void header_check(const struct map *m)
{
	unsigned char *buf, want[BLOCK];
	int fd = open(DEVICE, O_RDONLY | O_DIRECT | O_CLOEXEC);
	if (fd < 0)
		refuse("%s: %s", DEVICE, strerror(errno));
	if (posix_memalign((void **)&buf, BLOCK, BLOCK))
		die("out of memory");
	/* image bytes [0, 1 MiB) one 4 KiB block at a time; the extents are fragment aligned */
	for (int64_t off = 0; off < HEADER_SECTORS * 512; off += BLOCK) {
		int64_t sec = off / 512, dev = -1;
		for (int i = 0; i < m->nextents; i++)
			if (sec >= m->ext[i].l && sec + BLOCK / 512 <= m->ext[i].l + m->ext[i].n) {
				dev = (m->ext[i].p + (sec - m->ext[i].l)) * 512;
				break;
			}
		if (dev < 0)
			refuse("the header area's block at image byte %" PRId64 " spans two extents", off);
		if (dev % BLOCK)
			refuse("device byte %" PRId64 " is not 4 KiB aligned", dev);
		if (pread(fd, buf, BLOCK, dev) != BLOCK)
			refuse("%s: read at byte %" PRId64 ": %s", DEVICE, dev, strerror(errno));
		expected_block(m->uid, off, want);
		if (memcmp(buf, want, BLOCK))
			refuse("the header area's stamps don't read back through the map at image byte %" PRId64
			       " (device byte %" PRId64 "): the file is not where the map says", off, dev);
	}
	close(fd);
	free(buf);
}

/* --- the kernel's write guard (local/0045; docs/GAMEOS-HDD.md "The kernel guard") ------------ */
/* With the guard, ps3db refuses every write until root registers the sector ranges Linux may write
 * and seals them, once per boot. create registers exactly the device side of its dm-linear targets
 * as the kernel reads them back, so the header area (the image's first MiB, never mapped) and
 * everything outside the image stay unwritable. */
#define GUARD_DIR	"/sys/block/ps3db/write_guard"
#define GUARD_CHUNK	4096		/* sysfs takes at most a page per write(2); the smallest page */
#define GUARD_MAX	65536		/* PS3DISK_GUARD_MAX_RANGES */

enum { GUARD_AUTO, GUARD_REQUIRED };

#ifdef PS3_GUARD_FAKE
#include "guard-fake.h"		/* test builds only: a model of the kernel's side */
#else
/* 1 the directory is there, 0 it isn't (a kernel without the guard), -errno */
static int guard_present(void)
{
	struct stat st;
	if (stat(GUARD_DIR, &st) == 0)
		return S_ISDIR(st.st_mode) ? 1 : -ENOTDIR;
	return errno == ENOENT ? 0 : -errno;
}

/* write_guard/NAME without its newline into BUF: 0 or -errno */
static int guard_read(const char *name, char *buf, size_t cap)
{
	char path[256];
	ssize_t r;
	int fd, e;
	snprintf(path, sizeof(path), "%s/%s", GUARD_DIR, name);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	r = read(fd, buf, cap - 1);
	e = errno;
	close(fd);
	if (r < 0)
		return -e;
	buf[r] = 0;
	if (r && buf[r - 1] == '\n')
		buf[r - 1] = 0;
	return 0;
}

static int guard_open(const char *name)
{
	char path[256];
	int fd;
	snprintf(path, sizeof(path), "%s/%s", GUARD_DIR, name);
	fd = open(path, O_WRONLY | O_CLOEXEC);
	return fd < 0 ? -errno : fd;
}

/* one write(2): the count it returned or -errno */
static ssize_t guard_write(int fd, const char *buf, size_t len)
{
	ssize_t r = write(fd, buf, len);
	return r < 0 ? -errno : r;
}

static void guard_close(int fd)
{
	close(fd);
}
#endif

/* write_guard/NAME as a decimal number; -1 with ERR filled if it can't be read or isn't one */
static int64_t guard_num(const char *name, char *err, size_t errn)
{
	char v[64], *end;
	int e = guard_read(name, v, sizeof(v));
	long long x;
	if (e) {
		snprintf(err, errn, "%s/%s: %s", GUARD_DIR, name, strerror(-e));
		return -1;
	}
	errno = 0;
	x = strtoll(v, &end, 10);
	if (!isdigit((unsigned char)v[0]) || *end || errno) {
		snprintf(err, errn, "%s/%s: \"%.20s\" is not a number", GUARD_DIR, name, v);
		return -1;
	}
	return x;
}

/* step 2, before anything is made: 1 the guard is there, unsealed and empty; 0 the kernel has none */
static int guard_precheck(int mode)
{
	char v[64], err[512];
	int p = guard_present(), e;
	int64_t n;
	if (p < 0)
		refuse("%s: %s", GUARD_DIR, strerror(-p));
	if (!p) {
		if (mode == GUARD_REQUIRED)
			refuse("the kernel has no write guard (no %s), but this initramfs is for a kernel with it: "
			       "the wrong kernel or initramfs", GUARD_DIR);
		say("kernel has no write guard (no %s): ps3db's writes are not limited to the image's blocks", GUARD_DIR);
		return 0;
	}
	if ((e = guard_read("policy", v, sizeof(v))))
		refuse("%s/policy: %s", GUARD_DIR, strerror(-e));
	if (strcmp(v, "whitelist"))
		refuse("%s/policy is \"%s\", not whitelist: ps3db is not the region this kernel guards as dev_hdd0",
		       GUARD_DIR, v);
	if ((e = guard_read("sealed", v, sizeof(v))))
		refuse("%s/sealed: %s", GUARD_DIR, strerror(-e));
	if (strcmp(v, "0")) {
		if (strcmp(v, "1"))
			refuse("%s/sealed is \"%s\"", GUARD_DIR, v);
		refuse("ps3db's write guard was sealed earlier in this boot (%s ranges): its ranges can't change "
		       "until the next boot, so these devices could never be written. Reboot.",
		       guard_read("nr_ranges", v, sizeof(v)) ? "?" : v);
	}
	if ((n = guard_num("nr_ranges", err, sizeof(err))) < 0)
		refuse("%s", err);
	if (n)
		refuse("%" PRId64 " ranges are already registered (unsealed) in %s by something else in this boot: "
		       "sealing would make them writable too. Reboot.", n, GUARD_DIR);
	say("write guard: ps3db whitelist, not sealed, no ranges yet");
	return 1;
}

struct range {
	int64_t start, len;
};

static int by_start(const void *a, const void *b)
{
	const struct range *x = a, *y = b;
	return x->start < y->start ? -1 : x->start > y->start;
}

/* the kernel's seal: sorted by start, overlapping or touching ranges joined; returns how many,
 * *SECTORS what they cover */
static int merged_count(const struct range *r, int n, int64_t *sectors)
{
	struct range *s = malloc(sizeof(*s) * (n ? n : 1));
	int k = 0;
	if (!s)
		die("out of memory");
	memcpy(s, r, sizeof(*s) * n);
	qsort(s, n, sizeof(*s), by_start);
	*sectors = 0;
	for (int i = 0; i < n; i++) {
		if (k && s[i].start <= s[k - 1].start + s[k - 1].len) {
			int64_t end = s[i].start + s[i].len;
			if (end > s[k - 1].start + s[k - 1].len)
				s[k - 1].len = end - s[k - 1].start;
			continue;
		}
		s[k++] = s[i];
	}
	for (int i = 0; i < k; i++)
		*sectors += s[i].len;
	free(s);
	return k;
}

/* steps 4-6: R[0..n) into write_guard/ranges, then the seal and its check. 0, or -1 with ERR */
static int guard_register(const struct range *r, int n, char *err, size_t errn)
{
	char buf[GUARD_CHUNK], line[64], v[64];
	size_t len = 0;
	int fd, lines = 0, sent = 0, chunks = 0, e;
	int64_t sum = 0, want_sectors, got;
	ssize_t w;

	for (int i = 0; i < n; i++)
		sum += r[i].len;
	int want_ranges = merged_count(r, n, &want_sectors);
	if (want_sectors != sum) {
		snprintf(err, errn, "the targets overlap on ps3db (%" PRId64 " sectors in all, %" PRId64 " distinct)",
			 sum, want_sectors);
		return -1;
	}
	if ((fd = guard_open("ranges")) < 0) {
		snprintf(err, errn, "%s/ranges: %s", GUARD_DIR, strerror(-fd));
		return -1;
	}
	/* whole lines, at most GUARD_CHUNK bytes per write(2), each taken whole or the end */
	for (int i = 0; i <= n; i++) {
		int k = 0;
		if (i < n)
			k = snprintf(line, sizeof(line), "%" PRId64 " %" PRId64 "\n", r[i].start, r[i].len);
		if (len && (i == n || len + k > sizeof(buf))) {
			w = guard_write(fd, buf, len);
			if (w != (ssize_t)len) {
				guard_close(fd);
				snprintf(err, errn, "%s/ranges: write %d (%zu bytes, %d ranges) got %s after %d ranges taken",
					 GUARD_DIR, chunks + 1, len, lines, w < 0 ? strerror(-w) : "a short count", sent);
				return -1;
			}
			chunks++;
			sent += lines;
			len = 0;
			lines = 0;
		}
		if (i < n) {
			memcpy(buf + len, line, k);
			len += k;
			lines++;
		}
	}
	guard_close(fd);
	if ((got = guard_num("nr_ranges", err, errn)) < 0)
		return -1;
	if (got != n) {
		snprintf(err, errn, "%s/nr_ranges is %" PRId64 " after registering %d ranges", GUARD_DIR, got, n);
		return -1;
	}
	/* step 5: the seal; from here on nothing changes until the next boot */
	if ((fd = guard_open("sealed")) < 0) {
		snprintf(err, errn, "%s/sealed: %s", GUARD_DIR, strerror(-fd));
		return -1;
	}
	w = guard_write(fd, "1\n", 2);
	guard_close(fd);
	if (w != 2) {
		snprintf(err, errn, "%s/sealed: writing 1 got %s", GUARD_DIR, w < 0 ? strerror(-w) : "a short count");
		return -1;
	}
	/* step 6: the kernel's merged ranges are ours */
	if ((e = guard_read("sealed", v, sizeof(v))) || strcmp(v, "1")) {
		snprintf(err, errn, "%s/sealed reads %s after the seal", GUARD_DIR, e ? strerror(-e) : v);
		return -1;
	}
	if ((got = guard_num("nr_ranges", err, errn)) < 0)
		return -1;
	if (got != want_ranges) {
		snprintf(err, errn, "the sealed guard has %" PRId64 " ranges, ours merge into %d", got, want_ranges);
		return -1;
	}
	if ((got = guard_num("nr_sectors", err, errn)) < 0)
		return -1;
	if (got != want_sectors) {
		snprintf(err, errn, "the sealed guard covers %" PRId64 " sectors, our targets %" PRId64, got, want_sectors);
		return -1;
	}
	say("write guard sealed: %d ranges registered in %d writes, %d after merging, %" PRId64 " sectors (%" PRId64
	    " MiB) of ps3db writable, nothing else", n, chunks, want_ranges, want_sectors, want_sectors / 2048);
	return 0;
}

/* --- device-mapper ----------------------------------------------------------------------- */
#define DM_BUF (1 << 20)

static struct dm_ioctl *dm_new(const char *name, unsigned int flags)
{
	struct dm_ioctl *io = calloc(1, DM_BUF);
	if (!io)
		die("out of memory");
	io->version[0] = DM_VERSION_MAJOR;
	io->version[1] = 0;
	io->version[2] = 0;
	io->data_size = DM_BUF;
	io->data_start = sizeof(struct dm_ioctl);
	io->flags = flags;
	if (name)
		snprintf(io->name, sizeof(io->name), "%s", name);
	return io;
}

static int dm_ctl(int ctl, unsigned long cmd, struct dm_ioctl *io)
{
	return ioctl(ctl, cmd, io);
}

static void dm_remove(int ctl, const char *name)
{
	struct dm_ioctl *io = dm_new(name, 0);
	if (dm_ctl(ctl, DM_DEV_REMOVE, io) < 0)
		fprintf(stderr, "!! %s: DM_DEV_REMOVE %s: %s\n", prog, name, strerror(errno));
	else
		say("removed the half-made /dev/mapper/%s", name);
	free(io);
}

/* creates NAME from T[0..n), resumes it, compares the kernel's table with T and puts the kernel's
 * targets into BACK[0..n); returns its dev_t or 0 with ERR filled */
static dev_t dm_make(int ctl, const char *name, const struct target *t, int n, const char *devnum,
		     struct target *back, char *err, size_t errn, int *created)
{
	struct dm_ioctl *io = dm_new(name, 0);
	dev_t dev = 0;
	*created = 0;
	if (dm_ctl(ctl, DM_DEV_CREATE, io) < 0) {
		snprintf(err, errn, "DM_DEV_CREATE %s: %s", name, strerror(errno));
		goto out;
	}
	*created = 1;
	free(io);
	/* the table: n dm_target_specs, each followed by "MAJ:MIN OFFSET", 8-byte aligned */
	io = dm_new(name, 0);
	{
		char *p = (char *)io + io->data_start, *end = (char *)io + DM_BUF;
		for (int i = 0; i < n; i++) {
			struct dm_target_spec *s = (struct dm_target_spec *)p;
			char *params = p + sizeof(*s);
			int k = snprintf(params, end - params, "%s %" PRId64, devnum, t[i].dev);
			size_t step = (sizeof(*s) + k + 1 + 7) & ~(size_t)7;
			if (k < 0 || p + step + sizeof(*s) >= end) {
				snprintf(err, errn, "table of %s too big", name);
				goto out;
			}
			s->sector_start = t[i].start;
			s->length = t[i].len;
			s->status = 0;
			s->next = step;	/* the next spec, from this one */
			snprintf(s->target_type, sizeof(s->target_type), "linear");
			p += step;
		}
		io->target_count = n;
		io->data_size = p - (char *)io;
	}
	if (dm_ctl(ctl, DM_TABLE_LOAD, io) < 0) {
		snprintf(err, errn, "DM_TABLE_LOAD %s: %s", name, strerror(errno));
		goto out;
	}
	free(io);
	io = dm_new(name, 0);	/* DM_DEV_SUSPEND without DM_SUSPEND_FLAG: resume, the table goes live */
	if (dm_ctl(ctl, DM_DEV_SUSPEND, io) < 0) {
		snprintf(err, errn, "resume %s: %s", name, strerror(errno));
		goto out;
	}
	dev = makedev(major(io->dev), minor(io->dev));
	free(io);
	/* the kernel's view of the live table, compared target by target */
	io = dm_new(name, DM_STATUS_TABLE_FLAG);
	errno = 0;
	if (dm_ctl(ctl, DM_TABLE_STATUS, io) < 0 || (io->flags & DM_BUFFER_FULL_FLAG)) {
		snprintf(err, errn, "DM_TABLE_STATUS %s: %s", name, errno ? strerror(errno) : "buffer full");
		dev = 0;
		goto out;
	}
	if ((int)io->target_count != n) {
		snprintf(err, errn, "%s: the kernel has %u targets, we loaded %d", name, io->target_count, n);
		dev = 0;
		goto out;
	}
	{
		char *base = (char *)io + io->data_start, *end = (char *)io + io->data_size, want[64];
		struct dm_target_spec *s = (struct dm_target_spec *)base;
		for (int i = 0; i < n; i++) {
			if ((char *)(s + 1) > end) {
				snprintf(err, errn, "%s: short table status", name);
				dev = 0;
				goto out;
			}
			snprintf(want, sizeof(want), "%s %" PRId64, devnum, t[i].dev);
			if ((int64_t)s->sector_start != t[i].start || (int64_t)s->length != t[i].len ||
			    strncmp(s->target_type, "linear", sizeof(s->target_type)) ||
			    strncmp((char *)(s + 1), want, end - (char *)(s + 1))) {
				snprintf(err, errn, "%s: the kernel's target %d (%llu %llu %.16s %.40s) is not ours (%" PRId64
					 " %" PRId64 " linear %s)", name, i, (unsigned long long)s->sector_start,
					 (unsigned long long)s->length, s->target_type, (char *)(s + 1), t[i].start, t[i].len, want);
				dev = 0;
				goto out;
			}
			/* what the guard registers comes from here: the kernel's own view of the table */
			const char *off = strchr((char *)(s + 1), ' ');
			char *e2;
			errno = 0;
			back[i].start = s->sector_start;
			back[i].len = s->length;
			back[i].dev = off ? strtoll(off + 1, &e2, 10) : -1;
			if (!off || errno || *e2 || back[i].dev != t[i].dev) {
				snprintf(err, errn, "%s: target %d: can't read the device offset back", name, i);
				dev = 0;
				goto out;
			}
			s = (struct dm_target_spec *)(base + s->next);	/* replies: from the data start */
		}
	}
out:
	free(io);
	return dev;
}

static void write_table(const char *run, const char *name, const struct target *t, int n)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s.table", run, name);
	FILE *f = fopen(path, "we");
	if (!f)
		return;
	for (int i = 0; i < n; i++)
		fprintf(f, "%" PRId64 " %" PRId64 " linear %s %" PRId64 "\n", t[i].start, t[i].len, DEVICE, t[i].dev);
	fclose(f);
}

static int good_name(const char *s)
{
	if (!isalnum((unsigned char)*s) || strlen(s) > 60)
		return 0;
	for (; *s; s++)
		if (!isalnum((unsigned char)*s) && !strchr("_.-", *s))
			return 0;
	return 1;
}

static int cmd_create(const char *mappath, const char *rname, const char *sname, const char *run, int guard_mode)
{
	struct map m;
	char rec[1100], line[256], bid[64], devnum[32], wnum[32], ro[128];
	int64_t rs, re, ss, se;
	struct target *rt, *st;
	int nr, ns;

	if (!good_name(rname) || !good_name(sname) || !strcmp(rname, sname))
		refuse("names %s, %s: letters, digits, _ . -, different", rname, sname);
	load_map(mappath, &m);
	if (strcmp(m.device, DEVICE))
		refuse("%s: made for %s, the boot uses %s", mappath, m.device, DEVICE);
	/* the recheck of this map, in this boot */
	read_boot_id(bid, sizeof(bid));
	record_path(run, rec, sizeof(rec));
	FILE *f = fopen(rec, "re");
	if (!f || !fgets(line, sizeof(line), f))
		refuse("no recheck in this boot (%s): ps3-imgmap-boot recheck first", rec);
	fclose(f);
	line[strcspn(line, "\n")] = 0;
	snprintf(rec, sizeof(rec), "%s %s", m.extents_sha256, bid);
	if (strcmp(line, rec))
		refuse("the recorded recheck is of another map or boot: ps3-imgmap-boot recheck first");
	/* device-mapper must claim ps3db: not while UFS holds it */
	devnum_str(blockdev_rdev(DEVICE), devnum, sizeof(devnum));
	if (mounts_of(devnum, NULL))
		refuse("%s is mounted: unmount it first", DEVICE);
	if (device_sectors(DEVICE) != m.device_sectors)
		refuse("%s has %" PRId64 " sectors, the map says %" PRId64, DEVICE, device_sectors(DEVICE), m.device_sectors);
	for (int i = 0; i < 2; i++) {
		char p[128];
		snprintf(p, sizeof(p), "/dev/mapper/%s", i ? sname : rname);
		if (access(p, F_OK) == 0)
			refuse("%s exists", p);
	}
	/* the write guard: there, unsealed and empty (or none at all: an older kernel) */
	int guard = guard_precheck(guard_mode);
	/* the whole disk read-only (GameOS's regions and VFLASH) until the next boot */
	{
		dev_t w = blockdev_rdev(WHOLE);
		int one = 1, fd = open(WHOLE, O_RDONLY | O_CLOEXEC), got = 0;
		if (fd < 0)
			refuse("%s: %s", WHOLE, strerror(errno));
		if (ioctl(fd, BLKROSET, &one) < 0 || ioctl(fd, BLKROGET, &got) < 0 || got != 1)
			refuse("%s: BLKROSET didn't take (%s)", WHOLE, strerror(errno));
		close(fd);
		devnum_str(w, wnum, sizeof(wnum));
		snprintf(ro, sizeof(ro), "/sys/dev/block/%s/ro", wnum);
		f = fopen(ro, "re");
		if (!f || !fgets(line, sizeof(line), f) || strcmp(line, "1\n"))
			refuse("%s is not read-only after BLKROSET (%s)", WHOLE, ro);
		fclose(f);
		say("%s read-only", WHOLE);
	}
	header_check(&m);
	say("header area: all %d stamps read back through the map", HEADER_SECTORS * 512 / BLOCK);
	parts(&m, &rs, &re, &ss, &se);
	nr = table(&m, rs, re, &rt);
	ns = table(&m, ss, se, &st);
	if (guard && nr + ns > GUARD_MAX)
		refuse("%d targets: more ranges than the write guard takes (%d)", nr + ns, GUARD_MAX);

	if (access(DM_CONTROL, F_OK) < 0)
		refuse("%s missing: dm-mod not loaded?", DM_CONTROL);
	int ctl = open(DM_CONTROL, O_RDWR | O_CLOEXEC);
	if (ctl < 0)
		refuse("%s: %s", DM_CONTROL, strerror(errno));
	{
		struct dm_ioctl *io = dm_new(NULL, 0);
		if (dm_ctl(ctl, DM_VERSION, io) < 0 || io->version[0] != DM_VERSION_MAJOR)
			refuse("device-mapper ioctl version: %s", strerror(errno));
		free(io);
	}
	mkdir("/dev/mapper", 0755);
	struct { const char *name; struct target *t; int n; int64_t sectors; struct target *back; } dv[2] = {
		{ rname, rt, nr, re - rs, NULL }, { sname, st, ns, se - ss, NULL },
	};
	int made[2] = { 0, 0 };
	char err[512] = "";
	for (int i = 0; i < 2; i++) {
		if (!dv[i].n)
			continue;
		dv[i].back = calloc(dv[i].n, sizeof(*dv[i].back));
		if (!dv[i].back)
			die("out of memory");
		dev_t d = dm_make(ctl, dv[i].name, dv[i].t, dv[i].n, devnum, dv[i].back, err, sizeof(err), &made[i]);
		char node[128];
		snprintf(node, sizeof(node), "/dev/mapper/%s", dv[i].name);
		if (d && mknod(node, S_IFBLK | 0660, d) < 0) {
			snprintf(err, sizeof(err), "mknod %s: %s", node, strerror(errno));
			d = 0;
		}
		if (d && try_sectors(node) != dv[i].sectors) {
			snprintf(err, sizeof(err), "%s has %" PRId64 " sectors, not %" PRId64, node, try_sectors(node), dv[i].sectors);
			d = 0;
		}
		if (!d) {
			for (int j = i; j >= 0; j--)
				if (made[j]) {
					char nn[128];
					snprintf(nn, sizeof(nn), "/dev/mapper/%s", dv[j].name);
					unlink(nn);
					dm_remove(ctl, dv[j].name);
				}
			refuse("%s", err);
		}
		write_table(run, dv[i].name, dv[i].t, dv[i].n);
		say("/dev/mapper/%s: %" PRId64 " sectors (%" PRId64 " MiB, %d extents), device %u:%u", dv[i].name,
		    dv[i].sectors, dv[i].sectors / 2048, dv[i].n, major(d), minor(d));
	}
	if (guard) {
		/* steps 4-6: the device side of every target the kernel read back, both devices */
		struct range *r = malloc(sizeof(*r) * (nr + ns));
		int k = 0;
		if (!r)
			die("out of memory");
		for (int i = 0; i < 2; i++)
			for (int j = 0; j < dv[i].n; j++) {
				r[k].start = dv[i].back[j].dev;
				r[k].len = dv[i].back[j].len;
				k++;
			}
		if (guard_register(r, k, err, sizeof(err)) < 0) {
			for (int j = 1; j >= 0; j--)
				if (made[j]) {
					char nn[1100];
					snprintf(nn, sizeof(nn), "/dev/mapper/%s", dv[j].name);
					unlink(nn);
					dm_remove(ctl, dv[j].name);
					snprintf(nn, sizeof(nn), "%s/%s.table", run, dv[j].name);
					unlink(nn);
				}
			refuse("write guard: %s. ps3db's guard can't be reset in this boot (ranges taken stay, a seal "
			       "stays): reboot", err);
		}
		free(r);
	}
	close(ctl);
	return 0;
}

static int cmd_tables(const char *mappath, const char *which)
{
	struct map m;
	int64_t rs, re, ss, se;
	struct target *t;
	int n;
	load_map(mappath, &m);
	parts(&m, &rs, &re, &ss, &se);
	if (!strcmp(which, "root"))
		n = table(&m, rs, re, &t);
	else if (!strcmp(which, "swap"))
		n = table(&m, ss, se, &t);
	else
		die("tables: root or swap");
	for (int i = 0; i < n; i++)
		printf("%" PRId64 " %" PRId64 " linear %s %" PRId64 "\n", t[i].start, t[i].len, m.device, t[i].dev);
	return 0;
}

static int cmd_check(const char *mappath)
{
	struct map m;
	int64_t rs, re, ss, se;
	load_map(mappath, &m);
	parts(&m, &rs, &re, &ss, &se);
	printf("%s: %s, %" PRId64 " bytes, uuid %s, UFS2 %s block %" PRId64 " fragment %" PRId64 ", %d extents, "
	       "root %" PRId64 " MiB, swap %" PRId64 " MiB, extents sha256 %s\n", mappath, m.image, m.file_size, m.uuid,
	       m.byte_order, m.block_size, m.fragment_size, m.nextents, (re - rs) / 2048, (se - ss) / 2048, m.extents_sha256);
	return 0;
}

#ifdef PS3_GUARD_FAKE
/* test builds: create's guard steps on the map's own tables (no devices, no dm) */
static int cmd_guard_test(const char *mappath, int mode)
{
	struct map m;
	int64_t rs, re, ss, se;
	struct target *rt, *st;
	char err[512];
	load_map(mappath, &m);
	parts(&m, &rs, &re, &ss, &se);
	int nr = table(&m, rs, re, &rt), ns = table(&m, ss, se, &st), k = 0;
	if (!guard_precheck(mode))
		return 0;
	if (nr + ns > GUARD_MAX)
		refuse("%d targets: more ranges than the write guard takes (%d)", nr + ns, GUARD_MAX);
	struct range *r = malloc(sizeof(*r) * (nr + ns + 1));
	if (!r)
		die("out of memory");
	for (int i = 0; i < nr; i++)
		r[k++] = (struct range){ rt[i].dev, rt[i].len };
	for (int i = 0; i < ns; i++)
		r[k++] = (struct range){ st[i].dev, st[i].len };
	if (guard_register(r, k, err, sizeof(err)) < 0)
		refuse("write guard: %s. ps3db's guard can't be reset in this boot (ranges taken stay, a seal "
		       "stays): reboot", err);
	return 0;
}
#endif

static void __attribute__((noreturn)) usage(void)
{
	fprintf(stderr, "usage: %s check MAP | tables MAP root|swap | recheck MAP MOUNTPOINT [--run DIR] |\n"
		"       %s create MAP [--root-name ps3root] [--swap-name ps3swap] [--run DIR] [--guard auto|required] |\n"
		"       %s map MOUNTPOINT IMAGE --uuid U --size BYTES --swap-mib N --out MAP [--run DIR] |\n"
		"       %s verify MAP | disks | install-conf FILE\n"
#ifdef PS3_GUARD_FAKE
		"       %s guard-test MAP [--guard auto|required]   (test build: the guard steps on the map's tables)\n"
#endif
		, prog, prog, prog, prog
#ifdef PS3_GUARD_FAKE
		, prog
#endif
		);
	exit(1);
}

static int64_t opt_num(const char *s)
{
	char *end;
	errno = 0;
	long long v = strtoll(s, &end, 10);
	if (!isdigit((unsigned char)*s) || *end || errno)
		usage();
	return v;
}

int main(int argc, char **argv)
{
	const char *run = RUN_DEFAULT, *rname = "ps3root", *sname = "ps3swap", *uuid = NULL, *out = NULL;
	int64_t size = -1, swap_mib = -1;
	int guard_mode = GUARD_AUTO;
	char *pos[4];
	int np = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--run") && i + 1 < argc)
			run = argv[++i];
		else if (!strcmp(argv[i], "--root-name") && i + 1 < argc)
			rname = argv[++i];
		else if (!strcmp(argv[i], "--swap-name") && i + 1 < argc)
			sname = argv[++i];
		else if (!strcmp(argv[i], "--uuid") && i + 1 < argc)
			uuid = argv[++i];
		else if (!strcmp(argv[i], "--out") && i + 1 < argc)
			out = argv[++i];
		else if (!strcmp(argv[i], "--size") && i + 1 < argc)
			size = opt_num(argv[++i]);
		else if (!strcmp(argv[i], "--swap-mib") && i + 1 < argc)
			swap_mib = opt_num(argv[++i]);
		else if (!strcmp(argv[i], "--guard") && i + 1 < argc) {
			i++;
			if (!strcmp(argv[i], "auto"))
				guard_mode = GUARD_AUTO;
			else if (!strcmp(argv[i], "required"))
				guard_mode = GUARD_REQUIRED;
			else
				usage();
		}
		else if (argv[i][0] == '-' || np == 4)
			usage();
		else
			pos[np++] = argv[i];
	}
	if (np < 1)
		usage();
	if (!strcmp(pos[0], "disks") && np == 1)
		return cmd_disks();
	if (np < 2)
		usage();
	if (!strcmp(pos[0], "check") && np == 2)
		return cmd_check(pos[1]);
	if (!strcmp(pos[0], "tables") && np == 3)
		return cmd_tables(pos[1], pos[2]);
	if (!strcmp(pos[0], "recheck") && np == 3)
		return cmd_recheck(pos[1], pos[2], run);
	if (!strcmp(pos[0], "create") && np == 2)
		return cmd_create(pos[1], rname, sname, run, guard_mode);
	if (!strcmp(pos[0], "map") && np == 3)
		return cmd_map(pos[1], pos[2], uuid, size, swap_mib, out, run);
	if (!strcmp(pos[0], "verify") && np == 2)
		return cmd_verify(pos[1]);
	if (!strcmp(pos[0], "install-conf") && np == 2)
		return cmd_install_conf(pos[1]);
#ifdef PS3_GUARD_FAKE
	if (!strcmp(pos[0], "guard-test") && np == 2)
		return cmd_guard_test(pos[1], guard_mode);
#endif
	usage();
}
