/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The push dump's record format and its trigger scanner (pushdump.c, a test build's capture of
 * what clients submit: docs/DRIVER.md, test builds). Pure code without kernel dependencies, so
 * test/pushdump-test.c builds it on the host, and test/pushdump-split.py reads what it writes.
 *
 * Records: big-endian 32-bit words, each starting with PD_MAGIC, its type, its length in words
 * (the header included) and the reference (seqno) it belongs to; the layout of each type is
 * below its enum value. Byte strings (comm, contents) are padded to whole words.
 *
 * The scanner walks pushes the validator accepted and collects what the GPU will fetch from
 * memory the pushes don't carry: each fragment program bind (0x8e4) and each index batch's range
 * of the index buffer (0x1824 against 0x181c/0x1820). Its state follows the channel: binds per
 * subchannel and the index buffer's offset and format, which a submit may have set before.
 */
#ifndef PS3RSX_PUSHDUMP_SCAN_H
#define PS3RSX_PUSHDUMP_SCAN_H

#define PD_MAGIC	0x50344452	/* "P4DR" */
#define PD_VERSION	1
#define PD_HDR_WORDS	4

enum pd_type {
	PD_START = 1,	/* version, info.reserved, memory_size, gart_size, CPU_VRAM_LIMIT, RING_RESERVE */
	PD_OPEN,	/* owner id, tgid, kind, domain, offset hi, offset lo, size: a handle made */
	PD_CLOSE,	/* the same fields: a handle deleted */
	PD_SUBMIT,	/* channel id, channel serial, owner id, tgid, verdict, bad push (or ~0),
			 * nr_push, nr_buffers, comm (4 words), then per buffer: handle, kind,
			 * domain, offset, size, read domains, write domains (7 words) */
	PD_RESTORE,	/* channel id, word count, the words */
	PD_PUSH,	/* channel id, push index, word count, the words (after relocations) */
	PD_END,		/* channel id, the two words: ps3rsx ended the channel's open primitive */
	PD_CONTENT,	/* channel id, what, domain, address, length asked, status, length read,
			 * the bytes (padded) */
	PD_GAP,		/* records dropped since the last one (the ring was full) */
};

enum pd_verdict { PD_ACCEPTED, PD_REJECTED, PD_NOT_SENT };
enum pd_what { PD_FP = 1, PD_INDEX = 2 };
enum pd_dom { PD_DOM_VRAM = 1, PD_DOM_GART = 2 };
enum pd_status {
	PD_READ_CPU = 1,	/* read after the submit's fence passed */
	PD_READ_EARLY,		/* read without the fence passing (waiting timed out or was off) */
	PD_UNREADABLE,		/* GPU-only VRAM: no CPU mapping */
	PD_NO_BO,		/* not inside a BO of the submit's list */
	PD_CUT,			/* PD_READ_* but cut at the per-submit content cap */
};

/* Store a big-endian word (the host test runs little-endian) */
static inline void pd_put32(void *p, u32 v)
{
	u8 *b = p;

	b[0] = v >> 24;
	b[1] = v >> 16;
	b[2] = v >> 8;
	b[3] = v;
}

static inline u32 pd_get32(const void *p)
{
	const u8 *b = p;

	return (u32)b[0] << 24 | (u32)b[1] << 16 | (u32)b[2] << 8 | b[3];
}

/* A record's header into the first PD_HDR_WORDS words at p */
static inline void pd_header(void *p, u32 type, u32 words, u32 seq)
{
	pd_put32(p, PD_MAGIC);
	pd_put32((u8 *)p + 4, type);
	pd_put32((u8 *)p + 8, words);
	pd_put32((u8 *)p + 12, seq);
}

/*
 * The records, header and fixed fields (the variable parts follow: buffers, words, bytes).
 * Each builder fills PD_*_BYTES at r; pushdump.c writes them, test/pushdump-test.c too.
 */
#define PD_START_BYTES		((PD_HDR_WORDS + 6) * 4)
#define PD_BO_EVENT_BYTES	((PD_HDR_WORDS + 7) * 4)
#define PD_SUBMIT_BYTES		((PD_HDR_WORDS + 12) * 4)
#define PD_BO_BYTES		(7 * 4)
#define PD_RESTORE_BYTES	((PD_HDR_WORDS + 2) * 4)
#define PD_PUSH_BYTES		((PD_HDR_WORDS + 3) * 4)
#define PD_END_BYTES		((PD_HDR_WORDS + 3) * 4)
#define PD_CONTENT_BYTES	((PD_HDR_WORDS + 7) * 4)
#define PD_GAP_BYTES		((PD_HDR_WORDS + 2) * 4)

