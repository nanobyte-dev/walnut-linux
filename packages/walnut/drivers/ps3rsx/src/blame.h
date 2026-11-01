/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Which submit the GPU was running when it raised a graph exception.
 *
 * Every client shares one lv1 FIFO channel (the software context switch), so the exception
 * block's channel id names our driver context, never a client. Each commit ends with a
 * SET_REFERENCE of its own sequence number, and the ring runs in sequence order, so the GPU was
 * inside the first commit whose number the reference register hasn't reached: the faulting
 * submit is the oldest recorded entry with seq > ref. The last submitter is often another client
 * (2026-11-01: mpv submitted after the rsxdrm-smoke push that trapped).
 *
 * Pure code without kernel dependencies, so test/blame-test.c builds it on the host.
 */
#ifndef PS3RSX_BLAME_H
#define PS3RSX_BLAME_H

#define PS3RSX_BLAME_N		64
#define PS3RSX_BLAME_COMM	16

struct ps3rsx_blame_ent {
	unsigned int seq;
	int chan;			/* the client's channel id, or -1: the kernel's own commit */
	unsigned long long serial;	/* that channel's serial: ids are reused, serials never */
	int pid;
	char comm[PS3RSX_BLAME_COMM];
};

struct ps3rsx_blame {
	struct ps3rsx_blame_ent e[PS3RSX_BLAME_N];
	unsigned int n;			/* entries written so far (wraps: ring of the last N) */
};

/* has the GPU passed seq? (exec.c's seq_passed: wrap-safe) */
static inline int ps3rsx_blame_passed(unsigned int ref, unsigned int seq)
{
	return (int)(ref - seq) >= 0;
}

/* a commit with sequence number seq, in ring order (exec_lock held) */
static inline void ps3rsx_blame_add(struct ps3rsx_blame *b, unsigned int seq, int chan,
				    unsigned long long serial, int pid, const char *comm)
{
	struct ps3rsx_blame_ent *e = &b->e[b->n++ % PS3RSX_BLAME_N];
	unsigned int i;

	e->seq = seq;
	e->chan = chan;
	e->serial = serial;
	e->pid = pid;
	for (i = 0; i + 1 < PS3RSX_BLAME_COMM && comm && comm[i]; i++)
		e->comm[i] = comm[i];
	e->comm[i] = 0;
}

/*
 * The commit the GPU was in at reference ref, or NULL when it can't be known: every recorded
 * commit is passed (the trap came from something else), or even the oldest recorded one isn't
 * (the faulting one may have dropped out of the ring). Nobody is blamed then.
 */
static inline const struct ps3rsx_blame_ent *ps3rsx_blame_find(const struct ps3rsx_blame *b,
								unsigned int ref)
{
	unsigned int k = b->n < PS3RSX_BLAME_N ? b->n : PS3RSX_BLAME_N;
	unsigned int first = b->n - k, j;

	for (j = first; j < b->n; j++) {
		const struct ps3rsx_blame_ent *e = &b->e[j % PS3RSX_BLAME_N];

		if (!ps3rsx_blame_passed(ref, e->seq))
			return j == first && b->n > PS3RSX_BLAME_N ? NULL : e;
	}
	return NULL;
}

#endif
