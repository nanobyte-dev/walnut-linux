/*
 * rsx-clip-probe: which RSX state bounds render target writes (the surface-clip probe of
 * design-p4 7.2/16, with the rt-followups review's L3 safety changes).
 *
 *   rsx-clip-probe --list               the cases, their extents and worst-case reach (no GPU)
 *   rsx-clip-probe [options] [CASE...]  run the cases (all, in the order below, by default)
 *     --timeout-ms N   per-case limit on the fence wait (default 2000); past it the run stops
 *     --keep-going     don't stop after a case that wrote outside every prediction
 *
 * Run as a user with the render node (the desktop user), desktop stopped, the probe the only GPU
 * client, the user at the console, last in the session. It only uses ps3rsx's normal DRM
 * interface (GEM_NEW, mmap, CHANNEL_ALLOC, GEM_PUSHBUF, GEM_CPU_PREP) and 3D methods Mesa sends;
 * no lv1 call, no register or device page, no readback other than the BOs' own mappings.
 *
 * Why ps3rsx needs it: the validator bounds a clear's or draw's render target and zeta writes by
 * the union of the surface clip (RT_HORIZ/VERT 0x200/0x204), the window offset plus window clip
 * (0x2b8, 0x2c0/0x2c4) and the y flip's height (COORD_CONVENTIONS 0x1d88 bits 0-11), because
 * which of them the RSX obeys is undocumented. Each case below sets them so that the hypotheses
 * write different pixels, draws or clears once, and compares the pixels that changed with each
 * hypothesis. Output: per case a block, then a "RESULT <case> <tag>" line; a SUMMARY at the end
 * answers the questions (Q lines).
 *
 * Safety by construction. Both BOs (colour and zeta) are 16 MiB of GART with the target at
 * lo = BO + 4 MiB (32 MiB of the 64 MiB CMA GART in all), and every linear target has pitch 64
 * (16 A8R8G8B8 pixels a row). Then any write lands inside whose row is in [-65536, 131072) (a
 * flip, or a negative row wrapped at 12, 16 or 17 bits: L3's concern and the probe review's L1;
 * wraps at 26 bits or more alias to just before lo, mod 2^32) and whose column is below 65536:
 * at most 4 MiB before lo and 131071 * 64 + 65536 * 4 = 8,650,688 bytes past it, of the 12 MiB
 * there, whatever clip the RSX does or doesn't apply,
 * for draws and for clears (whose extent nothing but the clips and the ambient clear rectangle
 * bound: 0x1d98/0x1d9c aren't in the allowlist). Swizzled cases (P2s, S4096): columns below
 * 65536 plus the offset, laid out block after block (rt-followups L2's model), reach at most
 * 262,208 (P2s) and 278,528 (S4096) bytes past lo; their rows stay inside the block (the quad's
 * rows, no y offset, no flip). The quads themselves lie in [0, 16) x [0, 40) window pixels,
 * all non-negative (L3 b), and the window offsets are at most (4, 8), apart from S4096's 4095.
 * The program checks every case against these bounds before it submits anything (selfcheck()),
 * and every case passes the shipping validator's union check (fb_extent(), ps3rsx 0.1570; the
 * host test tests/clip-probe-check.sh runs the streams through validate.c), so ps3rsx refuses
 * nothing. Depth and stencil tests and writes are off; zeta sits in its own BO (same pitch),
 * which must stay untouched. The swizzled cases send pitch 4096 for colour and zeta, as Mesa
 * does for every swizzled surface (nv30_miptree_surface_new); the validator ignores it, and if
 * the RSX used it for rows, rows 0-1 reach lo + 4 KiB. Before any driver ioctl the probe checks
 * with DRM_IOCTL_VERSION that the node is ps3rsx ("nouveau", "PS3 RSX (nouveau ABI on lv1)").
 *
 * The cases (surface pixels; window pixels = surface pixels minus the window offset if the RSX
 * adds it; quads drawn through a 64x64 viewport transform):
 *
 *   P0   control draw: clips 16x48, quad 12x40. Must write the quad, or no draw result counts.
 *   P1a  surface clip 4x16, window clip 8x32, quad 12x40: surface / window / no clip bounds.
 *   P1b  surface clip 8x32, window clip 4x16: the same with the roles swapped.
 *   P1c  clips 8x32, scissor 4x4: the scissor bounds draws (sanity; Mesa relies on it).
 *   P1v  clips 8x32, VIEWPORT_HORIZ/VERT 4x4 (transform still 64): the viewport rectangle.
 *   C0   control clear: clips 16x48. Must clear them.
 *   C1   clear, surface clip 4x16, window clip 8x32 (C2 swapped, C3 scissor 4x4): as P1a-P1c.
 *   P2   surface clip 8x24, window offset (4,8), window clip 12x40: the surface clip in surface
 *        coordinates (after the offset: cols 4-7, rows 8-23) or in window coordinates (before
 *        it: cols 4-11, rows 8-31; what b60b773c's WIN_OFF rule guarded).
 *   P2w  surface clip 16x48, offset (4,8), window clip 8x24: the window clip in window
 *        coordinates (cols 4-11, rows 8-31) or in surface coordinates (cols 4-7, rows 8-23;
 *        Mesa's nv30_clear_origin assumes this one, nv30_validate_fb's small-surface hack the
 *        other).
 *   C4   clear as P2; C4w clear as P2w (a clear has no geometry: the clips alone bound it).
 *   P3   pitch 64, COORD 0x1000|16 (inverted origin, height 16), quads at rows 0-7 and 20-27:
 *        no write flip (rows 0-7 and 20-27), a flip clipped to the target (rows 8-15), a flip
 *        before lo (rows 8-15 and -12..-5), or a 12, 16 or 17-bit wrap of those rows.
 *   P3b  COORD 0x1000|16, window offset y 8, quad rows 0-3: no flip (rows 8-11), flip then
 *        offset (20-23), offset then flip (4-7), the offset ignored (0-3 or flipped 12-15).
 *   P3z  COORD 0x1000 (height 0: Mesa's COORD for a 4096-tall target, review M1), quad rows
 *        0-7: no flip, or rows -8..-1 before lo (the deepest flip), or a wrap.
 *   C5   clear with COORD 0x1000|16; C5z with COORD 0x1000: as P3/P3z for clears.
 *   P2s  Mesa 0060's 16x2 R5G6B5 swizzled block at offset 8, window clip 8: writes texels 8-15
 *        (bytes lo+32..63) if the window clip bounds; texels 16-23 past the block (L2's
 *        behaviours 1-3) land at lo+64..95 under the usual Morton layout. Runs just before S4096:
 *        a swizzled target is the least Mesa-like after it, and the flip cases go first.
 *   S4096 swizzled 4096x1 A8R8G8B8, window offset 4095, window clip 1 pixel, quad 16x1 (the
 *        rt-followups L2 case the validator accepts): the last texel only (lo+16380), or columns
 *        past the block (lo+16384..), or wrapped to the block's start. Not a shape Mesa sends
 *        (Mesa 0013 draws 1-high levels as linear rows), so it runs last.
 *
 * Left out because the shipping validator refuses them (they need the count-only build):
 * P2s with Mesa's full 16-wide window clip (columns 8 + 16 = 24 > the block's 16: "drawing
 * outside a swizzled surface"), and P4, a linear pitch of 0x10040 (above 0x10000: refused by the
 * pitch rule). The quads from -32 of the first plan are left out by design (L3 b).
 *
 * What each result means for the extent (design-p4 7.2):
 * - Q1 and Q3 yes (the surface clip bounds draws and clears) with Q5/Q7 "after the offset":
 *   7.2 can drop the window term; with Q12/Q14 "no flip" also the COORD height term. The extent
 *   becomes the surface clip alone, which is what Mesa's transfer blits into small swizzled
 *   textures need (quakespasm, etr), and b60b773c's WIN_OFF rule is not needed.
 * - Q5/Q7 "before the offset" (window coordinates): the surface term must add the window offset
 *   (b60b773c's rule returns, after the Mesa pad for 0013's linear rows), and Q15/Q16 say where
 *   swizzled columns past the block land (the swizzled exemption, or Mesa's 32-wide block).
 * - Q1 and Q3 no but Q2 and Q4 yes: the window rectangle bounds, the surface term can go and the
 *   window term (offset + clip, in the coordinates Q6/Q8 report) stays.
 * - Neither clip bounds (Q1-Q4 all no): the extent bounds nothing; the viewport (Q9) or scissor
 *   (Q10/Q11) would have to become checked fields. Stop and report.
 * - Q12/Q14 "yes, before lo" or a wrap: the inverted origin flips writes, so the rows-past-height
 *   rule returns (after Mesa masks the height); "no flip": the height term can go.
 * Each change to the validator still gets its own review and p4-test cases.
 *
 * Stops (exit 3), with nothing submitted after: a fence that doesn't signal within the timeout,
 * or one that ended with an error (-EIO), any submit error other than a refusal, a write to the
 * zeta BO, or (unless --keep-going) a write outside every prediction of the case. A refused
 * submit (-EINVAL: the validator; journalctl -k says why) is printed and the case skipped: each
 * case runs on a fresh channel, so a refusal doesn't carry over. Nothing is ever retried.
 * After a timeout or a fence error, journalctl -k says which: "recovered" is a graph exception,
 * recovered in place (unwedge.c); "GPU stopped ... wedged until reboot" is a silent stall
 * (exec.c, 2 s without progress): reboot before starting the desktop (ps3rsx keeps the GPU from
 * ps3fb, and X would come up display-only).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "rsxdrm.h"

/* drm.h's DRM_IOCTL_VERSION (core DRM, valid on every driver) */
struct drm_version {
	int version_major, version_minor, version_patchlevel;
	size_t name_len;
	char *name;
	size_t date_len;
	char *date;
	size_t desc_len;
	char *desc;
};
#define DRM_IOCTL_VERSION	_IOWR('d', 0x00, struct drm_version)

