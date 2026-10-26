/* Texture compression at upload (Mesa's NV30_FORCE_DXT, the PS3 patch "texture compression at
 * upload"): which glTexImage2D calls get stored as DXT1/DXT5 and that the texture still reads
 * back and draws as the game uploaded it, through everything a game may do afterwards: mipmap
 * levels uploaded one by one, glGenerateMipmap and GL_GENERATE_MIPMAP, glTexSubImage on whole
 * blocks (stays compressed) and off them (back to uncompressed), glCopyTexSubImage and a
 * framebuffer attachment (back to uncompressed), glGetTexImage.
 * The images have one colour per 4x4 block, each exact in RGB565 (and one alpha per block), so
 * DXT1/DXT5 store them without loss and every check is exact but for the GPU's rounding (+-2).
 * The expectations follow NV30_FORCE_DXT (off when unset; opaque; all), so the same binary checks
 * all three:  for m in off opaque all; do NV30_FORCE_DXT=$m gl-forcedxt; done
 * Host reference: LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=softpipe (core Mesa does the work, so
 * softpipe takes the same paths as nv30 up to the driver's blits and transfers).
 */
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define die(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); exit(1); } while (0)
#define FB 64
#define TOL 2

static PFNGLGENFRAMEBUFFERSEXTPROC glGenFramebuffersEXT;
static PFNGLBINDFRAMEBUFFEREXTPROC glBindFramebufferEXT;
static PFNGLGENRENDERBUFFERSEXTPROC glGenRenderbuffersEXT;
static PFNGLBINDRENDERBUFFEREXTPROC glBindRenderbufferEXT;
static PFNGLRENDERBUFFERSTORAGEEXTPROC glRenderbufferStorageEXT;
static PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC glFramebufferRenderbufferEXT;
static PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC glCheckFramebufferStatusEXT;
static PFNGLDELETEFRAMEBUFFERSEXTPROC glDeleteFramebuffersEXT;
static PFNGLGENERATEMIPMAPEXTPROC glGenerateMipmapEXT;
static PFNGLFRAMEBUFFERTEXTURE2DEXTPROC glFramebufferTexture2DEXT;
#define LOAD(f) (f = (void *)eglGetProcAddress(#f))

enum { OFF, OPAQUE, ALL } mode;
static GLuint fb_main;
static int fails, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("  FAIL: "); printf(__VA_ARGS__); \
	printf("\n"); } } while (0)

/* the colour of block (bx, by) of image `seed`, RGB565-exact; alpha 255 or one per block */
static void block_rgba(int seed, int bx, int by, int alpha, uint8_t *p)
{
	unsigned r5 = (bx * 7 + by * 3 + seed * 5) & 31, g6 = (bx * 5 + by * 11 + seed) & 63;
	unsigned b5 = (bx * 13 + by + seed * 3) & 31;

	p[0] = r5 << 3 | r5 >> 2;
	p[1] = g6 << 2 | g6 >> 4;
	p[2] = b5 << 3 | b5 >> 2;
	p[3] = alpha ? (uint8_t)((bx * 37 + by * 91 + seed) & 0xff) : 0xff;
}

/* w x h RGBA bytes (BGRA when bgra) */
static uint8_t *make_image(int seed, int w, int h, int alpha, int bgra)
{
	uint8_t *img = malloc((size_t)w * h * 4);

	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			uint8_t *p = img + 4 * ((size_t)y * w + x), t;

			block_rgba(seed, x / 4, y / 4, alpha, p);
			if (bgra)
				t = p[0], p[0] = p[2], p[2] = t;
		}
	return img;
}

static int compressed(GLint level)
{
	GLint c = -1;

	glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_COMPRESSED, &c);
	return c;
}

static GLint internal_format(GLint level)
{
	GLint f = 0;

	glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_INTERNAL_FORMAT, &f);
	return f;
}

/* draws texels (x0, y0)..(x0+FB, y0+FB) of level `level` (w x h) of the bound texture 1:1 and
 * compares them with expect (RGBA, w x h); alpha too when check_alpha */
