/* A game's texture loads replayed on a 1920x1080 pbuffer of an EGL surfaceless display (GL
 * compatibility, fixed function), for the shim's VRAM limits (wrap.c: PS3SHIM_VRAM_CPU_KB):
 *   texload LIST [FRAMES [DRAWLAST]]
 * LIST has one texture per line, "W H CC" (CC: 1 L8, 2 L8A8, 3 RGB8, 4 RGBA8, as Trigger Rally's
 * PTexture::load picks them), loaded in order with every mip level from glTexImage2D (the game
 * makes them on the CPU). Then FRAMES frames (default 30), each drawing one quad per texture of the
 * last DRAWLAST (default 40: a race's own textures, loaded last) and swapping. Prints the GL errors
 * it saw (GL_OUT_OF_MEMORY: a texture without storage, drawn untextured). TEXLOAD_GENMIP=N:
 * glGenerateMipmapEXT on the last N textures after the load (the GPU renders into them: into GART
 * for those that went there). Made for Trigger Rally's
 * missing textures (gamedb:trigger-rally, Mesa 0055); the list comes from the game's log. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>

int main(int argc, char **argv)
{
	int frames = argc > 2 ? atoi(argv[2]) : 30, drawlast = argc > 3 ? atoi(argv[3]) : 40;
	FILE *f = fopen(argv[1], "r");
	if (!f) { perror(argv[1]); return 1; }
	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	EGLDisplay d = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	if (!eglInitialize(d, NULL, NULL)) { fprintf(stderr, "eglInitialize\n"); return 1; }
	eglBindAPI(EGL_OPENGL_API);
	EGLConfig cfg; EGLint n;
	EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
			EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24,
			EGL_NONE };
	if (!eglChooseConfig(d, ca, &cfg, 1, &n) || !n) { fprintf(stderr, "config\n"); return 1; }
	EGLint pa[] = { EGL_WIDTH, 1920, EGL_HEIGHT, 1080, EGL_NONE };
	EGLSurface sf = eglCreatePbufferSurface(d, cfg, pa);
	EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, NULL);
	if (!c || !sf || !eglMakeCurrent(d, sf, sf, c)) { fprintf(stderr, "ctx\n"); return 1; }
	fprintf(stderr, "GL %s / %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

	static const GLenum fmt[5] = { 0, GL_LUMINANCE, GL_LUMINANCE_ALPHA, GL_RGB, GL_RGBA };
	static const GLenum ifmt[5] = { 0, GL_LUMINANCE8, GL_LUMINANCE8_ALPHA8, GL_RGB8, GL_RGBA8 };
	GLuint *tex = NULL;
	int ntex = 0, w, h, cc, oom = 0;
	unsigned char *px = malloc(2048 * 2048 * 4);
	for (int i = 0; i < 2048 * 2048 * 4; i++)
		px[i] = (unsigned char)(i * 7);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	while (fscanf(f, "%d %d %d", &w, &h, &cc) == 3) {
		tex = realloc(tex, sizeof(*tex) * (ntex + 1));
		glGenTextures(1, &tex[ntex]);
		glBindTexture(GL_TEXTURE_2D, tex[ntex]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		for (int l = 0, lw = w, lh = h;; l++) {
			glTexImage2D(GL_TEXTURE_2D, l, ifmt[cc], lw, lh, 0, fmt[cc], GL_UNSIGNED_BYTE, px);
			if (lw <= 1 && lh <= 1)
				break;
			lw = lw > 1 ? lw / 2 : 1;
			lh = lh > 1 ? lh / 2 : 1;
		}
		for (GLenum e; (e = glGetError()) != GL_NO_ERROR;)
			if (e == GL_OUT_OF_MEMORY)
				oom++;
			else
				fprintf(stderr, "texture %d: GL error %#x\n", ntex, e);
		ntex++;
	}
	fprintf(stderr, "loaded %d textures, %d GL_OUT_OF_MEMORY\n", ntex, oom);
	if (getenv("TEXLOAD_GENMIP")) {
		void (*genmip)(GLenum) = (void *)eglGetProcAddress("glGenerateMipmapEXT");
		int n = atoi(getenv("TEXLOAD_GENMIP"));
		for (int i = ntex > n ? ntex - n : 0; genmip && i < ntex; i++) {
			glBindTexture(GL_TEXTURE_2D, tex[i]);
			genmip(GL_TEXTURE_2D);
		}
		for (GLenum e; (e = glGetError()) != GL_NO_ERROR;)
			fprintf(stderr, "mipmap generation: GL error %#x\n", e);
		fprintf(stderr, "generated the mipmaps of the last %d textures\n", n);
	}

	glEnable(GL_TEXTURE_2D);
	oom = 0;
	for (int fr = 0; fr < frames; fr++) {
		glClearColor(0.2f, 0.3f, 0.4f, 1);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		for (int i = ntex > drawlast ? ntex - drawlast : 0; i < ntex; i++) {
			float x = -1 + 2.0f * (i % 8) / 8, y = -1 + 2.0f * ((i / 8) % 8) / 8;
			glBindTexture(GL_TEXTURE_2D, tex[i]);
			glBegin(GL_QUADS);
			glTexCoord2f(0, 0); glVertex2f(x, y);
			glTexCoord2f(1, 0); glVertex2f(x + 0.25f, y);
			glTexCoord2f(1, 1); glVertex2f(x + 0.25f, y + 0.25f);
			glTexCoord2f(0, 1); glVertex2f(x, y + 0.25f);
			glEnd();
		}
		eglSwapBuffers(d, sf);
		for (GLenum e; (e = glGetError()) != GL_NO_ERROR;)
			if (e == GL_OUT_OF_MEMORY)
				oom++;
			else
				fprintf(stderr, "frame %d: GL error %#x\n", fr, e);
	}
	glFinish();
	fprintf(stderr, "drew %d frames of the last %d textures, %d GL_OUT_OF_MEMORY\n", frames,
		drawlast, oom);
	return 0;
}