#define MIB		(1u << 20)
#define BO_SIZE		(16 * MIB)	/* colour and zeta BOs, GART */
#define LO		(4 * MIB)	/* the target's offset in its BO: 65536 rows of 64 before it */
#define NDW		(BO_SIZE / 4)
#define NBW		(NDW / 64)
#define PAT(i)		(0x5a000000u ^ (uint32_t)(i))	/* dword i of a BO before the case */
#define CLEAR_COLOUR	0xffc0ffeeu

enum { DRAW, CLEAR };
enum { LIN, SWZ16X2, SWZ4096X1 };

struct rect { int x0, y0, x1, y1; };	/* [x0, x1) x [y0, y1), pixels */
struct hyp { const char *tag, *what; int n; struct rect r[2]; };

struct pcase {
	const char *name, *what;
	int kind, fmt;
	uint32_t pitch;			/* colour and zeta, bytes */
	int cx, cw, cy, ch;		/* surface clip */
	int wox, woy;			/* window offset */
	int wx0, wx1, wy0, wy1;		/* window clip, inclusive */
	uint32_t coord;
	int vp, sc;			/* VIEWPORT_HORIZ/VERT and scissor size at 0 (0: 4096) */
	int nq;
	struct rect q[2];		/* quads, window pixels inside [0, 64) */
	int nh;
	struct hyp h[8];
};

#define R(a, b, c, d)	{ a, b, c, d }
#define Q		R(0, 0, 12, 40)	/* the quad of the unflipped linear draws */
#define UNB		{ "unbounded", "none of the clips (up to 4096x4096)", 1, { R(0, 0, 4096, 4096) } }