static int draw_check(int level, int w, int h, int x0, int y0, const uint8_t *expect,
		      int check_alpha, const char *what)
{
	static uint8_t px[FB * FB * 4];
	int n = FB < w ? FB : w, m = FB < h ? FB : h, bad = 0;

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb_main);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, level);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level);
	glViewport(0, 0, n, m);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	glBegin(GL_QUADS);
	glTexCoord2f((float)x0 / w, (float)y0 / h); glVertex2f(-1, -1);
	glTexCoord2f((float)(x0 + n) / w, (float)y0 / h); glVertex2f(1, -1);
	glTexCoord2f((float)(x0 + n) / w, (float)(y0 + m) / h); glVertex2f(1, 1);
	glTexCoord2f((float)x0 / w, (float)(y0 + m) / h); glVertex2f(-1, 1);
	glEnd();
	glReadPixels(0, 0, n, m, GL_RGBA, GL_UNSIGNED_BYTE, px);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1000);
	for (int y = 0; y < m; y++)
		for (int x = 0; x < n; x++) {
			const uint8_t *p = px + 4 * (y * n + x), *e = expect + 4 * ((size_t)(y + y0) * w + x + x0);

			for (int c = 0; c < (check_alpha ? 4 : 3); c++)
				if (abs(p[c] - e[c]) > TOL) {
					if (!bad)
						printf("  %s: level %d texel %d,%d: %02x%02x%02x%02x, expected %02x%02x%02x%02x\n",
						       what, level, x + x0, y + y0, p[0], p[1], p[2], p[3],
						       e[0], e[1], e[2], e[3]);
					bad++;
					break;
				}
		}
	CHECK(!bad, "%s: %d texels wrong", what, bad);
	return bad;
}

static void readback_check(int level, int w, int h, const uint8_t *expect, int check_alpha,
			   const char *what)
{
	uint8_t *got = malloc((size_t)w * h * 4);
	int bad = 0;

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_UNSIGNED_BYTE, got);
	for (int i = 0; i < w * h; i++)
		for (int c = 0; c < (check_alpha ? 4 : 3); c++)
			if (abs(got[4 * i + c] - expect[4 * i + c]) > TOL) {
				bad++;
				break;
			}
	CHECK(!bad, "%s: glGetTexImage level %d: %d texels wrong", what, level, bad);
	free(got);
}

static GLuint new_texture(GLenum mag)
{
	GLuint t;

	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
	return t;
}

static void expect_compressed(int want, int level, const char *what)
{
	int c = compressed(level);

	CHECK(c == want, "%s: level %d GL_TEXTURE_COMPRESSED %d, expected %d", what, level, c, want);
}

/* the source pixels of RGBA bytes in format/type: RGB/BGR 3 bytes, RGBA/BGRA ubyte 4 bytes in that
 * order, the 8888 packed types as host-order words */
static uint8_t *to_source(const uint8_t *rgba, int n, GLenum fmt, GLenum type)
{
	int rgb = fmt == GL_RGB || fmt == GL_BGR, bgr = fmt == GL_BGR || fmt == GL_BGRA;
	uint8_t *src = malloc((size_t)n * 4);

	for (int i = 0; i < n; i++) {
		const uint8_t *p = rgba + 4 * i;
		uint8_t c0 = bgr ? p[2] : p[0], c2 = bgr ? p[0] : p[2];

		if (rgb) {
			src[3 * i] = c0, src[3 * i + 1] = p[1], src[3 * i + 2] = c2;
		} else if (type == GL_UNSIGNED_INT_8_8_8_8_REV) {	/* first component in the low bits */
			uint32_t v = c0 | p[1] << 8 | c2 << 16 | (uint32_t)p[3] << 24;
			memcpy(src + 4 * i, &v, 4);
		} else if (type == GL_UNSIGNED_INT_8_8_8_8) {		/* first component in the high bits */
			uint32_t v = (uint32_t)c0 << 24 | p[1] << 16 | c2 << 8 | p[3];
			memcpy(src + 4 * i, &v, 4);
		} else {
			src[4 * i] = c0, src[4 * i + 1] = p[1], src[4 * i + 2] = c2, src[4 * i + 3] = p[3];
		}
	}
	return src;
}

