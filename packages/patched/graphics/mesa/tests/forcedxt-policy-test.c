/* forcedxt-policy-test: NV30_FORCE_DXT's decision (Mesa patch "texture compression at upload",
 * src/mesa/main/texforcedxt_policy.h) on the host: which glTexImage2D calls become DXT1/DXT5, the
 * alpha scan, the block-alignment test for sub-image updates, the byte counts.
 *   cc -I build/work/mesa/include -I build/work/mesa/src packages/patched/graphics/mesa/tests/forcedxt-policy-test.c
 * (tools/run-tests.sh mesa-forcedxt; skipped without build/work/mesa) */
#include <stdio.h>
#include <stdlib.h>
#include "mesa/main/texforcedxt_policy.h"

static int fails, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

static struct fdxt_request base(void)
{
	struct fdxt_request r = {
		.mode = FDXT_OPAQUE, .min = 64, .s3tc = true, .dims = 2, .target_2d = true,
		.level = 0, .internal_format = GL_RGBA, .format = GL_RGBA, .type = GL_UNSIGNED_BYTE,
		.width = 256, .height = 256, .depth = 1, .has_data = true,
	};
	return r;
}

static enum fdxt_result decide(struct fdxt_request r, enum fdxt_why *why)
{
	return fdxt_decide(&r, true, why);
}