static const struct pcase cases[] = {
	{ "P0", "control draw: clips 16x48, quad 12x40", DRAW, LIN, 64, 0, 16, 0, 48, 0, 0, 0, 15, 0, 47,
	  48, 0, 0, 1, { Q }, 1, {
		{ "quad", "the whole quad: the draw path works", 1, { R(0, 0, 12, 40) } } } },
	{ "P1a", "draw: surface clip 4x16, window clip 8x32, quad 12x40", DRAW, LIN, 64, 0, 4, 0, 16,
	  0, 0, 0, 7, 0, 31, 16, 0, 0, 1, { Q }, 3, {
		{ "surface", "the surface clip bounds", 1, { R(0, 0, 4, 16) } },
		{ "window", "the window clip bounds", 1, { R(0, 0, 8, 32) } },
		{ "none", "neither clip bounds (the quad)", 1, { R(0, 0, 12, 40) } } } },
	{ "P1b", "draw: surface clip 8x32, window clip 4x16, quad 12x40", DRAW, LIN, 64, 0, 8, 0, 32,
	  0, 0, 0, 3, 0, 15, 32, 0, 0, 1, { Q }, 3, {
		{ "surface", "the surface clip bounds", 1, { R(0, 0, 8, 32) } },
		{ "window", "the window clip bounds", 1, { R(0, 0, 4, 16) } },
		{ "none", "neither clip bounds (the quad)", 1, { R(0, 0, 12, 40) } } } },
	{ "P1c", "draw: clips 8x32, scissor 4x4, quad 12x40", DRAW, LIN, 64, 0, 8, 0, 32, 0, 0,
	  0, 7, 0, 31, 32, 0, 4, 1, { Q }, 3, {
		{ "scissor", "the scissor bounds", 1, { R(0, 0, 4, 4) } },
		{ "clips", "the clips bound, not the scissor", 1, { R(0, 0, 8, 32) } },
		{ "none", "nothing bounds (the quad)", 1, { R(0, 0, 12, 40) } } } },
	{ "P1v", "draw: clips 8x32, viewport rectangle 4x4 (transform 64x64), quad 12x40", DRAW, LIN,
	  64, 0, 8, 0, 32, 0, 0, 0, 7, 0, 31, 32, 4, 0, 1, { Q }, 3, {
		{ "viewport", "the viewport rectangle bounds", 1, { R(0, 0, 4, 4) } },
		{ "clips", "the clips bound, not the viewport rectangle", 1, { R(0, 0, 8, 32) } },
		{ "none", "nothing bounds (the quad)", 1, { R(0, 0, 12, 40) } } } },
	{ "C0", "control clear: clips 16x48", CLEAR, LIN, 64, 0, 16, 0, 48, 0, 0, 0, 15, 0, 47,
	  48, 0, 0, 0, { Q }, 2, {
		{ "target", "the target: the clear path works", 1, { R(0, 0, 16, 48) } }, UNB } },
	{ "C1", "clear: surface clip 4x16, window clip 8x32", CLEAR, LIN, 64, 0, 4, 0, 16, 0, 0,
	  0, 7, 0, 31, 16, 0, 0, 0, { Q }, 3, {
		{ "surface", "the surface clip bounds", 1, { R(0, 0, 4, 16) } },
		{ "window", "the window clip bounds", 1, { R(0, 0, 8, 32) } }, UNB } },
	{ "C2", "clear: surface clip 8x32, window clip 4x16", CLEAR, LIN, 64, 0, 8, 0, 32, 0, 0,
	  0, 3, 0, 15, 32, 0, 0, 0, { Q }, 3, {
		{ "surface", "the surface clip bounds", 1, { R(0, 0, 8, 32) } },
		{ "window", "the window clip bounds", 1, { R(0, 0, 4, 16) } }, UNB } },
	{ "C3", "clear: clips 8x32, scissor 4x4", CLEAR, LIN, 64, 0, 8, 0, 32, 0, 0,
	  0, 7, 0, 31, 32, 0, 4, 0, { Q }, 3, {
		{ "scissor", "the scissor bounds", 1, { R(0, 0, 4, 4) } },
		{ "clips", "the clips bound, not the scissor", 1, { R(0, 0, 8, 32) } }, UNB } },
	{ "P2", "draw: surface clip 8x24, window offset (4,8), window clip 12x40, quad 12x40", DRAW,
	  LIN, 64, 0, 8, 0, 24, 4, 8, 0, 11, 0, 39, 24, 0, 0, 1, { Q }, 4, {
		{ "after-offset", "the surface clip in surface coordinates (after the offset)", 1,
		  { R(4, 8, 8, 24) } },
		{ "before-offset", "the surface clip in window coordinates (before the offset)", 1,
		  { R(4, 8, 12, 32) } },
		{ "window-only", "only the window clip bounds (the quad plus the offset)", 1,
		  { R(4, 8, 16, 48) } },
		{ "no-offset", "the window offset is not applied", 1, { R(0, 0, 8, 24) } } } },
	{ "P2w", "draw: surface clip 16x48, window offset (4,8), window clip 8x24, quad 12x40", DRAW,
	  LIN, 64, 0, 16, 0, 48, 4, 8, 0, 7, 0, 23, 48, 0, 0, 1, { Q }, 4, {
		{ "window-coords", "the window clip in window coordinates (before the offset)", 1,
		  { R(4, 8, 12, 32) } },
		{ "surface-coords", "the window clip in surface coordinates (after the offset)", 1,
		  { R(4, 8, 8, 24) } },
		{ "neither", "the window clip doesn't bound (the quad plus the offset)", 1,
		  { R(4, 8, 16, 48) } },
		{ "no-offset", "the window offset is not applied", 1, { R(0, 0, 8, 24) } } } },
	{ "C4", "clear: surface clip 8x24, window offset (4,8), window clip 12x40", CLEAR, LIN, 64,
	  0, 8, 0, 24, 4, 8, 0, 11, 0, 39, 24, 0, 0, 0, { Q }, 4, {
		{ "after-offset", "window clip in window coordinates, surface clip in surface "
		  "coordinates", 1, { R(4, 8, 8, 24) } },
		{ "before-offset", "the surface clip in window coordinates (before the offset)", 1,
		  { R(4, 8, 12, 32) } },
		{ "surface-only", "the surface clip alone (no offset, or the window clip in surface "
		  "coordinates)", 1, { R(0, 0, 8, 24) } }, UNB } },
	{ "C4w", "clear: surface clip 16x48, window offset (4,8), window clip 8x24", CLEAR, LIN, 64,
	  0, 16, 0, 48, 4, 8, 0, 7, 0, 23, 48, 0, 0, 0, { Q }, 4, {
		{ "window-coords", "the window clip in window coordinates (before the offset)", 1,
		  { R(4, 8, 12, 32) } },
		{ "surface-coords", "the window clip in surface coordinates (or no offset)", 1,
		  { R(0, 0, 8, 24) } },
		{ "surface-only", "the window clip doesn't bound (the surface clip)", 1,
		  { R(0, 0, 16, 48) } }, UNB } },
	{ "P3", "draw: pitch 64, COORD 0x1000|16 (inverted, height 16), quads rows 0-7 and 20-27",
	  DRAW, LIN, 64, 0, 16, 0, 32, 0, 0, 0, 15, 0, 31, 0x1010, 0, 0, 2,
	  { R(0, 0, 16, 8), R(0, 20, 16, 28) }, 6, {
		{ "no-flip", "the inverted origin doesn't move writes", 2,
		  { R(0, 0, 16, 8), R(0, 20, 16, 28) } },
		{ "flip-clipped", "flipped about 16, then clipped to the target", 1,
		  { R(0, 8, 16, 16) } },
		{ "flip-before-lo", "flipped about 16, rows -12..-5 before the target", 2,
		  { R(0, 8, 16, 16), R(0, -12, 16, -4) } },
		{ "flip-wrap12", "flipped, negative rows wrapped at 4096", 2,
		  { R(0, 8, 16, 16), R(0, 4084, 16, 4092) } },
		{ "flip-wrap16", "flipped, negative rows wrapped at 65536", 2,
		  { R(0, 8, 16, 16), R(0, 65524, 16, 65532) } },
		{ "flip-wrap17", "flipped, negative rows wrapped at 131072", 2,
		  { R(0, 8, 16, 16), R(0, 131060, 16, 131068) } } } },
	{ "P3b", "draw: pitch 64, COORD 0x1000|16, window offset (0,8), quad rows 0-3", DRAW, LIN, 64,
	  0, 16, 0, 32, 0, 8, 0, 15, 0, 15, 0x1010, 0, 0, 1, { R(0, 0, 16, 4) }, 5, {
		{ "no-flip", "no flip, the offset applied: rows 8-11", 1, { R(0, 8, 16, 12) } },
		{ "flip-then-offset", "flipped about 16, then the offset: rows 20-23", 1,
		  { R(0, 20, 16, 24) } },
		{ "offset-then-flip", "the offset, then flipped about 16: rows 4-7", 1,
		  { R(0, 4, 16, 8) } },
		{ "no-offset", "no flip, no offset: rows 0-3", 1, { R(0, 0, 16, 4) } },
		{ "flip-no-offset", "flipped, no offset: rows 12-15", 1, { R(0, 12, 16, 16) } } } },
	{ "P3z", "draw: pitch 64, COORD 0x1000 (height 0, Mesa's 4096-tall target), quad rows 0-7",
	  DRAW, LIN, 64, 0, 16, 0, 32, 0, 0, 0, 15, 0, 31, 0x1000, 0, 0, 1, { R(0, 0, 16, 8) }, 5, {
		{ "no-flip", "the inverted origin doesn't move writes", 1, { R(0, 0, 16, 8) } },
		{ "flip-before-lo", "flipped about 0: rows -8..-1 before the target", 1,
		  { R(0, -8, 16, 0) } },
		{ "flip-wrap12", "flipped, wrapped at 4096", 1, { R(0, 4088, 16, 4096) } },
		{ "flip-wrap16", "flipped, wrapped at 65536", 1, { R(0, 65528, 16, 65536) } },
		{ "flip-wrap17", "flipped, wrapped at 131072", 1, { R(0, 131064, 16, 131072) } } } },
	{ "C5", "clear: pitch 64, clips 16x32, COORD 0x1000|16", CLEAR, LIN, 64, 0, 16, 0, 32, 0, 0,
	  0, 15, 0, 31, 0x1010, 0, 0, 0, { Q }, 7, {
		{ "no-flip", "the inverted origin doesn't move the clear", 1, { R(0, 0, 16, 32) } },
		{ "flip-clipped", "flipped about 16, clipped to the target", 1, { R(0, 0, 16, 16) } },
		{ "flip-before-lo", "flipped about 16: rows -16..15", 1, { R(0, -16, 16, 16) } },
		{ "flip-wrap12", "flipped, wrapped at 4096", 2,
		  { R(0, 0, 16, 16), R(0, 4080, 16, 4096) } },
		{ "flip-wrap16", "flipped, wrapped at 65536", 2,
		  { R(0, 0, 16, 16), R(0, 65520, 16, 65536) } },
		{ "flip-wrap17", "flipped, wrapped at 131072", 2,
		  { R(0, 0, 16, 16), R(0, 131056, 16, 131072) } }, UNB } },
	{ "C5z", "clear: pitch 64, clips 16x32, COORD 0x1000 (height 0)", CLEAR, LIN, 64, 0, 16, 0, 32,
	  0, 0, 0, 15, 0, 31, 0x1000, 0, 0, 0, { Q }, 6, {
		{ "no-flip", "the inverted origin doesn't move the clear", 1, { R(0, 0, 16, 32) } },
		{ "flip-before-lo", "flipped about 0: rows -32..-1 before the target", 1,
		  { R(0, -32, 16, 0) } },
		{ "flip-wrap12", "flipped, wrapped at 4096", 1, { R(0, 4064, 16, 4096) } },
		{ "flip-wrap16", "flipped, wrapped at 65536", 1, { R(0, 65504, 16, 65536) } },
		{ "flip-wrap17", "flipped, wrapped at 131072", 1, { R(0, 131040, 16, 131072) } },
		UNB } },
	{ "P2s", "draw: swizzled R5G6B5 16x2 (Mesa 0060), window offset 8, window clip 8, quad 16x2",
	  DRAW, SWZ16X2, 4096, 0, 16, 0, 2, 8, 0, 0, 7, 0, 1, 2, 0, 0, 1, { R(0, 0, 16, 2) }, 4, {
		{ "window", "the window clip bounds: texels 8-15, inside the block", 1,
		  { R(8, 0, 16, 2) } },
		{ "past-block", "texels 8-23: columns 16-23 past the block, above it", 1,
		  { R(8, 0, 24, 2) } },
		{ "no-offset", "the window offset is not applied: texels 0-7", 1, { R(0, 0, 8, 2) } },
		{ "masked", "columns 16-23 wrap to 0-7 inside the block", 2,
		  { R(8, 0, 16, 2), R(0, 0, 8, 2) } } } },
	{ "S4096", "draw: swizzled A8R8G8B8 4096x1, window offset 4095, window clip 1, quad 16x1",
	  DRAW, SWZ4096X1, 4096, 0, 4096, 0, 1, 4095, 0, 0, 0, 0, 0, 1, 0, 0, 1, { R(0, 0, 16, 1) }, 5, {
		{ "window", "the window clip bounds: texel 4095 only", 1, { R(4095, 0, 4096, 1) } },
		{ "past-block", "texels 4095-4110: columns past the block, above it", 1,
		  { R(4095, 0, 4111, 1) } },
		{ "masked", "columns 4096-4110 wrap to 0-14", 2,
		  { R(4095, 0, 4096, 1), R(0, 0, 15, 1) } },
		{ "no-offset", "the offset is not applied, the window clip bounds: texel 0", 1,
		  { R(0, 0, 1, 1) } },
		{ "no-offset-quad", "the offset is not applied, no clip: texels 0-15", 1,
		  { R(0, 0, 16, 1) } } } },
};
#define NCASES ((int)(sizeof(cases) / sizeof(cases[0])))