static inline void pd_rec_start(u8 *r, u32 seq, u32 vram_reserved, u32 memory_size,
				u32 gart_size, u32 cpu_vram_limit, u32 ring_reserve)
{
	pd_header(r, PD_START, PD_START_BYTES / 4, seq);
	pd_put32(r + 16, PD_VERSION);
	pd_put32(r + 20, vram_reserved);
	pd_put32(r + 24, memory_size);
	pd_put32(r + 28, gart_size);
	pd_put32(r + 32, cpu_vram_limit);
	pd_put32(r + 36, ring_reserve);
}

static inline void pd_rec_bo_event(u8 *r, bool open, u32 seq, u32 owner, u32 tgid, u32 kind,
				   u32 dom, u64 offset, u32 size)
{
	pd_header(r, open ? PD_OPEN : PD_CLOSE, PD_BO_EVENT_BYTES / 4, seq);
	pd_put32(r + 16, owner);
	pd_put32(r + 20, tgid);
	pd_put32(r + 24, kind);
	pd_put32(r + 28, dom);
	pd_put32(r + 32, (u32)(offset >> 32));
	pd_put32(r + 36, (u32)offset);
	pd_put32(r + 40, size);
}

/* comm: 16 bytes, NUL-padded */
static inline void pd_rec_submit(u8 *r, u32 seq, u32 chan, u32 serial, u32 owner, u32 tgid,
				 u32 verdict, u32 bad_push, u32 nr_push, u32 nr_buffers,
				 const char *comm)
{
	pd_header(r, PD_SUBMIT, PD_SUBMIT_BYTES / 4 + nr_buffers * (PD_BO_BYTES / 4), seq);
	pd_put32(r + 16, chan);
	pd_put32(r + 20, serial);
	pd_put32(r + 24, owner);
	pd_put32(r + 28, tgid);
	pd_put32(r + 32, verdict);
	pd_put32(r + 36, bad_push);
	pd_put32(r + 40, nr_push);
	pd_put32(r + 44, nr_buffers);
	memcpy(r + 48, comm, 16);
}

static inline void pd_rec_bo(u8 *r, u32 handle, u32 kind, u32 dom, u32 offset, u32 size,
			     u32 read_domains, u32 write_domains)
{
	pd_put32(r, handle);
	pd_put32(r + 4, kind);
	pd_put32(r + 8, dom);
	pd_put32(r + 12, offset);
	pd_put32(r + 16, size);
	pd_put32(r + 20, read_domains);
	pd_put32(r + 24, write_domains);
}

static inline void pd_rec_restore(u8 *r, u32 seq, u32 chan, u32 nwords)
{
	pd_header(r, PD_RESTORE, PD_RESTORE_BYTES / 4 + nwords, seq);
	pd_put32(r + 16, chan);
	pd_put32(r + 20, nwords);
}

static inline void pd_rec_push(u8 *r, u32 seq, u32 chan, u32 index, u32 nwords)
{
	pd_header(r, PD_PUSH, PD_PUSH_BYTES / 4 + nwords, seq);
	pd_put32(r + 16, chan);
	pd_put32(r + 20, index);
	pd_put32(r + 24, nwords);
}

static inline void pd_rec_end(u8 *r, u32 seq, u32 chan, u32 header)
{
	pd_header(r, PD_END, PD_END_BYTES / 4, seq);
	pd_put32(r + 16, chan);
	pd_put32(r + 20, header);
	pd_put32(r + 24, 0);
}

/* got: the bytes that follow (padded to whole words) */
static inline void pd_rec_content(u8 *r, u32 seq, u32 chan, u32 what, u32 dom, u32 addr,
				  u32 want, u32 status, u32 got)
{
	pd_header(r, PD_CONTENT, PD_CONTENT_BYTES / 4 + (got + 3) / 4, seq);
	pd_put32(r + 16, chan);
	pd_put32(r + 20, what);
	pd_put32(r + 24, dom);
	pd_put32(r + 28, addr);
	pd_put32(r + 32, want);
	pd_put32(r + 36, status);
	pd_put32(r + 40, got);
}

static inline void pd_rec_gap(u8 *r, u32 seq, u64 dropped)
{
	pd_header(r, PD_GAP, PD_GAP_BYTES / 4, seq);
	pd_put32(r + 16, (u32)(dropped >> 32));
	pd_put32(r + 20, (u32)dropped);
}

/* What one submit's pushes make the GPU fetch, merged */
#define PD_MAX_RANGES	64

