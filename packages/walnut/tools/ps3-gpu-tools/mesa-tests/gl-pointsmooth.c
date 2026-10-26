/* GL_POINT_SMOOTH (Mesa 0050, NV30_POINT_SMOOTH): white points of sizes 2..48 on black with
 * blending (SRC_ALPHA, ONE_MINUS_SRC_ALPHA), as Yamagi Quake II draws its particles. Rows from
 * the bottom:
 *   smooth        GL_POINT_SMOOTH, glPointSize
 *   smooth+param  GL_POINT_SMOOTH with point parameters (the size from the vertex program, as
 *                 Quake II's gl1_pointparameters 1), drawn as Quake II draws its particles:
 *                 vertex and colour arrays (unsigned bytes) and glDrawArrays, so the colour is
 *                 no constant of the fragment program (ps3.860 crashed on this, not on row 1)
 *   square        GL_POINT_SMOOTH off: squares
 *   quad          a square drawn as GL_QUADS with GL_POINT_SMOOTH still on (Quake II leaves it
 *                 on for everything): must stay square
 * For each point: lit pixels / S^2 (a square 1.00, a disc 0.79 less its soft edge: nv30 0.60 at S=8, 0.74 at 48), the
 * box corner and centre (red channel); "ok" when round rows are round and square rows square.
 * Exit 1 on any mismatch. Compare with llvmpipe or softpipe (LIBGL_ALWAYS_SOFTWARE=1).
 * NV30_POINT_SMOOTH=0 shows the old squares.
 */
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>

#define die(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); exit(1); } while (0)
#define CELL 64
#define NS 6
#define W (CELL * NS)
#define H (CELL * 4)

static PFNGLGENFRAMEBUFFERSEXTPROC glGenFramebuffersEXT;
static PFNGLBINDFRAMEBUFFEREXTPROC glBindFramebufferEXT;
static PFNGLGENRENDERBUFFERSEXTPROC glGenRenderbuffersEXT;
static PFNGLBINDRENDERBUFFEREXTPROC glBindRenderbufferEXT;
static PFNGLRENDERBUFFERSTORAGEEXTPROC glRenderbufferStorageEXT;
static PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC glFramebufferRenderbufferEXT;
static PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC glCheckFramebufferStatusEXT;
static PFNGLPOINTPARAMETERFVPROC pPointParameterfv;
static PFNGLPOINTPARAMETERFPROC pPointParameterf;
#define LOAD(f) (f = (void *)eglGetProcAddress(#f))

static const int sizes[NS] = { 2, 4, 8, 16, 32, 48 };
static unsigned char px[W * H * 4];

int main(void)
{
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	EGLContext ctx;
	GLuint fb, rb;
	GLfloat range[2];
	int bad = 0;

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
	pPointParameterfv = (void *)eglGetProcAddress("glPointParameterfv");
	pPointParameterf = (void *)eglGetProcAddress("glPointParameterf");
	if (!pPointParameterfv || !pPointParameterf)
		die("no glPointParameterf(v)");
	glGenFramebuffersEXT(1, &fb);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);
	glGenRenderbuffersEXT(1, &rb);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rb);
	glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, W, H);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb);
	if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
		die("framebuffer incomplete");
	glViewport(0, 0, W, H);
	glGetFloatv(GL_SMOOTH_POINT_SIZE_RANGE, range);
	printf("GL_SMOOTH_POINT_SIZE_RANGE: %g %g\n", range[0], range[1]);

	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, W, 0, H, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(1, 1, 1, 1);

	for (int row = 0; row < 4; row++) {
		float cy = row * CELL + CELL / 2;	/* between pixels: the point's box is S x S pixels */
		if (row == 2)
			glDisable(GL_POINT_SMOOTH);
		else
			glEnable(GL_POINT_SMOOTH);
		if (row == 1) {	/* sizes from the vertex program: no attenuation, clamped to [1, 64] */
			GLfloat att[3] = { 1, 0, 0 };
			pPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, att);
			pPointParameterf(GL_POINT_SIZE_MIN, 1);
			pPointParameterf(GL_POINT_SIZE_MAX, 64);
		}
		for (int i = 0; i < NS; i++) {
			float cx = i * CELL + CELL / 2, h = sizes[i] / 2.0f;
			if (row == 3) {
				glBegin(GL_QUADS);
				glVertex2f(cx - h, cy - h); glVertex2f(cx + h, cy - h);
				glVertex2f(cx + h, cy + h); glVertex2f(cx - h, cy + h);
				glEnd();
			} else {
				glPointSize(sizes[i]);
				if (row == 1) {
					static const GLubyte white[4] = { 255, 255, 255, 255 };
					GLfloat v[2] = { cx, cy };
					glEnableClientState(GL_VERTEX_ARRAY);
					glEnableClientState(GL_COLOR_ARRAY);
					glVertexPointer(2, GL_FLOAT, 0, v);
					glColorPointer(4, GL_UNSIGNED_BYTE, 0, white);
					glDrawArrays(GL_POINTS, 0, 1);
					glDisableClientState(GL_VERTEX_ARRAY);
					glDisableClientState(GL_COLOR_ARRAY);
				} else {
					glBegin(GL_POINTS);
					glVertex2f(cx, cy);
					glEnd();
				}
			}
		}
	}
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
	if (glGetError())
		die("GL error");

	static const char *names[4] = { "smooth", "smooth+param", "square", "quad" };
	for (int row = 0; row < 4; row++) {
		int round = row < 2;
		printf("%-13s", names[row]);
		for (int i = 0; i < NS; i++) {
			int S = sizes[i], x0 = i * CELL + CELL / 2 - S / 2, y0 = row * CELL + CELL / 2 - S / 2;
			double lit = 0;
			for (int y = y0 - 2; y < y0 + S + 2; y++)
				for (int x = x0 - 2; x < x0 + S + 2; x++)
					lit += px[(y * W + x) * 4] / 255.0;
			double ratio = lit / (S * S);
			int corner = px[(y0 * W + x0) * 4], centre = px[((y0 + S / 2) * W + x0 + S / 2) * 4];
			int ok = round ? (S < 8 || (corner < 64 && centre > 240 && ratio > 0.5 && ratio < 0.9))
				       : (ratio > 0.95 && corner > 240);
			bad += !ok;
			printf("  S=%-2d %.2f c%3d m%3d %s", S, ratio, corner, centre, ok ? "ok" : round ? "SQUARE" : "WRONG");
		}
		printf("\n");
	}
	printf("%s\n", bad ? "FAIL" : "PASS");
	return bad ? 1 : 0;
}