/* --- formats and addresses ------------------------------------------------------------------- */

static int fmt_bpp(int fmt) { return fmt == SWZ16X2 ? 2 : 4; }
static int fmt_lw(int fmt) { return fmt == SWZ16X2 ? 4 : fmt == SWZ4096X1 ? 12 : 0; }
static int fmt_lh(int fmt) { return fmt == SWZ16X2 ? 1 : 0; }

/* RT_FORMAT: colour, zeta, type, log2 size (nv30-40_3d.xml.h) */
static uint32_t rt_format(int fmt)
{
	switch (fmt) {
	case SWZ16X2:	return 0x03 | 0x20 | 0x200 | 4u << 16 | 1u << 24;	/* R5G6B5, Z16 */
	case SWZ4096X1:	return 0x08 | 0x40 | 0x200 | 12u << 16;		/* A8R8G8B8, Z24S8 */
	default:	return 0x08 | 0x40 | 0x100;			/* linear */
	}
}

/* x0 y0 x1 y1 ..., the larger dimension's leftover bits on top (NV swizzle) */
static uint64_t morton(uint32_t x, uint32_t y, int lw, int lh)
{
	uint64_t idx = 0;
	int bit = 0;

	for (int i = 0; i < 13; i++) {
		if (i < lw)
			idx |= (uint64_t)((x >> i) & 1) << bit++;
		if (i < lh)
			idx |= (uint64_t)((y >> i) & 1) << bit++;
	}
	return idx;
}

/* Byte offset of pixel (x, y) from lo: linear rows, or the swizzled block with columns past it
 * in the blocks after it (rt-followups L2's model) */
static int64_t pix_byte(const struct pcase *c, int x, int y)
{
	int lw = fmt_lw(c->fmt), lh = fmt_lh(c->fmt), bpp = fmt_bpp(c->fmt);

	if (c->fmt == LIN)
		return (int64_t)y * c->pitch + (int64_t)x * bpp;
	return ((int64_t)(x >> lw) * ((int64_t)1 << (lw + lh)) +
		(int64_t)morton(x & ((1u << lw) - 1), y, lw, lh)) * bpp;
}

/* What fb_extent()/rt_ok() in ps3rsx's validate.c compute: bytes from lo, or -1 if refused */
static int64_t validator_extent(const struct pcase *c, int bpp)
{
	int64_t cols = c->cx + c->cw, rows = c->cy + c->ch;

	if (c->wox + c->wx1 + 1 > cols)
		cols = c->wox + c->wx1 + 1;
	if (c->woy + c->wy1 + 1 > rows)
		rows = c->woy + c->wy1 + 1;
	if ((int64_t)(c->coord & 0xfff) > rows)
		rows = c->coord & 0xfff;
	if (c->fmt != LIN) {
		if (cols > (1 << fmt_lw(c->fmt)) || rows > (1 << fmt_lh(c->fmt)))
			return -1;
		return ((int64_t)1 << (fmt_lw(c->fmt) + fmt_lh(c->fmt))) * bpp;
	}
	if (c->pitch % 64 || c->pitch > 0x10000)
		return -1;
	return (int64_t)c->pitch * (rows - 1) + cols * bpp;
}

/* The worst-case reach [lo + *a, lo + *b) of any write the GPU can make: every 16-bit
 * column, every row from -65536 to a negative row wrapped at 17 bits (the header comment) */
static void reach(const struct pcase *c, int64_t *a, int64_t *b)
{
	const int64_t cmax = 65536;

	if (c->fmt != LIN) {
		int64_t w = (int64_t)1 << fmt_lw(c->fmt), blk = w << fmt_lh(c->fmt);

		*a = 0;
		*b = (cmax + c->wox + w - 1) / w * blk * fmt_bpp(c->fmt);
	} else {
		*a = -65536 * (int64_t)c->pitch;
		*b = 131071 * (int64_t)c->pitch + cmax * fmt_bpp(c->fmt);
	}
}

/* Every bound inside the BO, the quads in the viewport, swizzled rows in the block */
static const char *selfcheck(const struct pcase *c)
{
	int64_t a, b, e;

	reach(c, &a, &b);
	if (a < -(int64_t)LO || b > (int64_t)(BO_SIZE - LO))
		return "worst-case reach outside the BO";
	if (c->fmt == LIN && c->pitch != 64)
		return "a linear pitch other than 64 (16 pixels a row: the reach above assumes it)";
	e = validator_extent(c, 4);
	if (e < 0 || e > (int64_t)(BO_SIZE - LO))
		return "the validator would refuse it";
	for (int i = 0; i < c->nq; i++) {
		const struct rect *q = &c->q[i];

		if (q->x0 < 0 || q->y0 < 0 || q->x1 > 64 || q->y1 > 64 || q->x0 >= q->x1 ||
		    q->y0 >= q->y1)
			return "quad outside the 64x64 viewport";
		if (c->fmt != LIN && (q->y1 + c->woy > (1 << fmt_lh(c->fmt)) || c->coord & 0xf000))
			return "swizzled rows past the block";
	}
	for (int k = 0; k < c->nh; k++)
		for (int i = 0; i < c->h[k].n; i++) {
			const struct rect *r = &c->h[k].r[i];
			int64_t lo = pix_byte(c, r->x0, r->y0), hi = pix_byte(c, r->x1 - 1, r->y1 - 1);

			if (lo < a || hi + 4 > b)
				return "a prediction outside the reach";
		}
	return NULL;
}

/* --- the push -------------------------------------------------------------------------------- */

#define MTH(n, m)	(((uint32_t)(n) << 18) | (m))	/* subchannel 0 */
#define OUT(m, v)	do { *p++ = MTH(1, (m)); *p++ = (v); } while (0)

static uint32_t f2u(float f)
{
	uint32_t u;

	memcpy(&u, &f, 4);
	return u;
}

/* docs/RSX-PROGRAMS.md A.2 FP_COL (MOV R0, f[COL0]), memory form; A.1.3's vertex program */
static const uint32_t fp_col[8] = {
	0x3e010100, 0xc8011c9d, 0xc8000001, 0xc8000001,
	0x00010001, 0x00000000, 0x00000000, 0x00000000,
};
static const uint32_t vp_col[8] = {
	0x401f9c6c, 0x0040000d, 0x8106c083, 0x6041ff80,	/* MOV o[HPOS], v[0] */
	0x401f9c6c, 0x0040030d, 0x8106c083, 0x6041ff85,	/* MOV o[COL0], v[3]  END */
};
/* the quads' colours (R, G, B), components 0 or 1 */
static const float quad_rgb[2][3] = { { 1, 0, 1 }, { 0, 1, 1 } };

static uint32_t *vertex(uint32_t *p, int x, int y, const float *rgb)
{
	*p++ = MTH(4, 0x1c30);			/* attribute 3, colour: first */
	*p++ = f2u(rgb[0]); *p++ = f2u(rgb[1]); *p++ = f2u(rgb[2]); *p++ = f2u(1);
	*p++ = MTH(4, 0x1c00);			/* attribute 0, position: last, emits the vertex */
	*p++ = f2u(x / 32.0f - 1); *p++ = f2u(1 - y / 32.0f); *p++ = 0; *p++ = f2u(1);
	return p;
}

