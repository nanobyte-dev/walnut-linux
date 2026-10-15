/* Texture uploads the way SDL's desktop-GL renderer streams a window surface (ClassiCube's
 * launcher drew wrong colours and stale areas on the RSX; SDL's GLES2 renderer was fine):
 * a non-power-of-two GL_RGBA8 texture filled from packed 0xAARRGGBB words with GL_BGRA /
 * GL_UNSIGNED_INT_8_8_8_8_REV, drawn 1:1, then a sub-rectangle updated with
 * GL_UNPACK_ROW_LENGTH from the middle of a full-width buffer, drawn again. The same with
 * GL_BGRA / GL_UNSIGNED_BYTE bytes. Every pixel is checked against the expected colour.
 * Each round runs on a non-power-of-two 2D texture (300x200), a power-of-two one (256x128) and a
 * rectangle texture (300x200, ARB_texture_rectangle), the three ways SDL can store it.
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
#define W 300
#define H 200

static PFNGLGENFRAMEBUFFERSEXTPROC glGenFramebuffersEXT;
static PFNGLBINDFRAMEBUFFEREXTPROC glBindFramebufferEXT;
static PFNGLGENRENDERBUFFERSEXTPROC glGenRenderbuffersEXT;
static PFNGLBINDRENDERBUFFEREXTPROC glBindRenderbufferEXT;
static PFNGLRENDERBUFFERSTORAGEEXTPROC glRenderbufferStorageEXT;
static PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC glFramebufferRenderbufferEXT;
static PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC glCheckFramebufferStatusEXT;
#define LOAD(f) (f = (void *)eglGetProcAddress(#f))

static uint32_t img[W * H];	/* what the texture should hold, as 0xAARRGGBB */
static int tw, th;		/* the current texture's size (rows of img are tw long) */
static GLenum target;

/* a different colour per pixel, so swapped channels and shifted rows both show */
static uint32_t pattern(int x, int y, int salt)
{
	return 0xff000000u | (uint32_t)((x * 7 + salt) & 0xff) << 16 |
	       (uint32_t)((y * 5 + salt * 3) & 0xff) << 8 | (uint32_t)((x + y + salt * 11) & 0xff);
}

static void draw(void)
{
	glClear(GL_COLOR_BUFFER_BIT);
	float s = target == GL_TEXTURE_RECTANGLE_ARB ? tw : 1, t = target == GL_TEXTURE_RECTANGLE_ARB ? th : 1;

	glViewport(0, 0, tw, th);
	glBegin(GL_QUADS);	/* texel centres land on pixel centres: exact 1:1 */
	glTexCoord2f(0, 0); glVertex2f(-1, -1);
	glTexCoord2f(s, 0); glVertex2f(1, -1);
	glTexCoord2f(s, t); glVertex2f(1, 1);
	glTexCoord2f(0, t); glVertex2f(-1, 1);
	glEnd();
}

/* compares the framebuffer with img; prints the first mismatches */
static int check(const char *what)
{
	static unsigned char px[W * H * 4];
	int x, y, bad = 0;

	glReadPixels(0, 0, tw, th, GL_RGBA, GL_UNSIGNED_BYTE, px);
	for (y = 0; y < th; y++)
		for (x = 0; x < tw; x++) {
			const unsigned char *p = px + (y * tw + x) * 4;
			uint32_t c = img[y * tw + x];
			unsigned r = c >> 16 & 0xff, g = c >> 8 & 0xff, b = c & 0xff;

			if (p[0] != r || p[1] != g || p[2] != b) {
				if (bad < 4)
					printf("  %s: pixel (%d,%d) is %02x%02x%02x, expected %02x%02x%02x\n",
					       what, x, y, p[0], p[1], p[2], r, g, b);
				bad++;
			}
		}
	printf("%s: %s (%d of %d pixels wrong)\n", what, bad ? "FAIL" : "ok", bad, tw * th);
	return bad != 0;
}