/* glTexImage2D of w x h with these formats: compressed or not as the mode says, drawn, read back */
static void upload_case(const char *what, int w, int h, GLint ifmt, GLenum fmt, GLenum type,
			int alpha, int want_opaque, int want_all)
{
	uint8_t *rgba = make_image(w + h, w, h, alpha, 0), *img = to_source(rgba, w * h, fmt, type);
	int want = mode == OFF ? 0 : mode == OPAQUE ? want_opaque : want_all;
	GLuint t = new_texture(GL_LINEAR);
	/* without alpha in the internal format or the source, the alpha reads 255 */
	int keep_alpha = alpha && (ifmt == GL_RGBA || ifmt == GL_RGBA8 || ifmt == 4) && fmt != GL_RGB &&
			 fmt != GL_BGR;

	if (!keep_alpha)
		for (int i = 0; i < w * h; i++)
			rgba[4 * i + 3] = 0xff;
	glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, img);
	expect_compressed(want, 0, what);
	CHECK(internal_format(0) == ifmt, "%s: internal format 0x%x, expected 0x%x", what,
	      internal_format(0), ifmt);
	draw_check(0, w, h, 0, 0, rgba, 1, what);
	if (w >= FB * 2 && h >= FB * 2)
		draw_check(0, w, h, w - FB, h - FB, rgba, 1, what);
	readback_check(0, w, h, rgba, 1, what);
	printf("%s: compressed %d\n", what, compressed(0));
	glDeleteTextures(1, &t);
	free(img);
	free(rgba);
}