/* The case's whole push (one submit): words written to buf */
static size_t build(const struct pcase *c, uint32_t c_lo, uint32_t z_lo, uint32_t fp, uint32_t *buf)
{
	uint32_t *p = buf, vp = c->vp ? c->vp : 4096, sc = c->sc ? c->sc : 4096;

	OUT(0, 0x31337000);			/* bind 3D */
	OUT(0x184, 0xfeed0000);			/* DMA_TEXTURE A: the fragment program (location 1) */
	OUT(0x188, 0xfeed0001);
	OUT(0x194, 0xfeed0001);			/* colour 0 and zeta: GART */
	OUT(0x198, 0xfeed0001);
	OUT(0x1da4, 0);
	OUT(0x200, (uint32_t)c->cw << 16 | c->cx);
	OUT(0x204, (uint32_t)c->ch << 16 | c->cy);
	OUT(0x208, rt_format(c->fmt));
	OUT(0x20c, c->pitch);
	OUT(0x210, c_lo);
	OUT(0x214, z_lo);
	OUT(0x22c, c->pitch);
	OUT(0x220, 1);
	OUT(0x2b8, (uint32_t)c->woy << 16 | c->wox);
	OUT(0x2bc, 0);
	OUT(0x2c0, (uint32_t)c->wx1 << 16 | c->wx0);
	OUT(0x2c4, (uint32_t)c->wy1 << 16 | c->wy0);
	OUT(0x1d88, c->coord);
	OUT(0x8c0, sc << 16);
	OUT(0x8c4, sc << 16);
	OUT(0xa00, vp << 16);
	OUT(0xa04, vp << 16);
	/* state a client before us may have left: everything that could drop or add fragments */
	OUT(0x324, 0x01010101);			/* colour mask */
	OUT(0x1e98, 0x01000000);		/* render enable: on, no condition */
	OUT(0x1d7c, 0xffff0000);		/* multisample off, every sample */
	OUT(0x304, 0); OUT(0x310, 0);		/* alpha test, blend */
	OUT(0x328, 0); OUT(0x348, 0);		/* stencil, both faces */
	OUT(0x374, 0);				/* logic op */
	OUT(0xa68, 0); OUT(0xa70, 0); OUT(0xa74, 0);	/* polygon offset, depth write, depth test */
	OUT(0x147c, 0); OUT(0x1478, 0);		/* polygon stipple, user clip planes */
	OUT(0x1838, 0); OUT(0x183c, 0);		/* polygon smooth, cull */
	OUT(0x368, 0x1d01);			/* smooth shading */
	*p++ = MTH(2, 0x1828); *p++ = 0x1b02; *p++ = 0x1b02;	/* polygon mode fill */
	if (c->kind == CLEAR) {
		OUT(0x1d90, CLEAR_COLOUR);
		OUT(0x1d94, 0xf0);		/* colour only */
		return p - buf;
	}
	*p++ = MTH(16, 0x1740);			/* vertex arrays off: immediate attributes */
	for (int i = 0; i < 16; i++)
		*p++ = 2;
	OUT(0x1e94, 0x11);			/* ENGINE: VP + FP, as Mesa */
	*p++ = MTH(2, 0x394); *p++ = f2u(0); *p++ = f2u(1);	/* depth range */
	*p++ = MTH(8, 0xa20);			/* viewport transform: NDC to [0, 64) */
	*p++ = f2u(32); *p++ = f2u(32); *p++ = f2u(0.5f); *p++ = 0;
	*p++ = f2u(32); *p++ = f2u(-32); *p++ = f2u(0.5f); *p++ = 0;
	*p++ = MTH(2, 0x1e9c); *p++ = 0; *p++ = 0;	/* VP upload and start at 0 */
	*p++ = MTH(8, 0xb80);
	memcpy(p, vp_col, sizeof(vp_col));
	p += 8;
	OUT(0x1ff0, 0x00000009);		/* VP inputs: attributes 0 and 3 */
	OUT(0x1ff4, 0x00000025);
	OUT(0x1ef8, 0x0020ffff);
	OUT(0x8e4, fp | 1);			/* FP in VRAM */
	OUT(0x1d60, 0x02000440);
	OUT(0x1808, 5);				/* triangles */
	for (int i = 0; i < c->nq; i++) {
		const struct rect *q = &c->q[i];
		const float *rgb = quad_rgb[i];

		p = vertex(p, q->x0, q->y0, rgb);
		p = vertex(p, q->x1, q->y0, rgb);
		p = vertex(p, q->x1, q->y1, rgb);
		p = vertex(p, q->x0, q->y0, rgb);
		p = vertex(p, q->x1, q->y1, rgb);
		p = vertex(p, q->x0, q->y1, rgb);
	}
	OUT(0x1808, 0);
	return p - buf;
}

/* --- bitmaps of BO dwords -------------------------------------------------------------------- */

static void bm_range(uint64_t *bm, uint64_t first, uint64_t n)
{
	while (n && (first & 63)) {
		bm[first / 64] |= 1ull << (first & 63);
		first++, n--;
	}
	while (n >= 64) {
		bm[first / 64] = ~0ull;
		first += 64, n -= 64;
	}
	while (n--) {
		bm[first / 64] |= 1ull << (first & 63);
		first++;
	}
}

/* The dwords a prediction writes, as dword indexes of the BO (selfcheck() keeps them inside) */
static void hyp_bitmap(const struct pcase *c, const struct hyp *h, uint64_t *bm)
{
	int bpp = fmt_bpp(c->fmt);

	memset(bm, 0, NBW * 8);
	for (int i = 0; i < h->n; i++) {
		const struct rect *r = &h->r[i];

		for (int y = r->y0; y < r->y1; y++) {
			if (c->fmt == LIN) {
				uint64_t b0 = LO + pix_byte(c, r->x0, y);

				bm_range(bm, b0 / 4, (uint64_t)(r->x1 - r->x0) * bpp / 4);
				continue;
			}
			for (int x = r->x0; x < r->x1; x++)
				bm_range(bm, (LO + pix_byte(c, x, y)) / 4, 1);
		}
	}
}

/* bits set in v: no __builtin_popcountll, which calls libgcc's __popcountdi2, built for POWER7 and later in
 * the cross toolchain (popcntd: an illegal instruction on the Cell) */
static unsigned popcount64(uint64_t v)
{
	unsigned n = 0;

	for (; v; v &= v - 1)
		n++;
	return n;
}

static __attribute__((unused)) uint64_t bm_count(const uint64_t *bm)
{
	uint64_t n = 0;

	for (unsigned i = 0; i < NBW; i++)
		n += popcount64(bm[i]);
	return n;
}

/* The driver behind the node must be ps3rsx: it registers as "nouveau" (Mesa's loader needs
 * that name) with its own description, which real nouveau (the 7600 GT box) doesn't have */
#define PS3RSX_NAME	"nouveau"
#define PS3RSX_DESC	"PS3 RSX (nouveau ABI on lv1)"	/* ps3rsx's drv.c */

static bool identity_ok(const char *name, size_t name_len, const char *desc, size_t desc_len)
{
	return name_len == strlen(PS3RSX_NAME) && !memcmp(name, PS3RSX_NAME, name_len) &&
	       desc_len == strlen(PS3RSX_DESC) && !memcmp(desc, PS3RSX_DESC, desc_len);
}

static int case_index(const char *name)
{
	for (int i = 0; i < NCASES; i++)
		if (!strcmp(cases[i].name, name))
			return i;
	return -1;
}

/* --- readback ------------------------------------------------------------------------------- */

static uint64_t *bm_obs, *bm_hyp, *bm_uni;

static int analysis_init(void)
{
	bm_obs = malloc(NBW * 8);
	bm_hyp = malloc(NBW * 8);
	bm_uni = malloc(NBW * 8);
	return !bm_obs || !bm_hyp || !bm_uni;
}

static void fill(uint32_t *m)
{
	for (uint32_t i = 0; i < NDW; i++)
		m[i] = PAT(i);
}

/* Changed dwords into bm; their count; the first few distinct values */
static uint64_t scan(const uint32_t *m, uint64_t *bm, uint32_t *vals, uint64_t *vn, int *nv)
{
	uint64_t n = 0;

	memset(bm, 0, NBW * 8);
	*nv = 0;
	for (uint32_t i = 0; i < NDW; i++) {
		uint32_t v = m[i];
		int k;

		if (v == PAT(i))
			continue;
		bm[i / 64] |= 1ull << (i & 63);
		n++;
		for (k = 0; k < *nv && vals[k] != v; k++)
			;
		if (k < *nv)
			vn[k]++;
		else if (*nv < 4)
			vals[*nv] = v, vn[(*nv)++] = 1;
	}
	return n;
}

