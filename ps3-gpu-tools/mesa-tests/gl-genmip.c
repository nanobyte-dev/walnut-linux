/* Mipmaps generated on the GPU, as Neverball (GL_GENERATE_MIPMAP), SuperTuxKart and Luanti
 * (glGenerateMipmap) make them. Mesa renders every level with a blit, power-of-two textures are
 * swizzled on nv30, and the smallest levels are 1 pixel wide or high: a swizzled render target
 * of width 1 is what ps3rsx's validator refuses (docs/GAME-PLAN.md 1.0). For each size and each
 * way, every level is drawn on its own (BASE_LEVEL = MAX_LEVEL = n) and checked: level 0 is one
 * colour, so every level must be that colour. Then the same small levels are rendered through
 * a framebuffer (each level cleared to its own colour). A refused submit shows as wrong levels
 * here and as a validator message in dmesg.
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

/* draws level n of the bound texture over a 16x16 area and checks it is (r,g,b) everywhere */
static int check_level(int n, unsigned r, unsigned g, unsigned b, const char *what)
{
	unsigned char px[16 * 16 * 4];
	int i, bad = 0;

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, n);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, n);
	glClear(GL_COLOR_BUFFER_BIT);
	glViewport(0, 0, 16, 16);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(-1, -1);
	glTexCoord2f(1, 0); glVertex2f(1, -1);
	glTexCoord2f(1, 1); glVertex2f(1, 1);
	glTexCoord2f(0, 1); glVertex2f(-1, 1);
	glEnd();
	glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, px);
	for (i = 0; i < 16 * 16; i++) {
		const unsigned char *p = px + i * 4;
		/* a box filter of one colour stays that colour; allow rounding by one */
		if (abs(p[0] - (int)r) > 1 || abs(p[1] - (int)g) > 1 || abs(p[2] - (int)b) > 1) {
			if (!bad)
				printf("  %s level %d: %02x%02x%02x, expected %02x%02x%02x\n",
				       what, n, p[0], p[1], p[2], r, g, b);
			bad++;
		}
	}
	return bad != 0;
}

/* one texture of w x h in one colour, its mipmaps made by the GPU one way, every level checked */
static int run(int w, int h, int sgis, unsigned colour)
{
	static uint32_t img[512 * 512];
	unsigned r = colour >> 16 & 0xff, g = colour >> 8 & 0xff, b = colour & 0xff;
	char what[64];
	GLuint tex;
	int i, n, levels, fails = 0;

	snprintf(what, sizeof(what), "%dx%d %s", w, h, sgis ? "GL_GENERATE_MIPMAP" : "glGenerateMipmap");
	for (i = 0; i < w * h; i++)	/* RGBA bytes */
		((unsigned char *)&img[i])[0] = r, ((unsigned char *)&img[i])[1] = g,
		((unsigned char *)&img[i])[2] = b, ((unsigned char *)&img[i])[3] = 0xff;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	if (sgis)
		glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
	if (!sgis)
		glGenerateMipmapEXT(GL_TEXTURE_2D);
	glFinish();
	if ((n = glGetError()))
		printf("  %s: glGetError 0x%x\n", what, n);
	for (levels = 1, n = w > h ? w : h; n > 1; n >>= 1)
		levels++;
	for (n = 0; n < levels; n++)
		fails += check_level(n, r, g, b, what);
	printf("%s: %s (%d levels, %d wrong)\n", what, fails ? "FAIL" : "ok", levels, fails);
	glDeleteTextures(1, &tex);
	return fails != 0;
}

/* render to texture: every level of a w x h texture attached to a framebuffer on its own and
 * cleared to its own colour, then each level checked. The same small levels as above, rendered
 * through a framebuffer instead of a blit. */
static int run_fbo(int w, int h, GLuint fb_main, unsigned colour)
{
	char what[64];
	GLuint tex, fb;
	int n, levels, lw, lh, fails = 0;

	snprintf(what, sizeof(what), "%dx%d render to level", w, h);
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	for (levels = 0, lw = w, lh = h; ; levels++, lw = lw > 1 ? lw / 2 : 1, lh = lh > 1 ? lh / 2 : 1) {
		glTexImage2D(GL_TEXTURE_2D, levels, GL_RGBA8, lw, lh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		if (lw == 1 && lh == 1)
			break;
	}
	levels++;
	glGenFramebuffersEXT(1, &fb);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);
	for (n = 0, lw = w, lh = h; n < levels; n++, lw = lw > 1 ? lw / 2 : 1, lh = lh > 1 ? lh / 2 : 1) {
		unsigned c = colour + n * 0x0b1713u;

		glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, tex, n);
		if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT) {
			printf("  %s: level %d (%dx%d) incomplete\n", what, n, lw, lh);
			fails++;
			continue;
		}
		glViewport(0, 0, lw, lh);
		glClearColor((c >> 16 & 0xff) / 255.0f, (c >> 8 & 0xff) / 255.0f, (c & 0xff) / 255.0f, 1);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	glClearColor(0, 0, 0, 0);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb_main);
	glDeleteFramebuffersEXT(1, &fb);
	glFinish();
	if ((n = glGetError()))
		printf("  %s: glGetError 0x%x\n", what, n);
	for (n = 0; n < levels; n++) {
		unsigned c = colour + n * 0x0b1713u;

		fails += check_level(n, c >> 16 & 0xff, c >> 8 & 0xff, c & 0xff, what);
	}
	printf("%s: %s (%d levels, %d wrong)\n", what, fails ? "FAIL" : "ok", levels, fails);
	glDeleteTextures(1, &tex);
	return fails != 0;
}

int main(void)
{
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	EGLContext ctx;
	GLuint fb, rb;
	int fails = 0;
	static const struct { int w, h; } sizes[] = { { 64, 64 }, { 256, 16 }, { 16, 256 }, { 512, 512 } };
	unsigned i, k;

	if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL))
		die("eglInitialize failed");
	eglBindAPI(EGL_OPENGL_API);
	ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
		die("context creation failed: 0x%x", eglGetError());
	printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));

	LOAD(glGenFramebuffersEXT); LOAD(glBindFramebufferEXT); LOAD(glGenRenderbuffersEXT);
	LOAD(glBindRenderbufferEXT); LOAD(glRenderbufferStorageEXT);
	LOAD(glFramebufferRenderbufferEXT); LOAD(glCheckFramebufferStatusEXT); LOAD(glGenerateMipmapEXT);
	LOAD(glFramebufferTexture2DEXT); LOAD(glDeleteFramebuffersEXT);
	glGenFramebuffersEXT(1, &fb);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);
	glGenRenderbuffersEXT(1, &rb);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rb);
	glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, FB, FB);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb);
	if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
		die("framebuffer incomplete");
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

	/* a different colour per run, so a level left over from the run before shows */
	for (k = 0; k < 2; k++)
		for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
			fails += run(sizes[i].w, sizes[i].h, k, 0x203040u + (k * 4 + i) * 0x171d29u);
	for (i = 0; i < 3; i++)
		fails += run_fbo(sizes[i].w, sizes[i].h, fb, 0x405060u + i * 0x2b1d17u);
	printf("%s\n", fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
