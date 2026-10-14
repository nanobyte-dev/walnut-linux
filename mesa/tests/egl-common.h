/* Shared setup for the offscreen EGL/GLES2 tests: display, context, FBO, readback, checksum.
 *
 * Display: surfaceless platform by default (Mesa picks the render node), or the EGL device
 * whose render node is $EGL_RENDER_NODE (e.g. /dev/dri/renderD128). Reference runs on
 * softpipe: LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=softpipe.
 * Each test prints GL_RENDERER and an FNV-1a checksum of the RGBA framebuffer; with an argument
 * it also writes the image there as PPM, so two drivers' outputs can be compared.
 */
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define die(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); exit(1); } while (0)

static EGLDisplay egl_display(void)
{
	const char *node = getenv("EGL_RENDER_NODE");
	EGLDisplay dpy;

	if (node) {
		PFNEGLQUERYDEVICESEXTPROC query_devices =
			(void *)eglGetProcAddress("eglQueryDevicesEXT");
		PFNEGLQUERYDEVICESTRINGEXTPROC query_string =
			(void *)eglGetProcAddress("eglQueryDeviceStringEXT");
		EGLDeviceEXT devs[16];
		EGLint n = 0;

		if (!query_devices || !query_string || !query_devices(16, devs, &n))
			die("EGL device enumeration not available");
		for (int i = 0; i < n; i++) {
			const char *s = query_string(devs[i], EGL_DRM_RENDER_NODE_FILE_EXT);
			if (s && !strcmp(s, node)) {
				dpy = eglGetPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, devs[i], NULL);
				goto found;
			}
		}
		die("no EGL device with render node %s", node);
	}
	dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
found:
	if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL))
		die("eglInitialize failed: 0x%x", eglGetError());
	return dpy;
}

/* GLES2 context without a surface, rendering into a w x h RGBA8 (+ depth16) FBO */
static void egl_setup(int w, int h, int depth)
{
	static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLDisplay dpy = egl_display();
	EGLContext ctx;
	GLuint fb, rb[2];

	eglBindAPI(EGL_OPENGL_ES_API);
	ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctx_attr);
	if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
		die("context creation failed: 0x%x", eglGetError());
	printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));

	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glGenRenderbuffers(2, rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb[0]);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, w, h);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb[0]);
	if (depth) {
		glBindRenderbuffer(GL_RENDERBUFFER, rb[1]);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb[1]);
	}
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		die("framebuffer incomplete: 0x%x", glCheckFramebufferStatus(GL_FRAMEBUFFER));
	glViewport(0, 0, w, h);
}

static inline GLuint gl_program(const char *vs_src, const char *fs_src)
{
	const char *src[2] = { vs_src, fs_src };
	GLenum type[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
	GLuint prog = glCreateProgram();
	char log[1024];
	GLint ok;

	for (int i = 0; i < 2; i++) {
		GLuint s = glCreateShader(type[i]);
		glShaderSource(s, 1, &src[i], NULL);
		glCompileShader(s);
		glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
		if (!ok) {
			glGetShaderInfoLog(s, sizeof(log), NULL, log);
			die("shader %d: %s", i, log);
		}
		glAttachShader(prog, s);
	}
	glBindAttribLocation(prog, 0, "pos");
	glBindAttribLocation(prog, 1, "attr");
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (!ok) {
		glGetProgramInfoLog(prog, sizeof(log), NULL, log);
		die("link: %s", log);
	}
	glUseProgram(prog);
	return prog;
}

/* Reads the framebuffer, prints its checksum, optionally writes it as PPM; returns the pixels */
static uint8_t *readback(int w, int h, const char *ppm)
{
	uint8_t *px = malloc((size_t)w * h * 4);
	uint32_t sum = 0x811c9dc5;

	glFinish();
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
	if (glGetError() != GL_NO_ERROR)
		die("GL error 0x%x", glGetError());
	for (size_t i = 0; i < (size_t)w * h * 4; i++)
		sum = (sum ^ px[i]) * 0x01000193;
	printf("checksum: %08x (%dx%d)\n", sum, w, h);
	if (ppm) {
		FILE *f = fopen(ppm, "wb");
		if (!f)
			die("can't write %s", ppm);
		fprintf(f, "P6\n%d %d\n255\n", w, h);
		for (int y = h - 1; y >= 0; y--)
			for (int x = 0; x < w; x++)
				fwrite(px + ((size_t)y * w + x) * 4, 1, 3, f);
		fclose(f);
	}
	return px;
}

static inline void print_pixel(const uint8_t *px, int w, int x, int y)
{
	const uint8_t *p = px + ((size_t)y * w + x) * 4;
	printf("pixel(%d,%d): %u %u %u %u\n", x, y, p[0], p[1], p[2], p[3]);
}
