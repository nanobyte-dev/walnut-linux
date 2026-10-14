/* ClassiCube's sky: one 1280x1280 quad 34 units above the camera (corners far beyond the far
 * plane and the fog end), blue under white linear fog (end = far = 512, GL_NICEST), seen
 * horizontally in 8 directions. Prints the column at the centre of each view (top to the
 * middle) for comparison with softpipe (LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=softpipe).
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
#define W 160
#define H 90

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
	const float n = 0.1f, f = 512, fovy = 70 * M_PI / 180, aspect = (float)W / H;
	const float t = n * tanf(fovy / 2);

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

	glEnable(GL_FOG);
	glHint(GL_FOG_HINT, GL_NICEST);
	{
		GLfloat white[4] = { 1, 1, 1, 1 };
		glFogfv(GL_FOG_COLOR, white);
	}
	glFogi(GL_FOG_MODE, GL_LINEAR);
	glFogf(GL_FOG_START, 0);
	glFogf(GL_FOG_END, f);
	glEnable(GL_DEPTH_TEST);

	for (int yaw = 0; yaw < 360; yaw += 45) {
		glClearColor(1, 1, 1, 1);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glFrustum(-t * aspect, t * aspect, -t, t, n, f);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		glRotatef(yaw, 0, 1, 0);
		glTranslatef(-128, -40, -128);	/* camera in the middle of a 256x256 world */

		glColor3ub(0x99, 0xcc, 0xff);	/* ClassiCube's default sky colour */
		/* as ClassiCube draws it: two triangles, indices 0 1 2, 2 3 0 */
		static const float v[4][3] = { { -512, 74, -512 }, { -512, 74, 768 },
					       { 768, 74, 768 }, { 768, 74, -512 } };
		static const int idx[6] = { 0, 1, 2, 2, 3, 0 };
		glBegin(GL_TRIANGLES);
		for (int i = 0; i < 6; i++)
			glVertex3fv(v[idx[i]]);
		glEnd();

		glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
		printf("yaw %3d:", yaw);
		for (int y = H - 1; y >= H / 2; y -= 5) {
			unsigned char *p = &px[(y * W + W / 2) * 4];
			printf(" %3d", p[0]);	/* red: 0x99 unfogged, 255 fully fogged */
		}
		printf("\n");
	}
	if (glGetError())
		die("GL error");
	return 0;
}
