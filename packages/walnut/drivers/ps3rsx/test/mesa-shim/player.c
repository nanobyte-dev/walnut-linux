/* mpv's GPU renderer (libmpv's render API: vo_gpu's gl_video) on a 1920x1080 pbuffer of an EGL
 * surfaceless display, N frames: the GL work of mpv --vo=gpu without a window (mpv renders to a
 * framebuffer object only with ARB_framebuffer_object, which nv30 lacks, so to the pbuffer).
 * player CLIP [FRAMES]; MPVMSG: mpv's msg-level (default all=v). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>

static void *gpa(void *ctx, const char *name) { return (void *)eglGetProcAddress(name); }

int main(int argc, char **argv)
{
	int frames = argc > 2 ? atoi(argv[2]) : 10;
	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	EGLDisplay d = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	if (!eglInitialize(d, NULL, NULL)) { fprintf(stderr, "eglInitialize\n"); return 1; }
	eglBindAPI(EGL_OPENGL_API);
	EGLConfig cfg; EGLint n;
	EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
			EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE };
	if (!eglChooseConfig(d, ca, &cfg, 1, &n) || !n) { fprintf(stderr, "config\n"); return 1; }
	EGLint pa[] = { EGL_WIDTH, 1920, EGL_HEIGHT, 1080, EGL_NONE };
	EGLSurface sf = eglCreatePbufferSurface(d, cfg, pa);
	EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, NULL);
	if (!c || !sf || !eglMakeCurrent(d, sf, sf, c)) { fprintf(stderr, "ctx\n"); return 1; }
	fprintf(stderr, "GL %s / %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

	mpv_handle *m = mpv_create();
	mpv_set_option_string(m, "vo", "libmpv");
	mpv_set_option_string(m, "hwdec", "no");
	mpv_set_option_string(m, "ao", "null");
	mpv_set_option_string(m, "terminal", "yes");
	mpv_set_option_string(m, "msg-level", getenv("MPVMSG") ? getenv("MPVMSG") : "all=v");
	if (mpv_initialize(m) < 0) return 1;
	mpv_opengl_init_params ip = { .get_proc_address = gpa };
	mpv_render_param rp[] = {
		{ MPV_RENDER_PARAM_API_TYPE, MPV_RENDER_API_TYPE_OPENGL },
		{ MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &ip },
		{ 0 } };
	mpv_render_context *rc;
	if (mpv_render_context_create(&rc, m, rp) < 0) { fprintf(stderr, "render ctx\n"); return 1; }
	const char *cmd[] = { "loadfile", argv[1], NULL };
	mpv_command(m, cmd);
	int drawn = 0;
	while (drawn < frames) {
		mpv_event *e = mpv_wait_event(m, 0.01);
		if (e->event_id == MPV_EVENT_SHUTDOWN || e->event_id == MPV_EVENT_END_FILE) break;
		if (mpv_render_context_update(rc) & MPV_RENDER_UPDATE_FRAME) {
			mpv_opengl_fbo f = { .fbo = 0, .w = 1920, .h = 1080 };
			int flip = 1;
			mpv_render_param r[] = { { MPV_RENDER_PARAM_OPENGL_FBO, &f },
						 { MPV_RENDER_PARAM_FLIP_Y, &flip }, { 0 } };
			mpv_render_context_render(rc, r);
			eglSwapBuffers(d, sf);
			mpv_render_context_report_swap(rc);
			fprintf(stderr, "frame %d drawn\n", ++drawn);
		}
	}
	mpv_render_context_free(rc);
	mpv_terminate_destroy(m);
	return 0;
}
