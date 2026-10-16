/* Texture formats narrower than 32 bits per texel, uploaded and drawn 1:1, every texel checked
 * (docs/GAME-PLAN.md 1.0). Only the 8888 paths were tested before (gl-texsub). The RSX reads
 * 32-bit texels as big-endian words, which makes packed 565/4444/1555 come out right; L8A8 is
 * a byte array in Gallium but nv30 samples it as one 16-bit A8L8 texel, so if 16-bit texels are
 * read as big-endian halfwords, luminance and alpha swap (fonts, Neverball's GL_LUMINANCE_ALPHA).
 * Each case prints what Mesa stored (component sizes), then the result.
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
#define N 16	/* texture and viewport: N x N */

static PFNGLGENFRAMEBUFFERSEXTPROC glGenFramebuffersEXT;
static PFNGLBINDFRAMEBUFFEREXTPROC glBindFramebufferEXT;
static PFNGLGENRENDERBUFFERSEXTPROC glGenRenderbuffersEXT;
static PFNGLBINDRENDERBUFFEREXTPROC glBindRenderbufferEXT;
static PFNGLRENDERBUFFERSTORAGEEXTPROC glRenderbufferStorageEXT;
static PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC glFramebufferRenderbufferEXT;
static PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC glCheckFramebufferStatusEXT;
#define LOAD(f) (f = (void *)eglGetProcAddress(#f))

/* n-bit value v widened to 8 bits by bit replication, as the hardware and Mesa do */
static unsigned widen(unsigned v, int n)
{
	unsigned r = v << (8 - n);
	int s;

	for (s = n; s < 8; s += n)
		r |= r >> s;
	return r & 0xff;
}

/* what one texel should draw as (RGBA, GL_REPLACE on a white fragment) */
typedef void expect_fn(int i, unsigned char out[4]);

/* the source data and expectation of every case, texel i = y * N + x */
static unsigned char la[N * N * 2], l8[N * N], a8[N * N];
static uint16_t p565[N * N], p4444[N * N], p5551[N * N];
static unsigned v(int i, int salt) { return (unsigned)(i * 37 + salt * 101) & 0xff; }

static void e_la(int i, unsigned char o[4]) { o[0] = o[1] = o[2] = la[i * 2]; o[3] = la[i * 2 + 1]; }
static void e_l(int i, unsigned char o[4]) { o[0] = o[1] = o[2] = l8[i]; o[3] = 0xff; }
static void e_a(int i, unsigned char o[4]) { o[0] = o[1] = o[2] = 0xff; o[3] = a8[i]; }
static void e_i(int i, unsigned char o[4]) { o[0] = o[1] = o[2] = o[3] = l8[i]; }
static void e_565(int i, unsigned char o[4])
{
	o[0] = widen(p565[i] >> 11, 5); o[1] = widen(p565[i] >> 5 & 63, 6); o[2] = widen(p565[i] & 31, 5); o[3] = 0xff;
}
static void e_4444(int i, unsigned char o[4])
{
	o[0] = widen(p4444[i] >> 12, 4); o[1] = widen(p4444[i] >> 8 & 15, 4);
	o[2] = widen(p4444[i] >> 4 & 15, 4); o[3] = widen(p4444[i] & 15, 4);
}
static void e_5551(int i, unsigned char o[4])
{
	o[0] = widen(p5551[i] >> 11, 5); o[1] = widen(p5551[i] >> 6 & 31, 5);
	o[2] = widen(p5551[i] >> 1 & 31, 5); o[3] = p5551[i] & 1 ? 0xff : 0;
}