int main(void)
{
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	const char *m = getenv("NV30_FORCE_DXT");
	EGLContext ctx;
	GLuint rb, t;
	GLenum e;

	mode = !m || !strcmp(m, "off") || !strcmp(m, "0") ? OFF : !strcmp(m, "all") ? ALL : OPAQUE;
	if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL))
		die("eglInitialize failed");
	eglBindAPI(EGL_OPENGL_API);
	ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
		die("context creation failed: 0x%x", eglGetError());
	printf("GL_RENDERER: %s, NV30_FORCE_DXT=%s\n", glGetString(GL_RENDERER), m ? m : "(unset)");
	LOAD(glGenFramebuffersEXT); LOAD(glBindFramebufferEXT); LOAD(glGenRenderbuffersEXT);
	LOAD(glBindRenderbufferEXT); LOAD(glRenderbufferStorageEXT);
	LOAD(glFramebufferRenderbufferEXT); LOAD(glCheckFramebufferStatusEXT); LOAD(glGenerateMipmapEXT);
	LOAD(glFramebufferTexture2DEXT); LOAD(glDeleteFramebuffersEXT);
	glGenFramebuffersEXT(1, &fb_main);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb_main);
	glGenRenderbuffersEXT(1, &rb);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rb);
	glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, FB, FB);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb);
	if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
		die("framebuffer incomplete");
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	/* 1. what is taken: (opaque mode, all mode) */
	upload_case("256x256 RGBA8 opaque", 256, 256, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 0, 1, 1);
	upload_case("256x256 RGBA alpha", 256, 256, GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, 1, 0, 1);
	upload_case("128x64 RGB from RGBA with alpha", 128, 64, GL_RGB, GL_RGBA, GL_UNSIGNED_BYTE, 1, 1, 1);
	upload_case("64x128 RGB8 from RGB", 64, 128, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, 0, 1, 1);
	upload_case("128x128 RGBA from RGB", 128, 128, GL_RGBA, GL_RGB, GL_UNSIGNED_BYTE, 0, 1, 1);
	upload_case("128x128 BGRA 8888_REV opaque", 128, 128, GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, 0, 1, 1);
	upload_case("128x128 RGBA 8888 alpha", 128, 128, GL_RGBA8, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, 1, 0, 1);
	upload_case("128x128 BGR to RGB", 128, 128, GL_RGB, GL_BGR, GL_UNSIGNED_BYTE, 0, 1, 1);
	upload_case("128x128 BGRA ubyte alpha", 128, 128, 4, GL_BGRA, GL_UNSIGNED_BYTE, 1, 0, 1);
	upload_case("32x32 small", 32, 32, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 0, 0, 0);
	upload_case("96x96 NPOT", 96, 96, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 0, 0, 0);
	upload_case("128x128 RGBA16", 128, 128, GL_RGBA16, GL_RGBA, GL_UNSIGNED_BYTE, 0, 0, 0);

	/* 2. no data, GL_NEAREST magnification: kept */
	t = new_texture(GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	expect_compressed(0, 0, "glTexImage2D(NULL)");
	glDeleteTextures(1, &t);
	{
		uint8_t *img = make_image(3, 128, 128, 0, 0);

		t = new_texture(GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
		expect_compressed(0, 0, "GL_NEAREST magnification");
		glDeleteTextures(1, &t);
		free(img);
	}
	/* a chain made with glTexImage2D(NULL), level 0 given data later: stays uncompressed like
	 * its other levels (one format for all, or the texture is incomplete) */
	{
		uint8_t *img = make_image(4, 128, 128, 0, 0);

		t = new_texture(GL_LINEAR);
		for (int lv = 0, s = 128; s >= 1; s /= 2, lv++)
			glTexImage2D(GL_TEXTURE_2D, lv, GL_RGBA8, s, s, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
		expect_compressed(0, 0, "level 0 given data after a NULL chain");
		draw_check(0, 128, 128, 0, 0, img, 0, "level 0 given data after a NULL chain");
		glDeleteTextures(1, &t);
		free(img);
	}
	printf("no data, GL_NEAREST, NULL chain: checked\n");

	/* 3. a mip chain uploaded level by level: every level follows level 0 */
	{
		int want = mode != OFF, lv = 0;

		t = new_texture(GL_LINEAR);
		for (int s = 256; s >= 1; s /= 2, lv++) {
			uint8_t *img = make_image(lv, s, s, 0, 0);

			glTexImage2D(GL_TEXTURE_2D, lv, GL_RGB, s, s, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
			free(img);
		}
		lv = 0;
		for (int s = 256; s >= 1; s /= 2, lv++) {
			uint8_t *img = make_image(lv, s, s, 0, 0);
			char what[64];

			snprintf(what, sizeof(what), "mip chain %dx%d", s, s);
			expect_compressed(want, lv, what);
			if (s >= 4)	/* DXT stores whole blocks; 2x2 and 1x1 are partial ones */
				draw_check(lv, s, s, 0, 0, img, 0, what);
			free(img);
		}
		glDeleteTextures(1, &t);
		printf("mip chain uploaded by level: checked\n");
	}

	/* 4. glGenerateMipmap and GL_GENERATE_MIPMAP of a compressed-at-upload texture: one colour,
	 * so every level is that colour */
	for (int sgis = 0; sgis < 2; sgis++) {
		uint8_t *img = make_image(0, 128, 128, 0, 0), *lvl;
		const char *what = sgis ? "GL_GENERATE_MIPMAP" : "glGenerateMipmap";

		for (int i = 0; i < 128 * 128; i++)
			memcpy(img + 4 * i, img, 4);
		t = new_texture(GL_LINEAR);
		if (sgis)
			glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
		if (!sgis)
			glGenerateMipmapEXT(GL_TEXTURE_2D);
		for (int lv = 0, s = 128; s >= 1; s /= 2, lv++) {
			char w2[64];

			snprintf(w2, sizeof(w2), "%s level %d", what, lv);
			expect_compressed(mode != OFF, lv, w2);
			draw_check(lv, s, s, 0, 0, img, 0, w2);
		}
		/* a sub-image on whole blocks with GL_GENERATE_MIPMAP: the levels made again */
		if (sgis) {
			lvl = make_image(9, 128, 128, 0, 0);
			for (int i = 0; i < 128 * 128; i++)
				memcpy(lvl + 4 * i, lvl, 4);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, lvl);
			expect_compressed(mode != OFF, 3, "GL_GENERATE_MIPMAP after a whole sub-image");
			draw_check(3, 16, 16, 0, 0, lvl, 0, "GL_GENERATE_MIPMAP after a whole sub-image");
			free(lvl);
		}
		glDeleteTextures(1, &t);
		free(img);
		printf("%s: checked\n", what);
	}

	/* 5. glTexSubImage on whole blocks: stays compressed; off blocks: uncompressed, nothing lost */
	{
		uint8_t *img = make_image(5, 256, 256, 0, 0), *sub = make_image(6, 16, 16, 0, 0);

		t = new_texture(GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 16, 32, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, sub);
		for (int y = 0; y < 16; y++)
			memcpy(img + 4 * ((size_t)(y + 32) * 256 + 16), sub + 4 * y * 16, 16 * 4);
		expect_compressed(mode != OFF, 0, "sub-image 16x16 at 16,32");
		draw_check(0, 256, 256, 0, 0, img, 0, "sub-image 16x16 at 16,32");
		/* 5x3 at 2,3: not on blocks (its pixels any colour: it ends uncompressed) */
		for (int i = 0; i < 15; i++)
			sub[4 * i] = 0x12 + i, sub[4 * i + 1] = 0x34, sub[4 * i + 2] = 0x56 + 3 * i, sub[4 * i + 3] = 0xff;
		glTexSubImage2D(GL_TEXTURE_2D, 0, 2, 3, 5, 3, GL_RGBA, GL_UNSIGNED_BYTE, sub);
		for (int y = 0; y < 3; y++)
			memcpy(img + 4 * ((size_t)(y + 3) * 256 + 2), sub + 4 * y * 5, 5 * 4);
		CHECK((e = glGetError()) == GL_NO_ERROR, "sub-image 5x3 at 2,3: GL error 0x%x", e);
		expect_compressed(0, 0, "sub-image 5x3 at 2,3");
		CHECK(internal_format(0) == GL_RGBA8, "sub-image 5x3: internal format 0x%x", internal_format(0));
		draw_check(0, 256, 256, 0, 0, img, 0, "sub-image 5x3 at 2,3");
		draw_check(0, 256, 256, 192, 192, img, 0, "sub-image 5x3 at 2,3 (far corner)");
		readback_check(0, 256, 256, img, 0, "sub-image 5x3 at 2,3");
		glDeleteTextures(1, &t);
		free(img);
		free(sub);
		printf("sub-images: checked\n");
	}

	/* 6. a framebuffer attachment: back to uncompressed, complete, rendered to; other levels kept */
	{
		uint8_t *img = make_image(7, 128, 128, 0, 0), *l1 = make_image(8, 64, 64, 0, 0);
		GLuint fb;

		t = new_texture(GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
		glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, l1);
		expect_compressed(mode != OFF, 1, "framebuffer texture before");
		glGenFramebuffersEXT(1, &fb);
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);
		glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, t, 0);
		CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) == GL_FRAMEBUFFER_COMPLETE_EXT,
		      "framebuffer texture: incomplete");
		expect_compressed(0, 0, "framebuffer texture");
		expect_compressed(0, 1, "framebuffer texture level 1");
		/* level 1 kept its texels */
		draw_check(1, 64, 64, 0, 0, l1, 0, "framebuffer texture level 1 after");
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);
		glViewport(0, 0, 128, 128);
		glClearColor(0x10 / 255.0f, 0x80 / 255.0f, 0xf0 / 255.0f, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		glClearColor(0, 0, 0, 0);
		for (int i = 0; i < 128 * 128; i++)
			img[4 * i] = 0x10, img[4 * i + 1] = 0x80, img[4 * i + 2] = 0xf0;
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb_main);
		glDeleteFramebuffersEXT(1, &fb);
		draw_check(0, 128, 128, 0, 0, img, 0, "framebuffer texture cleared");
		glDeleteTextures(1, &t);
		free(img);
		free(l1);
		printf("framebuffer attachment: checked\n");
	}

	/* 7. glCopyTexSubImage2D into a compressed-at-upload texture: uncompressed first */
	{
		uint8_t *img = make_image(10, 128, 128, 0, 0);

		t = new_texture(GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb_main);
		glViewport(0, 0, FB, FB);
		glClearColor(0x40 / 255.0f, 0x20 / 255.0f, 0x60 / 255.0f, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		glClearColor(0, 0, 0, 0);
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 3, 5, 0, 0, 8, 8);
		CHECK((e = glGetError()) == GL_NO_ERROR, "glCopyTexSubImage2D: GL error 0x%x", e);
		expect_compressed(0, 0, "glCopyTexSubImage2D");
		for (int y = 5; y < 13; y++)
			for (int x = 3; x < 11; x++)
				img[4 * (y * 128 + x)] = 0x40, img[4 * (y * 128 + x) + 1] = 0x20,
				img[4 * (y * 128 + x) + 2] = 0x60;
		draw_check(0, 128, 128, 0, 0, img, 0, "glCopyTexSubImage2D");
		glDeleteTextures(1, &t);
		free(img);
		printf("glCopyTexSubImage2D: checked\n");
	}

	CHECK((e = glGetError()) == GL_NO_ERROR, "GL error 0x%x at the end", e);
	printf("%d checks, %d failed\n%s\n", checks, fails, fails ? "FAIL" : "PASS");
	eglDestroyContext(dpy, ctx);
	eglTerminate(dpy);
	return fails ? 1 : 0;
}
