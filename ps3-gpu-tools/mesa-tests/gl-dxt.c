/* DXT1 textures compressed by Mesa at upload (glTexImage2D with an RGBA source into
 * GL_COMPRESSED_RGB(A)_S3TC_DXT1): the encoder's speed, its quality, and whether the RSX samples
 * the blocks right. Per format:
 *   - upload time of a 1024x1024 image (Mpixel/s; with libspeq this is its encoder, SPEQ_S3TC
 *     chooses which), averaged over a few uploads;
 *   - a 256x256 image drawn 1:1 with nearest filtering, compared with the blocks read back
 *     (glGetCompressedTexImage) and decoded here: a layout or byte-order bug shows as large
 *     differences, the GPU's own palette rounding only as small ones;
 *   - RMSE of the decoded blocks against the source image (the encoder's quality).
 *   gl-dxt [dir]: also writes dir/<fmt>.src (RGBA) and dir/<fmt>.dxt (blocks) for speq-dxt --compare.
 */
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define die(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); exit(1); } while (0)
#define W 256
#define H 256
#define BIG 1024
#define TOL 8	/* per channel: the GPU's palette interpolation may round differently */

static PFNGLGENFRAMEBUFFERSEXTPROC glGenFramebuffersEXT;
static PFNGLBINDFRAMEBUFFEREXTPROC glBindFramebufferEXT;
static PFNGLGENRENDERBUFFERSEXTPROC glGenRenderbuffersEXT;
static PFNGLBINDRENDERBUFFEREXTPROC glBindRenderbufferEXT;
static PFNGLRENDERBUFFERSTORAGEEXTPROC glRenderbufferStorageEXT;
static PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC glFramebufferRenderbufferEXT;
static PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC glCheckFramebufferStatusEXT;
static PFNGLGETCOMPRESSEDTEXIMAGEPROC glGetCompressedTexImage_;
#define LOAD(f) (f = (void *)eglGetProcAddress(#f))

static double now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec * 1e-9;
}

/* Smooth gradients, flat areas, a ramp and noise, like game textures; with alpha, a band of
 * transparent pixels (alpha 0) and one of half-transparent ones (DXT1: transparent below 128). */
static void make_image(uint8_t *p, int w, int h, int alpha)
{
	uint32_t seed = 12345;

	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++, p += 4) {
			int q = (x * 4 / w) + 4 * (y * 4 / h);	/* 16 regions */

			seed = seed * 1103515245 + 12345;
			switch (q % 4) {
			case 0: p[0] = x * 255 / w; p[1] = y * 255 / h; p[2] = 128; break;
			case 1: p[0] = 200; p[1] = 120; p[2] = 40; break;
			case 2: p[0] = p[1] = p[2] = (x + y) * 255 / (w + h); break;
			default: p[0] = seed >> 24; p[1] = seed >> 16; p[2] = seed >> 8; break;
			}
			p[3] = 255;
			if (alpha && q == 5)
				p[3] = (x / 4 + y / 4) & 1 ? 0 : 255;
			if (alpha && q == 10)
				p[3] = 100 + (x & 3) * 10;
		}
}

static void rgb565(unsigned c, uint8_t *o)
{
	unsigned r = c >> 11 & 31, g = c >> 5 & 63, b = c & 31;

	o[0] = r << 3 | r >> 2; o[1] = g << 2 | g >> 4; o[2] = b << 3 | b >> 2; o[3] = 255;
}

/* the standard DXT1 decoder (S3TC spec): 4-colour mode when color0 > color1 */
static void decode(const uint8_t *blk, uint8_t *out, int w, int h)
{
	for (int by = 0; by < h / 4; by++)
		for (int bx = 0; bx < w / 4; bx++, blk += 8) {
			unsigned c0 = blk[0] | blk[1] << 8, c1 = blk[2] | blk[3] << 8;
			uint32_t idx = blk[4] | blk[5] << 8 | blk[6] << 16 | (uint32_t)blk[7] << 24;
			uint8_t pal[4][4];

			rgb565(c0, pal[0]); rgb565(c1, pal[1]);
			for (int k = 0; k < 3; k++) {
				if (c0 > c1) {
					pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3;
					pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3;
				} else {
					pal[2][k] = (pal[0][k] + pal[1][k]) / 2;
					pal[3][k] = 0;
				}
			}
			pal[2][3] = 255; pal[3][3] = c0 > c1 ? 255 : 0;
			for (int i = 0; i < 16; i++)
				memcpy(out + ((by * 4 + i / 4) * w + bx * 4 + i % 4) * 4, pal[idx >> 2 * i & 3], 4);
		}
}

static void draw(GLuint tex)
{
	glBindTexture(GL_TEXTURE_2D, tex);
	glClear(GL_COLOR_BUFFER_BIT);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(-1, -1);
	glTexCoord2f(1, 0); glVertex2f(1, -1);
	glTexCoord2f(1, 1); glVertex2f(1, 1);
	glTexCoord2f(0, 1); glVertex2f(-1, 1);
	glEnd();
}

static GLuint new_texture(void)
{
	GLuint tex;

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return tex;
}