/* one round: full upload, draw, check; sub-rectangle update, draw, check */
static int run(GLenum tgt, int w, int h, GLenum type, const char *tname)
{
	static uint32_t words[W * H];
	static unsigned char bytes[W * H * 4];
	const void *src;
	char what[96];
	int x, y, fails = 0;
	const int sx = 37, sy = 21, sw = 150, sh = 90;	/* the updated rectangle */
	char name[96];

	target = tgt; tw = w; th = h;
	snprintf(name, sizeof(name), "%s %dx%d, %s", tgt == GL_TEXTURE_RECTANGLE_ARB ? "rect" : "2D",
		 w, h, tname);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_TEXTURE_RECTANGLE_ARB);
	glEnable(tgt);
	for (y = 0; y < th; y++)
		for (x = 0; x < tw; x++)
			img[y * tw + x] = pattern(x, y, 0);
	/* GL_UNSIGNED_BYTE + GL_BGRA reads bytes B, G, R, A; 8_8_8_8_REV reads native words */
	for (x = 0; x < tw * th; x++) {
		words[x] = img[x];
		bytes[x * 4 + 0] = img[x] & 0xff;
		bytes[x * 4 + 1] = img[x] >> 8 & 0xff;
		bytes[x * 4 + 2] = img[x] >> 16 & 0xff;
		bytes[x * 4 + 3] = img[x] >> 24;
	}
	src = type == GL_UNSIGNED_BYTE ? (const void *)bytes : (const void *)words;
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	if (getenv("TEXSUB_RGBA")) {	/* debug: the plainest upload, RGBA bytes */
		static unsigned char rgba[W * H * 4];
		for (x = 0; x < tw * th; x++) {
			rgba[x * 4 + 0] = img[x] >> 16; rgba[x * 4 + 1] = img[x] >> 8;
			rgba[x * 4 + 2] = img[x]; rgba[x * 4 + 3] = img[x] >> 24;
		}
		glTexImage2D(tgt, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	} else
		glTexImage2D(tgt, 0, GL_RGBA8, tw, th, 0, GL_BGRA, type, src);
	printf("  glGetError after upload: 0x%x\n", glGetError());
	if (getenv("TEXSUB_REBIND")) {	/* debug: does the upload leave another render target bound? */
		GLint cur;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &cur);
		printf("  framebuffer binding after upload: %d\n", cur);
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 1);
	}
	draw();
	snprintf(what, sizeof(what), "%s full upload", name);
	fails += check(what);

	/* SDL: pointer to the rectangle's first pixel, row length = the surface's width */
	for (y = sy; y < sy + sh; y++)
		for (x = sx; x < sx + sw; x++) {
			uint32_t c = pattern(x, y, 77);
			img[y * tw + x] = c;
			words[y * tw + x] = c;
			bytes[(y * tw + x) * 4 + 0] = c & 0xff;
			bytes[(y * tw + x) * 4 + 1] = c >> 8 & 0xff;
			bytes[(y * tw + x) * 4 + 2] = c >> 16 & 0xff;
			bytes[(y * tw + x) * 4 + 3] = c >> 24;
		}
	glPixelStorei(GL_UNPACK_ROW_LENGTH, tw);
	src = type == GL_UNSIGNED_BYTE ? (const void *)(bytes + (sy * tw + sx) * 4)
				       : (const void *)(words + sy * tw + sx);
	glTexSubImage2D(tgt, 0, sx, sy, sw, sh, GL_BGRA, type, src);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	draw();
	snprintf(what, sizeof(what), "%s sub-rectangle update", name);
	fails += check(what);
	return fails;
}

int main(void)
{
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	EGLContext ctx;
	GLuint fb, rb, tex;
	int fails = 0;

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
	glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, W, H);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb);
	if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
		die("framebuffer incomplete");
	glViewport(0, 0, W, H);

	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, getenv("TEXSUB_MODULATE") ? GL_MODULATE : GL_REPLACE);
	glColor4f(1, 1, 1, 1);
	if (getenv("TEXSUB_MAGENTA"))	/* debug: shows whether the quad is drawn at all */
		glClearColor(1, 0, 1, 1);
	{
		static const struct { GLenum tgt; int w, h; } cfg[] = {
			{ GL_TEXTURE_2D, 256, 128 }, { GL_TEXTURE_2D, W, H },
			{ GL_TEXTURE_RECTANGLE_ARB, W, H },
		};
		unsigned i;

		for (i = 0; i < sizeof(cfg) / sizeof(cfg[0]); i++) {
			glGenTextures(1, &tex);
			glBindTexture(cfg[i].tgt, tex);
			glTexParameteri(cfg[i].tgt, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(cfg[i].tgt, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(cfg[i].tgt, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(cfg[i].tgt, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			fails += run(cfg[i].tgt, cfg[i].w, cfg[i].h, GL_UNSIGNED_INT_8_8_8_8_REV, "BGRA 8_8_8_8_REV");
			fails += run(cfg[i].tgt, cfg[i].w, cfg[i].h, GL_UNSIGNED_BYTE, "BGRA bytes");
		}
	}
	printf("%s\n", fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