static int run(const char *what, GLint ifmt, GLenum fmt, GLenum type, const void *src, expect_fn *expect,
	       int tol)
{
	unsigned char px[N * N * 4];
	GLint rs, gs, bs, as, ls, is;
	GLuint tex;
	int i, bad = 0, err;

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, ifmt, N, N, 0, fmt, type, src);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_RED_SIZE, &rs);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_GREEN_SIZE, &gs);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_BLUE_SIZE, &bs);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_ALPHA_SIZE, &as);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_LUMINANCE_SIZE, &ls);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTENSITY_SIZE, &is);
	if ((err = glGetError()))
		printf("  %s: glGetError 0x%x\n", what, err);
	glClear(GL_COLOR_BUFFER_BIT);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(-1, -1);
	glTexCoord2f(1, 0); glVertex2f(1, -1);
	glTexCoord2f(1, 1); glVertex2f(1, 1);
	glTexCoord2f(0, 1); glVertex2f(-1, 1);
	glEnd();
	glReadPixels(0, 0, N, N, GL_RGBA, GL_UNSIGNED_BYTE, px);
	for (i = 0; i < N * N; i++) {
		unsigned char e[4];
		const unsigned char *p = px + i * 4;
		int c, off = 0;

		expect(i, e);
		for (c = 0; c < 4; c++)
			off |= abs(p[c] - e[c]) > tol;
		if (off) {
			if (bad < 3)
				printf("  %s texel %d: %02x%02x%02x%02x, expected %02x%02x%02x%02x (RGBA)\n", what, i,
				       p[0], p[1], p[2], p[3], e[0], e[1], e[2], e[3]);
			bad++;
		}
	}
	printf("%s: %s (stored R%d G%d B%d A%d L%d I%d; %d of %d texels wrong)\n", what, bad ? "FAIL" : "ok",
	       rs, gs, bs, as, ls, is, bad, N * N);
	glDeleteTextures(1, &tex);
	return bad != 0;
}

int main(void)
{
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	EGLContext ctx;
	GLuint fb, rb;
	int i, fails = 0;

	if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL))
		die("eglInitialize failed");
	eglBindAPI(EGL_OPENGL_API);
	ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
		die("context creation failed: 0x%x", eglGetError());
	printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));

	LOAD(glGenFramebuffersEXT); LOAD(glBindFramebufferEXT); LOAD(glGenRenderbuffersEXT);
	LOAD(glBindRenderbufferEXT); LOAD(glRenderbufferStorageEXT);
	LOAD(glFramebufferRenderbufferEXT); LOAD(glCheckFramebufferStatusEXT);
	glGenFramebuffersEXT(1, &fb);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);
	glGenRenderbuffersEXT(1, &rb);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rb);
	glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, N, N);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb);
	if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
		die("framebuffer incomplete");
	glViewport(0, 0, N, N);
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glColor4f(1, 1, 1, 1);

	for (i = 0; i < N * N; i++) {
		la[i * 2] = v(i, 1); la[i * 2 + 1] = v(i, 2);
		l8[i] = v(i, 3); a8[i] = v(i, 4);
		p565[i] = (uint16_t)(v(i, 5) << 8 | v(i, 6));
		p4444[i] = (uint16_t)(v(i, 7) << 8 | v(i, 8));
		p5551[i] = (uint16_t)(v(i, 9) << 8 | v(i, 10));
	}
	fails += run("LUMINANCE8_ALPHA8 (L, A bytes)", GL_LUMINANCE8_ALPHA8, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, la, e_la, 0);
	fails += run("LUMINANCE_ALPHA (unsized)", GL_LUMINANCE_ALPHA, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, la, e_la, 0);
	fails += run("LUMINANCE8", GL_LUMINANCE8, GL_LUMINANCE, GL_UNSIGNED_BYTE, l8, e_l, 0);
	fails += run("ALPHA8", GL_ALPHA8, GL_ALPHA, GL_UNSIGNED_BYTE, a8, e_a, 0);
	fails += run("INTENSITY8", GL_INTENSITY8, GL_LUMINANCE, GL_UNSIGNED_BYTE, l8, e_i, 0);
	/* GL_RGB565 (ES2_compatibility): GL_RGB5 may be stored 5-5-5 and lose green's low bit */
	fails += run("RGB565 from UNSIGNED_SHORT_5_6_5", GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, p565, e_565, 1);
	fails += run("RGBA4 from UNSIGNED_SHORT_4_4_4_4", GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, p4444, e_4444, 1);
	fails += run("RGB5_A1 from UNSIGNED_SHORT_5_5_5_1", GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, p5551, e_5551, 1);
	printf("%s\n", fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
