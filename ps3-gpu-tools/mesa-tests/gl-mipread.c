/* Reading back the small levels of swizzled textures, which is what wedged the RSX in Neverball
 * (2026-10-17, graph exception 0x105). Mesa generates the mipmaps of a format nv30 can't render
 * to (L8A8, L8, A8, I8, 4444, 5551) in software: it maps level n-1 for reading, and nv30 copies
 * a level out of a swizzled texture with a 3D blit that samples it as a texture based at the
 * level's offset. In a 32x32 L8A8 texture the 2x2 level is at +0xaa0, and a texture offset the
 * RSX can't take (libgcm: 128-byte aligned) raised the exception. Mesa patch 0014 reads such
 * levels with the CPU instead.
 *
 * WITHOUT patch 0014 (Mesa ps3.223 and older) THIS WEDGES THE RSX UNTIL REBOOT: run it only
 * on a Mesa with 0014.
 *
 *   gl-mipread [get|gen|all] [format...] [WxH...]
 *       default: both parts, every format (RGBA8 first: it never takes the changed path) and
 *       every size (32x32 first: Neverball's, whose blits at aligned offsets ran before the
 *       wedge; 256x16 and 16x256 add blits from 2-pixel-wide or -high swizzled levels)
 *
 * get: every level uploaded with its own value (no mipmap generation), then glGetTexImage of
 *      every level, byte-exact. Before each level it prints the level's offset and the path
 *      Mesa takes (blit, or cpu for an offset that isn't 128-byte aligned): a hang stops right
 *      after the line that caused it.
 * gen: GL_GENERATE_MIPMAP of a one-colour texture (as Neverball), every level drawn on its own
 *      and checked.
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
#define FB 16

static PFNGLGENFRAMEBUFFERSEXTPROC glGenFramebuffersEXT;
static PFNGLBINDFRAMEBUFFEREXTPROC glBindFramebufferEXT;
static PFNGLGENRENDERBUFFERSEXTPROC glGenRenderbuffersEXT;
static PFNGLBINDRENDERBUFFEREXTPROC glBindRenderbufferEXT;
static PFNGLRENDERBUFFERSTORAGEEXTPROC glRenderbufferStorageEXT;
static PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC glFramebufferRenderbufferEXT;
static PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC glCheckFramebufferStatusEXT;
#define LOAD(f) (f = (void *)eglGetProcAddress(#f))

/* one texel value per format (cpp bytes, as uploaded), and what it draws as with GL_REPLACE on
 * a white fragment; every value is exact in its format, so box filtering keeps it */
static const struct fmt {
	const char *name;
	GLenum internal, format, type;
	int cpp;
	unsigned char texel[4];		/* bytes as uploaded (packed shorts in host order below) */
	uint16_t packed;		/* for the packed 16-bit types */
	unsigned char draws[4];		/* RGBA */
} fmts[] = {
	{ "RGBA8", GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, { 0x30, 0x60, 0x90, 0xc0 }, 0,
	  { 0x30, 0x60, 0x90, 0xc0 } },
	{ "LA8", GL_LUMINANCE8_ALPHA8, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, 2, { 0x5a, 0xc3 }, 0,
	  { 0x5a, 0x5a, 0x5a, 0xc3 } },
	{ "RGBA4", GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, 2, { 0 }, 0x369c,
	  { 0x33, 0x66, 0x99, 0xcc } },
	{ "RGB5_A1", GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, 2, { 0 }, 0xf821,
	  { 0xff, 0x00, 0x84, 0xff } },
	{ "L8", GL_LUMINANCE8, GL_LUMINANCE, GL_UNSIGNED_BYTE, 1, { 0x6b }, 0,
	  { 0x6b, 0x6b, 0x6b, 0xff } },
	{ "A8", GL_ALPHA8, GL_ALPHA, GL_UNSIGNED_BYTE, 1, { 0x4d }, 0,
	  { 0xff, 0xff, 0xff, 0x4d } },
	{ "I8", GL_INTENSITY8, GL_LUMINANCE, GL_UNSIGNED_BYTE, 1, { 0x8e }, 0,
	  { 0x8e, 0x8e, 0x8e, 0x8e } },
};

