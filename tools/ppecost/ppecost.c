/* ppecost: a qemu TCG plugin that counts what an in-order PPE (the PS3's Cell) pays for besides
 * instructions: guest instructions, loads, stores, load-hit-stores, integer multiplies and divides
 * (docs/PROFILING.md "Load-time decoders and linking"). The PPE has no moves between its integer,
 * float and vector registers, so a conversion stores and reloads, and the load waits about 40 cycles
 * for the store to drain. A load-hit-store here is a load from an 8-byte granule a store wrote within
 * the last W guest instructions (window=, default 24), or with exact=1 per PC only loads whose bytes
 * overlap that store's. Per guest PC at exit to out=FILE ("pc insns lhs kind", kind 1 multiply,
 * 2 divide); totals to stderr. attrib.py sums the file per function with the guest's
 * /proc/self/maps (the harnesses here write it when DUMPMAPS=FILE is set). The crude PPE estimate
 * used in the docs: insns + 40 x lhs + 8 x mul + 40 x div.
 * Build against a qemu built with --enable-plugins (10.2.2 here):
 *   gcc -O2 -shared -fPIC -o ppecost.so ppecost.c -I<qemu>/include/qemu $(pkg-config --cflags glib-2.0)
 * Run: qemu-ppc64 -L <ppc64 root> -cpu power7 -plugin ./ppecost.so,exact=1,out=pc.txt -d plugin PROG
 * (-cpu 970, closer to the PPE, crashes glibc's malloc under qemu 10.2.2.) Harnesses: oalbench.c
 * (OpenAL Soft's mixer via a loopback device), alsaconv.c (ALSA's plug float to S16), vorbisbench.c
 * (ov_read), fsbench.c (fluidsynth's render), pngdec.c, zdec.c, jpgdec.c (decoders), paeth.c (libpng's
 * Paeth with the left pixel in registers). */
#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct { uint64_t pc; uint64_t n, lhs, lhsx; int kind; } Rec;	/* kind 1 mul, 2 div */
#define RING 64
typedef struct { uint64_t g[RING], t[RING]; unsigned h; uint64_t xa[RING], xt[RING]; unsigned xs[RING], xh; uint64_t icount, loads, stores, lhs, lhsx, mul, div; } Vcpu;
static Vcpu vc[256];
static GHashTable *recs;
static GMutex lock;
static uint64_t window = 24;
static int exact;	/* exact=1: the per-PC lhs column counts byte overlaps only */
static const char *outname;

static void on_exec(unsigned int v, void *ud)
{
	Rec *r = ud; Vcpu *c = &vc[v & 255];
	c->icount++; r->n++;
	if (r->kind == 1) c->mul++; else if (r->kind == 2) c->div++;
}

static void on_mem(unsigned int v, qemu_plugin_meminfo_t info, uint64_t va, void *ud)
{
	Rec *r = ud; Vcpu *c = &vc[v & 255];
	uint64_t g = va >> 3;
	unsigned sz = 1u << qemu_plugin_mem_size_shift(info);
	uint64_t g2 = (va + sz - 1) >> 3;
	if (qemu_plugin_mem_is_store(info)) {
		c->stores++;
		c->g[c->h % RING] = g; c->t[c->h % RING] = c->icount; c->h++;
		if (g2 != g) { c->g[c->h % RING] = g2; c->t[c->h % RING] = c->icount; c->h++; }
		c->xa[c->xh % RING] = va; c->xs[c->xh % RING] = sz; c->xt[c->xh % RING] = c->icount; c->xh++;
		return;
	}
	c->loads++;
	for (unsigned i = 0; i < RING; i++)	/* exact: a recent store's bytes overlap the load's */
		if (c->xt[i] && c->icount - c->xt[i] <= window && va < c->xa[i] + c->xs[i] && c->xa[i] < va + sz) {
			c->lhsx++; r->lhsx++; break;
		}
	for (unsigned i = 0; i < RING; i++)
		if ((c->g[i] == g || c->g[i] == g2) && c->icount - c->t[i] <= window && c->t[i]) {
			c->lhs++; r->lhs++; break;
		}
}