int main(void)
{
	enum fdxt_why why;
	struct fdxt_request r;

	/* what is taken */
	r = base();
	CHECK(decide(r, &why) == FDXT_CHECK_ALPHA, "RGBA/RGBA ubyte needs the alpha scan");
	r.internal_format = GL_RGB;
	CHECK(decide(r, &why) == FDXT_DXT1, "RGB internal from RGBA: DXT1, alpha dropped");
	r.internal_format = 3;
	CHECK(decide(r, &why) == FDXT_DXT1, "internal 3");
	r.internal_format = GL_RGB8;
	CHECK(decide(r, &why) == FDXT_DXT1, "GL_RGB8");
	r = base(); r.format = GL_RGB;
	CHECK(decide(r, &why) == FDXT_DXT1, "RGBA internal from an RGB source: opaque, DXT1");
	r.format = GL_BGR;
	CHECK(decide(r, &why) == FDXT_DXT1, "BGR source");
	r = base(); r.format = GL_BGRA; r.type = GL_UNSIGNED_INT_8_8_8_8_REV;
	CHECK(decide(r, &why) == FDXT_CHECK_ALPHA, "BGRA 8888_REV");
	r.internal_format = 4;
	CHECK(decide(r, &why) == FDXT_CHECK_ALPHA, "internal 4");
	r.internal_format = GL_RGBA8;
	CHECK(decide(r, &why) == FDXT_CHECK_ALPHA, "GL_RGBA8");

	/* what is kept, and why */
	static const struct { const char *what; enum fdxt_why why; } keep[] = {
		{ "mode off", FDXT_WHY_OFF },
		{ "1D", FDXT_WHY_TARGET },
		{ "not GL_TEXTURE_2D (cube face, rectangle, proxy)", FDXT_WHY_TARGET },
		{ "level 1 (follows level 0 through Mesa)", FDXT_WHY_LEVEL },
		{ "no data: render target or filled by TexSubImage", FDXT_WHY_NODATA },
		{ "63 wide", FDXT_WHY_SMALL },
		{ "32 high", FDXT_WHY_SMALL },
		{ "NPOT 96x96", FDXT_WHY_NPOT },
		{ "NPOT 256x192", FDXT_WHY_NPOT },
		{ "border", FDXT_WHY_BORDER },
		{ "pixel transfer ops", FDXT_WHY_TRANSFER },
		{ "swap bytes", FDXT_WHY_TRANSFER },
		{ "GL_NEAREST magnification", FDXT_WHY_NEAREST },
		{ "no native s3tc", FDXT_WHY_NOS3TC },
		{ "compressed internal format", FDXT_WHY_FORMAT },
		{ "GL_SRGB8", FDXT_WHY_FORMAT },
		{ "GL_RGB5", FDXT_WHY_FORMAT },
		{ "GL_LUMINANCE internal", FDXT_WHY_FORMAT },
		{ "GL_ALPHA internal (fonts)", FDXT_WHY_FORMAT },
		{ "GL_RGBA16F internal", FDXT_WHY_FORMAT },
		{ "luminance source", FDXT_WHY_FORMAT },
		{ "float source", FDXT_WHY_FORMAT },
		{ "565 source", FDXT_WHY_FORMAT },
		{ "RGB with 8888 type", FDXT_WHY_FORMAT },
		{ "ABGR source", FDXT_WHY_FORMAT },
	};
	for (unsigned i = 0; i < sizeof(keep) / sizeof(keep[0]); i++) {
		struct fdxt_request *q = &r;

		r = base();
		switch (i) {
		case 0: q->mode = FDXT_OFF; break;
		case 1: q->dims = 1; break;
		case 2: q->target_2d = false; break;
		case 3: q->level = 1; break;
		case 4: q->has_data = false; break;
		case 5: q->width = 63; break;
		case 6: q->height = 32; break;
		case 7: (q->width = 96, q->height = 96); break;
		case 8: q->height = 192; break;
		case 9: q->border = 1; break;
		case 10: q->transfer_ops = true; break;
		case 11: q->swap_bytes = true; break;
		case 12: q->mag_nearest = true; break;
		case 13: q->s3tc = false; break;
		case 14: q->internal_format = GL_COMPRESSED_RGB; break;
		case 15: q->internal_format = GL_SRGB8; break;
		case 16: q->internal_format = GL_RGB5; break;
		case 17: q->internal_format = GL_LUMINANCE; break;
		case 18: q->internal_format = GL_ALPHA; break;
		case 19: q->internal_format = GL_RGBA16F; break;
		case 20: q->format = GL_LUMINANCE; break;
		case 21: q->type = GL_FLOAT; break;
		case 22: (q->format = GL_RGB, q->type = GL_UNSIGNED_SHORT_5_6_5); break;
		case 23: (q->format = GL_RGB, q->type = GL_UNSIGNED_INT_8_8_8_8); break;
		case 24: q->format = GL_ABGR_EXT; break;
		}
		enum fdxt_result res = decide(r, &why);
		CHECK(res == FDXT_KEEP && why == keep[i].why, "%s: result %d why %s (expected %s)",
		      keep[i].what, res, fdxt_why_name[why], fdxt_why_name[keep[i].why]);
	}
	r = base(); r.mag_nearest = true; r.nearest_ok = true;
	CHECK(decide(r, &why) == FDXT_CHECK_ALPHA, "GL_NEAREST taken with NV30_FORCE_DXT_NEAREST=1");
	r = base(); r.width = 64; r.height = 64;
	CHECK(decide(r, &why) == FDXT_CHECK_ALPHA, "64x64 is the smallest taken");
	r.min = 128;
	CHECK(decide(r, &why) == FDXT_KEEP && why == FDXT_WHY_SMALL, "NV30_FORCE_DXT_MIN=128");
	r = base(); r.width = 2048; r.height = 64;
	CHECK(decide(r, &why) == FDXT_CHECK_ALPHA, "2048x64");

	/* the alpha result */
	CHECK(fdxt_alpha_result(FDXT_OPAQUE, true, &why) == FDXT_DXT1, "opaque: DXT1");
	CHECK(fdxt_alpha_result(FDXT_OPAQUE, false, &why) == FDXT_KEEP && why == FDXT_WHY_ALPHA,
	      "opaque mode, alpha: kept");
	CHECK(fdxt_alpha_result(FDXT_ALL, false, &why) == FDXT_DXT5, "all, alpha: DXT5");
	CHECK(fdxt_alpha_result(FDXT_ALL, true, &why) == FDXT_DXT1, "all, opaque: DXT1");

	/* where the alpha byte is */
	CHECK(fdxt_src_alpha_byte(GL_RGBA, GL_UNSIGNED_BYTE, true) == 3, "RGBA ubyte");
	CHECK(fdxt_src_alpha_byte(GL_BGRA, GL_UNSIGNED_BYTE, false) == 3, "BGRA ubyte");
	CHECK(fdxt_src_alpha_byte(GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, true) == 0, "8888_REV BE");
	CHECK(fdxt_src_alpha_byte(GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, false) == 3, "8888_REV LE");
	CHECK(fdxt_src_alpha_byte(GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, true) == 3, "8888 BE");
	CHECK(fdxt_src_alpha_byte(GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, false) == 0, "8888 LE");
	CHECK(fdxt_src_alpha_byte(GL_RGB, GL_UNSIGNED_BYTE, true) == -1, "RGB: no alpha");

	/* the scan: every position of one non-255 byte, odd widths, misaligned rows, padding */
	for (int w = 1; w <= 37; w += 3) {
		for (int pad = 0; pad <= 4; pad += 4) {
			for (int off = 0; off < 8; off += 4) {
				int h = 5, a = (w + pad + off) & 3;
				size_t stride = (size_t)w * 4 + pad;
				uint8_t *buf = malloc(stride * h + 8), *img = buf + off;

				memset(buf, 0x11, stride * h + 8);
				for (int y = 0; y < h; y++)
					for (int x = 0; x < w; x++)
						img[y * stride + 4 * x + a] = 0xff;
				CHECK(fdxt_alpha_opaque(img, w, h, stride, a), "opaque w %d pad %d off %d", w, pad, off);
				for (int y = 0; y < h; y++)
					for (int x = 0; x < w; x++) {
						img[y * stride + 4 * x + a] = 0xfe;
						CHECK(!fdxt_alpha_opaque(img, w, h, stride, a),
						      "alpha 254 at %d,%d of %dx%d (pad %d, off %d) not found", x, y, w, h, pad, off);
						img[y * stride + 4 * x + a] = 0xff;
					}
				/* the other bytes and the row padding don't matter */
				CHECK(fdxt_alpha_opaque(img, w, h, stride, a), "colour bytes ignored");
				free(buf);
			}
		}
	}

	/* sub-image alignment */
	CHECK(fdxt_sub_aligned(0, 0, 256, 256, 256, 256), "whole level");
	CHECK(fdxt_sub_aligned(16, 32, 16, 16, 256, 256), "16x16 tile at 16,32");
	CHECK(!fdxt_sub_aligned(2, 0, 16, 16, 256, 256), "x 2");
	CHECK(!fdxt_sub_aligned(0, 1, 16, 16, 256, 256), "y 1");
	CHECK(!fdxt_sub_aligned(0, 0, 10, 16, 256, 256), "width 10 inside");
	CHECK(fdxt_sub_aligned(248, 0, 8, 6, 256, 6), "partial block at the bottom edge");
	CHECK(fdxt_sub_aligned(0, 0, 2, 2, 2, 2), "a 2x2 level whole");
	CHECK(!fdxt_sub_aligned(0, 0, 1, 1, 2, 2), "1x1 of a 2x2 level");

	/* bytes */
	CHECK(fdxt_level_bytes(256, 256, FDXT_KEEP) == 262144, "256x256 X8R8G8B8");
	CHECK(fdxt_level_bytes(256, 256, FDXT_DXT1) == 32768, "256x256 DXT1: an eighth");
	CHECK(fdxt_level_bytes(256, 256, FDXT_DXT5) == 65536, "256x256 DXT5: a quarter");
	CHECK(fdxt_level_bytes(2, 1, FDXT_DXT1) == 8, "2x1: one block");

	printf("forcedxt-policy-test: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