static const struct { int w, h; } sizes[] = { { 32, 32 }, { 64, 64 }, { 8, 8 }, { 256, 16 }, { 16, 256 } };

static unsigned char img[256 * 256 * 4], back[256 * 256 * 4];

/* texel of level n as uploaded: the base value with every byte moved by the level, so a
 * level read back from the wrong place shows */
static void texel(const struct fmt *f, int n, unsigned char out[4])
{
	int k;

	if (f->type != GL_UNSIGNED_BYTE) {
		uint16_t p = f->packed ^ (uint16_t)(n * 0x1357);

		memcpy(out, &p, 2);
		return;
	}
	for (k = 0; k < f->cpp; k++)
		out[k] = f->texel[k] + n * 0x11;
}

static void fill(const struct fmt *f, int n, int w, int h)
{
	unsigned char t[4];
	int i;

	texel(f, n, t);
	for (i = 0; i < w * h; i++)
		memcpy(img + i * f->cpp, t, f->cpp);
}

static int levels_of(int w, int h)
{
	int n = 1;

	while (w > 1 || h > 1) {
		w = w > 1 ? w / 2 : 1;
		h = h > 1 ? h / 2 : 1;
		n++;
	}
	return n;
}

/* every level uploaded on its own, then each read back with glGetTexImage */
static int run_get(const struct fmt *f, int w, int h)
{
	int n, lw, lh, levels = levels_of(w, h), fails = 0;
	unsigned off = 0;
	GLuint tex;

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	for (n = 0, lw = w, lh = h; n < levels; n++, lw = lw > 1 ? lw / 2 : 1, lh = lh > 1 ? lh / 2 : 1) {
		fill(f, n, lw, lh);
		glTexImage2D(GL_TEXTURE_2D, n, f->internal, lw, lh, 0, f->format, f->type, img);
	}
	glFinish();
	for (n = 0, lw = w, lh = h; n < levels; n++, lw = lw > 1 ? lw / 2 : 1, lh = lh > 1 ? lh / 2 : 1) {
		unsigned char t[4];
		int i, bad = 0;

		/* nv30's level offset in a swizzled texture (as Mesa's pick of the format's size) and
		 * the read path of a Mesa with 0014 */
		printf("  get %s %dx%d level %d (%dx%d) at +0x%x: %s%s\n", f->name, w, h, n, lw, lh, off,
		       (off & 127) || lw < 2 || lh < 2 ? "cpu" : "blit",
		       off & 127 ? " (0x105 on a Mesa without 0014)" : "");
		fflush(stdout);
		memset(back, 0xee, (size_t)lw * lh * f->cpp);
		glGetTexImage(GL_TEXTURE_2D, n, f->format, f->type, back);
		texel(f, n, t);
		for (i = 0; i < lw * lh; i++)
			if (memcmp(back + i * f->cpp, t, f->cpp))
				bad++;
		if (bad) {
			printf("  get %s %dx%d level %d: %d of %d texels wrong (first %02x%02x, expected %02x%02x)\n",
			       f->name, w, h, n, bad, lw * lh, back[0], f->cpp > 1 ? back[1] : 0, t[0],
			       f->cpp > 1 ? t[1] : 0);
			fails++;
		}
		off += lw * lh * f->cpp;
	}
	if ((n = glGetError()))
		printf("  get %s %dx%d: glGetError 0x%x\n", f->name, w, h, n);
	glDeleteTextures(1, &tex);
	printf("get %s %dx%d: %s (%d levels, %d wrong)\n", f->name, w, h, fails ? "FAIL" : "ok", levels, fails);
	return fails != 0;
}