struct pd_range {
	u8 what;		/* PD_FP or PD_INDEX */
	u8 dom;			/* PD_DOM_* */
	u8 isize;		/* index bytes (PD_INDEX) */
	u32 base;		/* FP: the bound address; INDEX: IDXBUF_OFFSET */
	u64 lo, hi;		/* byte range [lo, hi) in the domain */
};

/* The channel's state as the scanner follows it (only accepted submits change it) */
struct pd_chan_state {
	u32 bound[8];
	u32 idx_off, idx_fmt;
	bool idx_off_set, idx_fmt_set;
};

/* A scan: the channel's state, and where the ranges go (cap 0: the state only) */
struct pd_scan {
	struct pd_chan_state st;
	struct pd_range *r;
	u32 cap, n;
	u32 dropped;		/* ranges that didn't fit */
	u32 fp_max;		/* bytes to take from a program's start */
};

static inline void pd_scan_begin(struct pd_scan *s, const struct pd_chan_state *st,
				 struct pd_range *r, u32 cap, u32 fp_max)
{
	s->st = *st;
	s->r = r;
	s->cap = cap;
	s->n = 0;
	s->dropped = 0;
	s->fp_max = fp_max;
}

/* Add or merge a range: one entry per program address, one per (index buffer, format) */
static inline void pd_add(struct pd_scan *s, u8 what, u8 dom, u8 isize, u32 base, u64 lo, u64 hi)
{
	u32 i;

	for (i = 0; i < s->n; i++) {
		struct pd_range *r = &s->r[i];

		if (r->what != what || r->dom != dom || r->base != base || r->isize != isize)
			continue;
		if (lo < r->lo)
			r->lo = lo;
		if (hi > r->hi)
			r->hi = hi;
		return;
	}
	if (s->n == s->cap) {
		s->dropped++;
		return;
	}
	s->r[s->n++] = (struct pd_range){ what, dom, isize, base, lo, hi };
}

#define PD_OBJ_3D	0x31337000

static inline void pd_method(struct pd_scan *s, u32 subch, u32 m, u32 v)
{
	if (m == 0) {
		s->st.bound[subch] = v;
		return;
	}
	if (s->st.bound[subch] != PD_OBJ_3D)
		return;
	switch (m) {
	case 0x08e4: {		/* FP_ACTIVE_PROGRAM: location in bits 0-1 */
		u32 loc = v & 3;

		if (loc == 1 || loc == 2)
			pd_add(s, PD_FP, loc == 1 ? PD_DOM_VRAM : PD_DOM_GART, 0, v & ~3u,
			       v & ~3u, (u64)(v & ~3u) + s->fp_max);
		break;
	}
	case 0x181c:
		s->st.idx_off = v;
		s->st.idx_off_set = true;
		break;
	case 0x1820:
		s->st.idx_fmt = v;
		s->st.idx_fmt_set = true;
		break;
	case 0x1824: {		/* VB_INDEX_BATCH: start 0-23, count - 1 in 24-31 */
		u8 isize = ((s->st.idx_fmt >> 4) & 0xf) == 1 ? 2 : 4;
		u64 start = v & 0xffffff, count = (v >> 24) + 1;

		if (!s->st.idx_off_set || !s->st.idx_fmt_set)
			break;
		pd_add(s, PD_INDEX, (s->st.idx_fmt & 1) ? PD_DOM_GART : PD_DOM_VRAM, isize,
		       s->st.idx_off, s->st.idx_off + start * isize,
		       s->st.idx_off + (start + count) * isize);
		break;
	}
	}
}

/*
 * One push the validator accepted (so its headers are plain method headers and its bursts
 * complete; anything else ends the scan of the push)
 */
static inline void pd_scan_push(struct pd_scan *s, const u32 *w, u32 nwords)
{
	u32 i = 0, k;

	while (i < nwords) {
		u32 h = w[i], count, subch, mthd;
		bool noninc;

		if (h == 0) {
			i++;
			continue;
		}
		if ((h & 0xe0030003) == 0)
			noninc = false;
		else if ((h & 0xe0030003) == 0x40000000)
			noninc = true;
		else
			return;
		count = (h >> 18) & 0x7ff;
		subch = (h >> 13) & 7;
		mthd = h & 0x1ffc;
		if (i + 1 + count > nwords)
			return;
		for (k = 0; k < count; k++)
			pd_method(s, subch, noninc ? mthd : mthd + 4 * k, w[i + 1 + k]);
		i += 1 + count;
	}
}

#endif