static void save(const char *dir, const char *name, const char *ext, const void *p, size_t n)
{
	char path[512];
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s.%s", dir, name, ext);
	if (!(f = fopen(path, "wb")) || fwrite(p, 1, n, f) != n || fclose(f))
		die("can't write %s", path);
}

static int run(GLenum fmt, const char *name, const char *dir)
{
	static uint8_t src[W * H * 4], dec[W * H * 4], fb[W * H * 4], big[BIG * BIG * 4];
	static uint8_t blocks[W * H / 2];
	const int alpha = fmt == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, reps = 5;
	GLint size = 0, internal = 0;
	double t, se = 0;
	int bad = 0, maxd = 0;
	GLuint tex;

	/* speed: the big image, a fresh texture each time (as a game loads them) */
	make_image(big, BIG, BIG, alpha);
	t = now();
	for (int i = 0; i < reps; i++) {
		tex = new_texture();
		glTexImage2D(GL_TEXTURE_2D, 0, fmt, BIG, BIG, 0, GL_RGBA, GL_UNSIGNED_BYTE, big);
		glFinish();
		glDeleteTextures(1, &tex);
	}
	t = (now() - t) / reps;
	printf("%s: upload %dx%d %.1f ms, %.1f Mpixel/s\n", name, BIG, BIG, t * 1e3, BIG * BIG / t / 1e6);

	/* correctness and quality: the small image */
	make_image(src, W, H, alpha);
	tex = new_texture();
	glTexImage2D(GL_TEXTURE_2D, 0, fmt, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, src);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internal);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &size);
	if ((GLenum)internal != fmt || size != (GLint)sizeof(blocks))
		die("%s: internal format 0x%x, %d bytes: not compressed (glGetError 0x%x)", name,
		    internal, size, glGetError());
	glGetCompressedTexImage_(GL_TEXTURE_2D, 0, blocks);
	decode(blocks, dec, W, H);
	if (!alpha)	/* an RGB texture samples as opaque, whatever the block mode */
		for (int i = 0; i < W * H; i++)
			dec[4 * i + 3] = 255;
	if (dir) {
		save(dir, name, "src", src, sizeof(src));
		save(dir, name, "dxt", blocks, sizeof(blocks));
	}
	for (int i = 0; i < W * H; i++)	/* quality: decoded against the source, colour channels */
		for (int k = 0; k < 3; k++) {
			int d = (src[4 * i + 3] < 128 && alpha) ? 0 : dec[4 * i + k] - src[4 * i + k];
			se += d * d;
		}
	draw(tex);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, fb);
	for (int i = 0; i < W * H; i++) {	/* the RSX's sampling against our decoder */
		int worst = 0;

		for (int k = 0; k < 4; k++) {
			int d = abs(fb[4 * i + k] - dec[4 * i + k]);
			if (d > worst)
				worst = d;
		}
		if (worst > maxd)
			maxd = worst;
		if (worst > TOL && bad++ < 4)
			printf("  %s: pixel (%d,%d) is %02x%02x%02x%02x, decoded %02x%02x%02x%02x\n", name,
			       i % W, i / W, fb[4 * i], fb[4 * i + 1], fb[4 * i + 2], fb[4 * i + 3],
			       dec[4 * i], dec[4 * i + 1], dec[4 * i + 2], dec[4 * i + 3]);
	}
	printf("%s: RMSE %.2f against the source; sampled vs decoded: max diff %d, %d pixels over %d: %s\n",
	       name, sqrt(se / (W * H * 3)), maxd, bad, TOL, bad ? "FAIL" : "ok");
	glDeleteTextures(1, &tex);
	return bad != 0;
}

int main(int argc, char **argv)
{
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	EGLContext ctx;
	GLuint fb, rb;
	int fails = 0;

	if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL))
		die("eglInitialize failed");
	eglBindAPI(EGL_OPENGL_API);
	ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
		die("context creation failed: 0x%x", eglGetError());
	printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
	if (!strstr((const char *)glGetString(GL_EXTENSIONS), "GL_EXT_texture_compression_s3tc"))
		die("no GL_EXT_texture_compression_s3tc");

	LOAD(glGenFramebuffersEXT); LOAD(glBindFramebufferEXT); LOAD(glGenRenderbuffersEXT);
	LOAD(glBindRenderbufferEXT); LOAD(glRenderbufferStorageEXT);
	LOAD(glFramebufferRenderbufferEXT); LOAD(glCheckFramebufferStatusEXT);
	glGetCompressedTexImage_ = (void *)eglGetProcAddress("glGetCompressedTexImage");
	glGenFramebuffersEXT(1, &fb);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);
	glGenRenderbuffersEXT(1, &rb);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rb);
	glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, W, H);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb);
	if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
		die("framebuffer incomplete");
	glViewport(0, 0, W, H);
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glClearColor(1, 0, 1, 1);

	fails += run(GL_COMPRESSED_RGB_S3TC_DXT1_EXT, "rgb-dxt1", argc > 1 ? argv[1] : NULL);
	fails += run(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, "rgba-dxt1", argc > 1 ? argv[1] : NULL);
	printf("%s\n", fails ? "FAIL" : "PASS");
	return fails != 0;
}