/* draws level n of the bound texture over the framebuffer and checks every pixel */
static int check_level(const struct fmt *f, int n, const char *what)
{
	unsigned char px[FB * FB * 4];
	int i, k, bad = 0;

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, n);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, n);
	glClear(GL_COLOR_BUFFER_BIT);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(-1, -1);
	glTexCoord2f(1, 0); glVertex2f(1, -1);
	glTexCoord2f(1, 1); glVertex2f(1, 1);
	glTexCoord2f(0, 1); glVertex2f(-1, 1);
	glEnd();
	glReadPixels(0, 0, FB, FB, GL_RGBA, GL_UNSIGNED_BYTE, px);
	for (i = 0; i < FB * FB; i++)
		for (k = 0; k < 4; k++)
			if (abs(px[i * 4 + k] - f->draws[k]) > 2) {
				if (!bad)
					printf("  %s level %d: %02x%02x%02x%02x, expected %02x%02x%02x%02x\n", what, n,
					       px[i * 4], px[i * 4 + 1], px[i * 4 + 2], px[i * 4 + 3], f->draws[0],
					       f->draws[1], f->draws[2], f->draws[3]);
				bad++;
				break;
			}
	return bad != 0;
}

/* Neverball's way: GL_GENERATE_MIPMAP on, one glTexImage2D of level 0 */
static int run_gen(const struct fmt *f, int w, int h)
{
	int n, levels = levels_of(w, h), fails = 0;
	char what[64];
	GLuint tex;

	snprintf(what, sizeof(what), "gen %s %dx%d", f->name, w, h);
	printf("  %s: GL_GENERATE_MIPMAP\n", what);
	fflush(stdout);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	fill(f, 0, w, h);
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
	glTexImage2D(GL_TEXTURE_2D, 0, f->internal, w, h, 0, f->format, f->type, img);
	glFinish();
	if ((n = glGetError()))
		printf("  %s: glGetError 0x%x\n", what, n);
	for (n = 0; n < levels; n++)
		fails += check_level(f, n, what);
	glDeleteTextures(1, &tex);
	printf("%s: %s (%d levels, %d wrong)\n", what, fails ? "FAIL" : "ok", levels, fails);
	return fails != 0;
}

int main(int argc, char **argv)
{
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	EGLContext ctx;
	int get = 1, gen = 1, a = 1, fails = 0;
	unsigned i, s;
	GLuint fb, rb;

	if (argc > 1 && !strcmp(argv[1], "get"))
		gen = 0, a = 2;
	else if (argc > 1 && !strcmp(argv[1], "gen"))
		get = 0, a = 2;
	else if (argc > 1 && !strcmp(argv[1], "all"))
		a = 2;

	if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL))
		die("eglInitialize failed");
	eglBindAPI(EGL_OPENGL_API);
	ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
		die("context creation failed: 0x%x", eglGetError());
	printf("GL_RENDERER: %s\nGL_VERSION: %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

	LOAD(glGenFramebuffersEXT); LOAD(glBindFramebufferEXT); LOAD(glGenRenderbuffersEXT);
	LOAD(glBindRenderbufferEXT); LOAD(glRenderbufferStorageEXT);
	LOAD(glFramebufferRenderbufferEXT); LOAD(glCheckFramebufferStatusEXT);
	glGenFramebuffersEXT(1, &fb);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);
	glGenRenderbuffersEXT(1, &rb);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rb);
	glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, FB, FB);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb);
	if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
		die("framebuffer incomplete");
	glViewport(0, 0, FB, FB);
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

	for (i = 0; i < sizeof(fmts) / sizeof(fmts[0]); i++) {
		const struct fmt *f = &fmts[i];
		int k, chosen = 1, named = 0, w, h;

		for (k = a; k < argc; k++)	/* a format named: only those */
			if (sscanf(argv[k], "%dx%d", &w, &h) != 2)
				named = 1, chosen = 0;
		for (k = a; k < argc; k++)
			if (!strcmp(argv[k], f->name))
				chosen = 1;
		if (named && !chosen)
			continue;
		for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
			int sized = 0, want = 0;

			for (k = a; k < argc; k++)
				if (sscanf(argv[k], "%dx%d", &w, &h) == 2) {
					sized = 1;
					if (w == sizes[s].w && h == sizes[s].h)
						want = 1;
				}
			if (sized && !want)
				continue;
			if (get)
				fails += run_get(f, sizes[s].w, sizes[s].h);
			if (gen)
				fails += run_gen(f, sizes[s].w, sizes[s].h);
		}
	}
	printf("%s\n", fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
