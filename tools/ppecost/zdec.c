/* zdec gz|zip|gzld|zipld|zipnocrc FILE...: what a game's loader does with them.
 * gz: gzopen/gzread in 64 KB reads (Sauerbraten's .ogz stream); zip: every deflated member of a
 * zip (pk3) inflated raw and crc32-checked (ioquake3's unzip); zipnocrc: the same without crc32;
 * gzld/zipld: the same data through libdeflate whole-buffer (crc32 checked). */
#include <zlib.h>
#include <libdeflate.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void dump_maps(void)	/* MAPS=FILE: the guest's mappings, for the PC attribution */
{
	const char *o = getenv("MAPS"); if (!o) return;
	FILE *i = fopen("/proc/self/maps", "r"), *w = fopen(o, "w"); char l[512];
	while (i && w && fgets(l, sizeof l, i)) fputs(l, w);
	if (i) fclose(i); if (w) fclose(w);
}
static unsigned char *slurp(const char *p, size_t *n)
{
	FILE *f = fopen(p, "rb"); if (!f) return NULL;
	fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
	unsigned char *b = malloc(*n); if (fread(b, 1, *n, f) != *n) { fclose(f); free(b); return NULL; }
	fclose(f); return b;
}
static unsigned le16(const unsigned char *p) { return p[0] | p[1] << 8; }
static unsigned le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
int main(int argc, char **argv)
{
	const char *mode = argv[1];
	unsigned long sum = 0, outb = 0, bad = 0;
	struct libdeflate_decompressor *ld = libdeflate_alloc_decompressor();
	for (int a = 2; a < argc; a++) {
		if (!strcmp(mode, "gz")) {
			gzFile g = gzopen(argv[a], "rb"); static unsigned char buf[65536]; int r;
			gzbuffer(g, 65536);
			while ((r = gzread(g, buf, sizeof buf)) > 0) { outb += r; sum += buf[0] + buf[r - 1]; }
			gzclose(g);
			continue;
		}
		size_t n; unsigned char *b = slurp(argv[a], &n);
		if (!strcmp(mode, "gzld")) {
			size_t ol = le32(b + n - 4), got; unsigned char *o = malloc(ol);
			if (libdeflate_gzip_decompress(ld, b, n, o, ol, &got)) bad++; else { outb += got; sum += o[0] + o[got - 1]; }
			free(o); free(b); continue;
		}
		/* zip: walk the central directory */
		size_t e = n - 22; while (e > 0 && le32(b + e) != 0x06054b50) e--;
		unsigned cnt = le16(b + e + 10); size_t cd = le32(b + e + 16);
		for (unsigned i = 0; i < cnt; i++) {
			unsigned meth = le16(b + cd + 10), crc = le32(b + cd + 16), cs = le32(b + cd + 20), us = le32(b + cd + 24);
			unsigned fnl = le16(b + cd + 28), exl = le16(b + cd + 30), cml = le16(b + cd + 32);
			size_t lo = le32(b + cd + 42);
			const unsigned char *d = b + lo + 30 + le16(b + lo + 26) + le16(b + lo + 28);
			cd += 46 + fnl + exl + cml;
			if (meth != 8 || !us) continue;
			unsigned char *o = malloc(us);
			if (!strcmp(mode, "zipld")) {
				size_t got;
				if (libdeflate_deflate_decompress(ld, d, cs, o, us, &got) || libdeflate_crc32(0, o, us) != crc) bad++;
			} else {
				z_stream z; memset(&z, 0, sizeof z);
				inflateInit2(&z, -MAX_WBITS);
				z.next_in = (unsigned char *)d; z.avail_in = cs; z.next_out = o; z.avail_out = us;
				if (inflate(&z, Z_FINISH) != Z_STREAM_END) bad++;
				inflateEnd(&z);
				if (strcmp(mode, "zipnocrc") && crc32(0, o, us) != crc) bad++;
			}
			outb += us; sum += o[0] + o[us - 1];
			free(o);
		}
		free(b);
	}
	printf("%s bytes=%lu checksum=%lu bad=%lu zlib=%s\n", mode, outb, sum, bad, zlibVersion());
	dump_maps();
	return 0;
}
