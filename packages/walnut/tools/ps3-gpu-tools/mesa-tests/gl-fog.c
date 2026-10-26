/* Fixed-function fog (desktop GL compatibility context, like ClassiCube's GL1 backend): blue
 * quads at eye distances 32..512 under white linear fog (start 0, end 512), then exp fog
 * (density 0.004), one column each; then both again through a rotated and translated modelview
 * (fog uses eye-space distance, so the result must not change). Prints every column's colour and the expected one; compare
 * the RSX with softpipe (LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=softpipe).
 */
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define die(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); exit(1); } while (0)
#define W 256
#define H 128
#define COLS 16

/* EXT_framebuffer_object isn't exported by libOpenGL: loaded through EGL */
static PFNGLGENFRAMEBUFFERSEXTPROC glGenFramebuffersEXT;
static PFNGLBINDFRAMEBUFFEREXTPROC glBindFramebufferEXT;
static PFNGLGENRENDERBUFFERSEXTPROC glGenRenderbuffersEXT;
static PFNGLBINDRENDERBUFFEREXTPROC glBindRenderbufferEXT;
static PFNGLRENDERBUFFERSTORAGEEXTPROC glRenderbufferStorageEXT;
static PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC glFramebufferRenderbufferEXT;
static PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC glCheckFramebufferStatusEXT;
#define LOAD(f) (f = (void *)eglGetProcAddress(#f))

int main(void)
{
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	EGLContext ctx;
	GLuint fb, rb[2];
	static unsigned char px[W * H * 4];

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
	glGenRenderbuffersEXT(2, rb);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rb[0]);
	glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, W, H);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb[0]);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rb[1]);
	glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH24_STENCIL8_EXT, W, H);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, rb[1]);
	if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
		die("framebuffer incomplete");
	glViewport(0, 0, W, H);

	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glFrustum(-1, 1, -1, 1, 1, 2000);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glEnable(GL_FOG);
	glHint(GL_FOG_HINT, GL_NICEST);
	{
		GLfloat white[4] = { 1, 1, 1, 1 };
		glFogfv(GL_FOG_COLOR, white);
	}
	glFogf(GL_FOG_START, 0);
	glFogf(GL_FOG_END, 512);
	glFogf(GL_FOG_DENSITY, 0.004f);

	/* rows from the bottom: linear, exp (identity modelview), linear, exp (camera turned 90
	 * degrees about y and moved: modelview T(50,20,-30) R_y(90), vertices given in object space
	 * p = R^-1 (e - t) = (-(ez - tz), ey - ty, ex - tx)); column i at eye distance 32*(i+1) */
	for (int row = 0; row < 4; row++) {
		int moved = row >= 2;
		const float tx = 50, ty = 20, tz = -30;

		glLoadIdentity();
		if (moved) {
			glTranslatef(tx, ty, tz);
			glRotatef(90, 0, 1, 0);
		}
		glFogi(GL_FOG_MODE, row & 1 ? GL_EXP : GL_LINEAR);
		for (int i = 0; i < COLS; i++) {
			float d = 32.0f * (i + 1);
			float x0 = -1 + 2.0f * i / COLS, x1 = x0 + 2.0f / COLS;
			float y0 = -1 + 0.5f * row, y1 = y0 + 0.5f;
			float e[4][3] = { { x0 * d, y0 * d, -d }, { x1 * d, y0 * d, -d },
					  { x1 * d, y1 * d, -d }, { x0 * d, y1 * d, -d } };

			glColor3f(0, 0, 1);
			glBegin(GL_QUADS);
			for (int v = 0; v < 4; v++) {
				if (moved)
					glVertex3f(-(e[v][2] - tz), e[v][1] - ty, e[v][0] - tx);
				else
					glVertex3f(e[v][0], e[v][1], e[v][2]);
			}
			glEnd();
		}
	}
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
	if (glGetError())
		die("GL error");

	/* expected (eye-plane distance; radial differs a little off-centre) */
	for (int row = 0; row < 4; row++) {
		int half = row & 1;
		int y = row * H / 4 + H / 8;
		printf("%s%s:", half ? "exp   " : "linear", row >= 2 ? " moved" : "      ");
		for (int i = 0; i < COLS; i++) {
			unsigned char *p = &px[(y * W + i * W / COLS + W / COLS / 2) * 4];
			printf(" %3d,%3d", p[0], p[2]);
		}
		printf("\nexpect      :");
		for (int i = 0; i < COLS; i++) {
			float d = 32.0f * (i + 1);
			float f = half ? expf(-0.004f * d) : (512 - d) / 512;
			f = f < 0 ? 0 : f > 1 ? 1 : f;
			printf(" %3d,%3d", (int)lrintf((1 - f) * 255), 255);
		}
		printf("\n");
	}
	return 0;
}