static int64_t floordiv(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* The changed dwords as rows and column spans (linear) or byte runs from lo (swizzled, zeta) */
static void print_writes(const struct pcase *c, const uint64_t *bm, bool rows)
{
	int64_t row = INT64_MIN, cmin = 0, cmax = 0, cnt = 0, run_a = 0, run_b = 0;
	int64_t g_r0 = 0, g_r1 = 0, g_c0 = -1, g_c1 = -1, g_n = -1;
	int lines = 0;
	const int maxl = 16;
	bool in_run = false;

#define FLUSH_GROUP() do { if (g_c0 >= 0 && lines++ < maxl) { \
	if (g_r0 == g_r1) printf("    row %lld:", (long long)g_r0); \
	else printf("    rows %lld..%lld:", (long long)g_r0, (long long)g_r1); \
	printf(" cols %lld-%lld", (long long)g_c0, (long long)g_c1); \
	if (g_n != g_c1 - g_c0 + 1) printf(" (%lld written)", (long long)g_n); \
	printf("\n"); } } while (0)
#define FLUSH_ROW() do { if (row != INT64_MIN) { \
	if (g_c0 == cmin && g_c1 == cmax && g_n == cnt && g_r1 == row - 1) g_r1 = row; \
	else { FLUSH_GROUP(); g_r0 = g_r1 = row; g_c0 = cmin; g_c1 = cmax; g_n = cnt; } } } while (0)
#define FLUSH_RUN() do { if (in_run && lines++ < maxl) \
	printf("    bytes lo%+lld..lo%+lld\n", (long long)run_a, (long long)run_b - 1); } while (0)

	for (uint32_t w = 0; w < NBW; w++) {
		for (uint64_t bits = bm[w]; bits; bits &= bits - 1) {
			uint32_t i = w * 64 + __builtin_ctzll(bits);
			int64_t d = (int64_t)i * 4 - LO;

			if (!rows) {
				if (in_run && d == run_b) {
					run_b += 4;
				} else {
					FLUSH_RUN();
					run_a = d, run_b = d + 4, in_run = true;
				}
				continue;
			}
			int64_t r = floordiv(d, c->pitch), col = (d - r * c->pitch) / 4;

			if (r != row) {
				FLUSH_ROW();
				row = r, cmin = cmax = col, cnt = 0;
			}
			cmax = col;
			cnt++;
		}
	}
	if (rows) {
		FLUSH_ROW();
		FLUSH_GROUP();
	} else {
		FLUSH_RUN();
	}
	if (lines > maxl)
		printf("    ... %d more lines\n", lines - maxl);
#undef FLUSH_GROUP
#undef FLUSH_ROW
#undef FLUSH_RUN
}

/*
 * After the fence: what changed in both BOs, printed, against each prediction. Returns the
 * result tag (a prediction's, "nothing", "other", "OUTSIDE" or "ZETA-WRITTEN"); *stop when the
 * run must stop (zeta written, or a write outside every prediction without keep_going).
 */
static const char *analyse(const struct pcase *c, const uint32_t *cm, const uint32_t *zm,
			   bool keep_going, bool *stop)
{
	uint32_t vals[4];
	uint64_t vn[4], n, nz;
	int nv;
	const char *res = NULL;

	*stop = false;
	nz = scan(zm, bm_obs, vals, vn, &nv);
	if (nz) {
		printf("  ZETA BO: %llu dwords changed (depth and stencil are off):\n",
		       (unsigned long long)nz);
		print_writes(c, bm_obs, false);
		*stop = true;
	}
	n = scan(cm, bm_obs, vals, vn, &nv);
	printf("  colour BO: %llu dwords changed", (unsigned long long)n);
	for (int k = 0; k < nv; k++)
		printf("%s%08x x%llu", k ? ", " : ", values ", vals[k], (unsigned long long)vn[k]);
	printf("\n");
	print_writes(c, bm_obs, c->fmt == LIN);

	memset(bm_uni, 0, NBW * 8);
	for (int k = 0; k < c->nh; k++) {
		bool eq;

		hyp_bitmap(c, &c->h[k], bm_hyp);
		eq = !memcmp(bm_hyp, bm_obs, NBW * 8);
		for (unsigned i = 0; i < NBW; i++)
			bm_uni[i] |= bm_hyp[i];
		printf("  %-16s %-3s %s\n", c->h[k].tag, eq ? "YES" : "no", c->h[k].what);
		if (eq && !res)
			res = c->h[k].tag;
	}
	if (!res) {
		uint64_t out = 0;

		for (unsigned i = 0; i < NBW; i++)
			out += popcount64(bm_obs[i] & ~bm_uni[i]);
		if (!n) {
			res = "nothing";
		} else if (out) {
			res = "OUTSIDE";
			printf("  %llu changed dwords lie outside every prediction\n",
			       (unsigned long long)out);
			if (!keep_going)
				*stop = true;
		} else {
			res = "other";
		}
	}
	if (nz)
		res = "ZETA-WRITTEN";
	return res;
}

#ifndef CLIP_PROBE_LIB
/* --- the device ------------------------------------------------------------------------------ */

static int fd = -1;
static struct drm_nouveau_gem_info cbo, zbo, fpbo, pushbo;
static uint32_t *cmap, *zmap, *pushmap;

static void *map_bo(struct drm_nouveau_gem_info *bo)
{
	void *m = mmap(NULL, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, bo->map_handle);

	return m == MAP_FAILED ? NULL : m;
}

static int alloc(struct drm_nouveau_gem_info *bo, uint32_t domain, uint64_t size, const char *what)
{
	int rc = gem_new(fd, domain | NOUVEAU_GEM_DOMAIN_MAPPABLE, size, 0, bo);

	if (rc || !bo->map_handle) {
		printf("setup: %s (%llu KiB): GEM_NEW %s\n", what, (unsigned long long)size >> 10,
		       rc ? strerror(-rc) : "gave no mapping");
		return 1;
	}
	return 0;
}

static int setup(void)
{
	struct drm_nouveau_getparam gp = { .param = 0x1000 };	/* PS3RSX_GETPARAM_PS3_ABI */
	char name[64] = "", date[64] = "", desc[128] = "";
	struct drm_version v = { .name_len = sizeof(name) - 1, .name = name,
				 .date_len = sizeof(date) - 1, .date = date,
				 .desc_len = sizeof(desc) - 1, .desc = desc };
	volatile uint32_t *f;

	fd = open("/dev/dri/renderD128", O_RDWR);
	if (fd < 0) {
		perror("setup: /dev/dri/renderD128");
		return 1;
	}
	/* DRM_IOCTL_VERSION is core DRM (valid on any driver): no driver ioctl before it says ps3rsx */
	if (ioctl(fd, DRM_IOCTL_VERSION, &v)) {
		perror("setup: DRM_IOCTL_VERSION");
		return 1;
	}
	if (!identity_ok(name, v.name_len, desc, v.desc_len)) {
		printf("setup: /dev/dri/renderD128 is \"%.*s\" (\"%.*s\"), not ps3rsx: nothing done\n",
		       (int)(v.name_len < sizeof(name) ? v.name_len : sizeof(name) - 1), name,
		       (int)(v.desc_len < sizeof(desc) ? v.desc_len : sizeof(desc) - 1), desc);
		return 1;
	}
	if (ioctl(fd, DRM_IOCTL_NOUVEAU_GETPARAM, &gp)) {
		perror("setup: ps3rsx's PS3_ABI getparam");
		return 1;
	}
	printf("setup: %s %d.%d.%d (%s), PS3 ABI %llu\n", desc, v.version_major, v.version_minor,
	       v.version_patchlevel, date, (unsigned long long)gp.value);
	if (alloc(&cbo, NOUVEAU_GEM_DOMAIN_GART, BO_SIZE, "colour BO") ||
	    alloc(&zbo, NOUVEAU_GEM_DOMAIN_GART, BO_SIZE, "zeta BO") ||
	    alloc(&fpbo, NOUVEAU_GEM_DOMAIN_VRAM, 4096, "fragment program BO") ||
	    alloc(&pushbo, NOUVEAU_GEM_DOMAIN_GART, 65536, "push BO"))
		return 1;
	cmap = map_bo(&cbo);
	zmap = map_bo(&zbo);
	pushmap = map_bo(&pushbo);
	f = map_bo(&fpbo);
	if (!cmap || !zmap || !pushmap || !f || analysis_init()) {
		perror("setup: mmap or malloc");
		return 1;
	}
	for (int i = 0; i < 8; i++)
		f[i] = fp_col[i];
	__sync_synchronize();
	printf("setup: colour BO GART %#llx, zeta BO GART %#llx (%u MiB each, targets at +%u MiB), "
	       "FP VRAM %#llx\n", (unsigned long long)cbo.offset, (unsigned long long)zbo.offset,
	       BO_SIZE / MIB, LO / MIB, (unsigned long long)fpbo.offset);
	return 0;
}