static void tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
	size_t n = qemu_plugin_tb_n_insns(tb);
	for (size_t i = 0; i < n; i++) {
		struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
		uint64_t pc = qemu_plugin_insn_vaddr(insn);
		g_mutex_lock(&lock);
		Rec *r = g_hash_table_lookup(recs, &pc);
		if (!r) {
			r = g_new0(Rec, 1); r->pc = pc;
			char *d = qemu_plugin_insn_disas(insn);
			if (d && (!strncmp(d, "mul", 3))) r->kind = 1;
			else if (d && !strncmp(d, "div", 3)) r->kind = 2;
			g_free(d);
			if (!r->kind) {	/* qemu's ppc plugin disassembly may be empty: decode the word */
				uint8_t b[4];
				if (qemu_plugin_insn_data(insn, b, 4) == 4) {
					uint32_t w = (uint32_t)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
					unsigned op = w >> 26, xo = (w >> 1) & 0x1ff;
					if (op == 7 || (op == 31 && (xo == 235 || xo == 233 || xo == 75 || xo == 11 || xo == 73 || xo == 9)))
						r->kind = 1;
					else if (op == 31 && (xo == 491 || xo == 459 || xo == 489 || xo == 457 || xo == 427 || xo == 395 || xo == 425 || xo == 393))
						r->kind = 2;
				}
			}
			g_hash_table_insert(recs, &r->pc, r);
		}
		g_mutex_unlock(&lock);
		qemu_plugin_register_vcpu_insn_exec_cb(insn, on_exec, QEMU_PLUGIN_CB_NO_REGS, r);
		qemu_plugin_register_vcpu_mem_cb(insn, on_mem, QEMU_PLUGIN_CB_NO_REGS, QEMU_PLUGIN_MEM_RW, r);
	}
}

static void at_exit(qemu_plugin_id_t id, void *p)
{
	uint64_t ic = 0, ld = 0, st = 0, lhs = 0, lhsx = 0, mul = 0, div = 0;
	for (int i = 0; i < 256; i++) {
		ic += vc[i].icount; ld += vc[i].loads; st += vc[i].stores; lhs += vc[i].lhs; lhsx += vc[i].lhsx; mul += vc[i].mul; div += vc[i].div;
	}
	char buf[512];
	snprintf(buf, sizeof buf, "ppecost: insns %" PRIu64 " loads %" PRIu64 " stores %" PRIu64 " lhs %" PRIu64
		" lhs_exact %" PRIu64 " mul %" PRIu64 " div %" PRIu64 " window %" PRIu64 "\n", ic, ld, st, lhs, lhsx, mul, div, window);
	qemu_plugin_outs(buf);
	if (outname) {
		FILE *f = fopen(outname, "w");
		if (f) {
			GHashTableIter it; gpointer k, v;
			g_hash_table_iter_init(&it, recs);
			while (g_hash_table_iter_next(&it, &k, &v)) {
				Rec *r = v;
				if (r->n) fprintf(f, "%" PRIx64 " %" PRIu64 " %" PRIu64 " %d\n", r->pc, r->n, exact ? r->lhsx : r->lhs, r->kind);
			}
			fclose(f);
		}
	}
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info, int argc, char **argv)
{
	for (int i = 0; i < argc; i++) {
		if (!strncmp(argv[i], "out=", 4)) outname = strdup(argv[i] + 4);
		else if (!strncmp(argv[i], "exact=", 6)) exact = atoi(argv[i] + 6);
		else if (!strncmp(argv[i], "window=", 7)) window = strtoull(argv[i] + 7, NULL, 10);
	}
	recs = g_hash_table_new(g_int64_hash, g_int64_equal);
	qemu_plugin_register_vcpu_tb_trans_cb(id, tb_trans);
	qemu_plugin_register_atexit_cb(id, at_exit, NULL);
	return 0;
}