/* Submit nwords of the push BO on a channel; no SYNC: the caller polls the fence */
static int submit(int channel, size_t nwords)
{
	struct drm_nouveau_gem_pushbuf_bo bos[4] = {
		{ .handle = pushbo.handle, .read_domains = NOUVEAU_GEM_DOMAIN_GART,
		  .valid_domains = NOUVEAU_GEM_DOMAIN_GART },
		{ .handle = cbo.handle, .read_domains = NOUVEAU_GEM_DOMAIN_GART,
		  .write_domains = NOUVEAU_GEM_DOMAIN_GART, .valid_domains = NOUVEAU_GEM_DOMAIN_GART },
		{ .handle = zbo.handle, .read_domains = NOUVEAU_GEM_DOMAIN_GART,
		  .write_domains = NOUVEAU_GEM_DOMAIN_GART, .valid_domains = NOUVEAU_GEM_DOMAIN_GART },
		{ .handle = fpbo.handle, .read_domains = NOUVEAU_GEM_DOMAIN_VRAM,
		  .valid_domains = NOUVEAU_GEM_DOMAIN_VRAM },
	};
	struct drm_nouveau_gem_pushbuf_push push = { .bo_index = 0, .length = nwords * 4 };
	struct drm_nouveau_gem_pushbuf req = {
		.channel = channel, .nr_buffers = 4, .buffers = (uintptr_t)bos,
		.nr_push = 1, .push = (uintptr_t)&push,
	};

	return ioctl(fd, DRM_IOCTL_NOUVEAU_GEM_PUSHBUF, &req) ? -errno : 0;
}

static long now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000L + t.tv_nsec / 1000000;
}

/* Poll the BO's write fence (CPU_PREP NOWAIT) until it signals: 0, -ETIMEDOUT or its error */
static int wait_bo(uint32_t handle, int timeout_ms, long *ms)
{
	long t0 = now_ms();

	for (;;) {
		struct drm_nouveau_gem_cpu_prep r = { .handle = handle, .flags = 1 /* NOWAIT */ };
		int rc = ioctl(fd, DRM_IOCTL_NOUVEAU_GEM_CPU_PREP, &r) ? -errno : 0;

		*ms = now_ms() - t0;
		if (rc != -EBUSY)
			return rc;
		if (*ms > timeout_ms)
			return -ETIMEDOUT;
		usleep(1000);
	}
}

static const char *results[NCASES];

/* What a stopped GPU needs (exec.c: a silent stall is wedged for good after 2 s; only a graph
 * exception is recovered in place, unwedge.c) */
static const char after_stop[] =
	"  check journalctl -k: \"recovered\" means a graph exception, recovered in place;\n"
	"  \"GPU stopped ... wedged until reboot\" means a stall: reboot before starting the desktop\n"
	"  (ps3rsx keeps the GPU from ps3fb, and X would come up display-only)\n";

/* One case: 0 ran (whatever it showed), 1 refused (skipped), 2 stop the run */
static int run_case(int ci, int timeout_ms, bool keep_going)
{
	const struct pcase *c = &cases[ci];
	struct drm_nouveau_channel_alloc ch = { .fb_ctxdma_handle = 0xfeed0000,
						.tt_ctxdma_handle = 0xfeed0001 };
	struct drm_nouveau_channel_free cf;
	int rc;
	bool stop;
	long ms;
	time_t t = time(NULL);
	char ts[16];
	const char *res = NULL;

	strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&t));
	printf("\ncase %s (%s) at %s: %s\n", c->name, c->kind == DRAW ? "draw" : "clear", ts, c->what);
	fill(cmap);
	fill(zmap);
	__sync_synchronize();
	if (ioctl(fd, DRM_IOCTL_NOUVEAU_CHANNEL_ALLOC, &ch)) {
		printf("  CHANNEL_ALLOC: %s: stopping\n", strerror(errno));
		results[ci] = "error";
		return 2;
	}
	size_t nw = build(c, cbo.offset + LO, zbo.offset + LO, fpbo.offset, pushmap);

	__sync_synchronize();
	rc = submit(ch.channel, nw);
	if (rc == -EINVAL) {
		printf("  submit REFUSED by the validator (journalctl -k has the reason): case skipped\n");
		results[ci] = "refused";
		cf.channel = ch.channel;
		ioctl(fd, DRM_IOCTL_NOUVEAU_CHANNEL_FREE, &cf);
		return 1;
	}
	if (rc) {
		printf("  submit failed: %s: stopping, nothing more is submitted\n", strerror(-rc));
		results[ci] = "error";
		return 2;
	}
	rc = wait_bo(cbo.handle, timeout_ms, &ms);
	if (rc == -ETIMEDOUT) {
		printf("  TIMEOUT: the fence didn't signal in %d ms: stopping, nothing more is "
		       "submitted\n%s", timeout_ms, after_stop);
		results[ci] = "timeout";
		return 2;
	}
	if (rc) {
		printf("  the fence ended with %s (the GPU stopped or raised an exception): stopping, "
		       "nothing more is submitted\n%s", strerror(-rc), after_stop);
		results[ci] = "fence-error";
		return 2;
	}
	cf.channel = ch.channel;
	ioctl(fd, DRM_IOCTL_NOUVEAU_CHANNEL_FREE, &cf);
	printf("  submitted %zu words, done in %ld ms\n", nw, ms);

	res = analyse(c, cmap, zmap, keep_going, &stop);
	results[ci] = res;
	printf("RESULT %s %s\n", c->name, res);
	if (stop)
		printf("  stopping: a write the case doesn't predict (or zeta); nothing more is "
		       "submitted\n");
	return stop ? 2 : 0;
}

/* --- the summary ----------------------------------------------------------------------------- */

struct ans { const char *tag, *answer; };
struct question { const char *id, *text, *cname; struct ans a[8]; };

static const struct question qs[] = {
	{ "Q1", "draws: the surface clip bounds the writes", "P1a",
	  { { "surface", "yes" }, { "window", "no (the window clip does)" },
	    { "none", "no (neither clip)" } } },
	{ "Q2", "draws: the window clip bounds the writes", "P1b",
	  { { "window", "yes" }, { "surface", "no (the surface clip does)" },
	    { "none", "no (neither clip)" } } },
	{ "Q3", "clears: the surface clip bounds the writes", "C1",
	  { { "surface", "yes" }, { "window", "no (the window clip does)" },
	    { "unbounded", "no (neither clip)" } } },
	{ "Q4", "clears: the window clip bounds the writes", "C2",
	  { { "window", "yes" }, { "surface", "no (the surface clip does)" },
	    { "unbounded", "no (neither clip)" } } },
	{ "Q5", "draws: the surface clip applies after the window offset", "P2",
	  { { "after-offset", "yes (surface coordinates)" },
	    { "before-offset", "no: window coordinates (the offset reaches past the clip)" },
	    { "window-only", "no: the surface clip doesn't bound with an offset" },
	    { "no-offset", "the window offset isn't applied to draws" } } },
	{ "Q6", "draws: the window clip is in window coordinates", "P2w",
	  { { "window-coords", "yes (before the offset)" },
	    { "surface-coords", "no: surface coordinates (after the offset)" },
	    { "neither", "no: it doesn't bound with an offset" },
	    { "no-offset", "the window offset isn't applied to draws" } } },
	{ "Q7", "clears: the surface clip applies after the window offset", "C4",
	  { { "after-offset", "yes (and the window clip in window coordinates)" },
	    { "before-offset", "no: window coordinates" },
	    { "surface-only", "the surface clip alone, the offset not seen (or the window clip "
	      "in surface coordinates)" },
	    { "unbounded", "no clip bounds" } } },
	{ "Q8", "clears: the window clip is in window coordinates", "C4w",
	  { { "window-coords", "yes" }, { "surface-coords", "no: surface coordinates (or no offset)" },
	    { "surface-only", "no: it doesn't bound with an offset" },
	    { "unbounded", "no clip bounds" } } },
	{ "Q9", "draws: the viewport rectangle (0xa00/0xa04) bounds the writes", "P1v",
	  { { "viewport", "yes" }, { "clips", "no" }, { "none", "no, and neither do the clips" } } },
	{ "Q10", "draws: the scissor bounds the writes", "P1c",
	  { { "scissor", "yes" }, { "clips", "no" }, { "none", "no, and neither do the clips" } } },
	{ "Q11", "clears: the scissor bounds the writes", "C3",
	  { { "scissor", "yes" }, { "clips", "no" }, { "unbounded", "no clip bounds" } } },
	{ "Q12", "draws: the inverted origin (COORD bit 12) moves writes (height 16)", "P3",
	  { { "no-flip", "no" }, { "flip-clipped", "yes, flipped and clipped to the target" },
	    { "flip-before-lo", "YES: rows before the target" },
	    { "flip-wrap12", "YES: wrapped rows (12 bits)" },
	    { "flip-wrap16", "YES: wrapped rows (16 bits)" },
	    { "flip-wrap17", "YES: wrapped rows (17 bits)" } } },
	{ "Q12z", "draws: the inverted origin with height 0 (a 4096-tall target)", "P3z",
	  { { "no-flip", "no" }, { "flip-before-lo", "YES: rows before the target" },
	    { "flip-wrap12", "YES: wrapped rows (12 bits)" },
	    { "flip-wrap16", "YES: wrapped rows (16 bits)" },
	    { "flip-wrap17", "YES: wrapped rows (17 bits)" },
	    { "nothing", "yes, flipped about 0 and clipped away (nothing drawn)" } } },
	{ "Q13", "draws: the flip and the window offset", "P3b",
	  { { "no-flip", "no flip, the offset applied" },
	    { "flip-then-offset", "flipped, then offset (the wo_y + height term)" },
	    { "offset-then-flip", "offset, then flipped" },
	    { "no-offset", "no flip, the y offset not applied" },
	    { "flip-no-offset", "flipped, the y offset not applied" } } },
	{ "Q14", "clears: the inverted origin moves writes (height 16)", "C5",
	  { { "no-flip", "no" }, { "flip-clipped", "yes, flipped and clipped to the target" },
	    { "flip-before-lo", "YES: rows before the target" },
	    { "flip-wrap12", "YES: wrapped rows (12 bits)" },
	    { "flip-wrap16", "YES: wrapped rows (16 bits)" },
	    { "flip-wrap17", "YES: wrapped rows (17 bits)" }, { "unbounded", "no clip bounds" } } },
	{ "Q14z", "clears: the inverted origin with height 0", "C5z",
	  { { "no-flip", "no" }, { "flip-before-lo", "YES: rows before the target" },
	    { "flip-wrap12", "YES: wrapped rows (12 bits)" },
	    { "flip-wrap16", "YES: wrapped rows (16 bits)" },
	    { "flip-wrap17", "YES: wrapped rows (17 bits)" }, { "unbounded", "no clip bounds" },
	    { "nothing", "yes, flipped about 0 and clipped away (nothing cleared)" } } },
	{ "Q15", "swizzled 16x2 block (Mesa 0060): writes stay inside the block", "P2s",
	  { { "window", "yes (the window clip bounds)" },
	    { "past-block", "NO: columns past the block land above it" },
	    { "masked", "yes, but columns past it wrap inside the block" },
	    { "no-offset", "the window offset isn't applied" } } },
	{ "Q16", "swizzled 4096x1, window offset 4095: writes stay inside the block", "S4096",
	  { { "window", "yes (the window clip bounds)" },
	    { "past-block", "NO: columns past the block land above it" },
	    { "masked", "yes, but columns past it wrap to its start" },
	    { "no-offset", "the window offset isn't applied" },
	    { "no-offset-quad", "the offset isn't applied and no clip bounds" } } },
};

static void summary(void)
{
	int p0 = case_index("P0"), c0 = case_index("C0");

	printf("\nSUMMARY\n");
	for (int i = 0; i < NCASES; i++)
		if (results[i])
			printf("  %-6s %-16s %s\n", cases[i].name, results[i], cases[i].what);
	if (results[p0] && strcmp(results[p0], "quad"))
		printf("  !! P0 didn't draw its quad: every draw result below is void\n");
	if (results[c0] && strcmp(results[c0], "target"))
		printf("  !! C0 didn't clear its target: every clear result below is void\n");
	for (size_t q = 0; q < sizeof(qs) / sizeof(qs[0]); q++) {
		const char *r = results[case_index(qs[q].cname)], *a = NULL;

		for (int k = 0; r && k < 8 && qs[q].a[k].tag; k++)
			if (!strcmp(qs[q].a[k].tag, r))
				a = qs[q].a[k].answer;
		if (a)
			printf("  %-5s %s: %s (%s)\n", qs[q].id, qs[q].text, a, qs[q].cname);
		else if (r)
			printf("  %-5s %s: unclear, %s gave %s\n", qs[q].id, qs[q].text, qs[q].cname, r);
		else
			printf("  %-5s %s: not run (%s)\n", qs[q].id, qs[q].text, qs[q].cname);
	}
	printf("  what each answer changes in the extent: the comment at the top of rsx-clip-probe.c\n");
}

static void list(void)
{
	printf("BO %u MiB (colour and zeta, GART), target at +%u MiB: reach must stay in "
	       "[lo-%u MiB, lo+%u MiB)\n", BO_SIZE / MIB, LO / MIB, LO / MIB, (BO_SIZE - LO) / MIB);
	for (int i = 0; i < NCASES; i++) {
		const struct pcase *c = &cases[i];
		const char *bad = selfcheck(c);
		int64_t a, b, e = validator_extent(c, fmt_bpp(c->fmt));

		reach(c, &a, &b);
		printf("%-6s %-5s pitch %-3u validator extent %lld B, worst-case reach lo%+lld..lo%+lld"
		       " %s\n       %s\n", c->name, c->kind == DRAW ? "draw" : "clear", c->pitch,
		       (long long)e, (long long)a, (long long)b, bad ? bad : "ok", c->what);
	}
}

static void usage(const char *argv0)
{
	fprintf(stderr, "usage: %s --list | [--timeout-ms N] [--keep-going] [CASE...]\n", argv0);
}

int main(int argc, char **argv)
{
	bool keep_going = false, sel = false, want[NCASES] = { false };
	int timeout_ms = 2000, ran = 0, refused = 0, rc = 0;

	setvbuf(stdout, NULL, _IOLBF, 0);
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--list")) {
			list();
			return 0;
		} else if (!strcmp(argv[i], "--keep-going")) {
			keep_going = true;
		} else if (!strcmp(argv[i], "--timeout-ms") && i + 1 < argc) {
			timeout_ms = atoi(argv[++i]);
			if (timeout_ms < 1 || timeout_ms > 30000) {
				fprintf(stderr, "--timeout-ms: 1 to 30000\n");
				return 2;
			}
		} else if (case_index(argv[i]) >= 0) {
			want[case_index(argv[i])] = sel = true;
		} else {
			usage(argv[0]);
			return 2;
		}
	}
	for (int i = 0; i < NCASES; i++) {
		const char *bad = selfcheck(&cases[i]);

		if (bad) {
			printf("case %s fails its own safety check (%s): nothing submitted\n",
			       cases[i].name, bad);
			return 1;
		}
	}
	if (setup())
		return 1;
	for (int i = 0; i < NCASES; i++) {
		if (sel && !want[i])
			continue;
		int r = run_case(i, timeout_ms, keep_going);

		ran += r == 0;
		refused += r == 1;
		if (r == 2) {
			rc = 3;
			break;
		}
	}
	summary();
	printf("%d cases ran, %d refused%s\n", ran, refused, rc ? ", STOPPED early" : "");
	return rc;
}
#endif
