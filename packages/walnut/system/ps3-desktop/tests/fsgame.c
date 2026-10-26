/* fsgame: a stand-in game for autogm-check.sh (built on the host, never shipped). An X window
 * with a GLX context (the GL driver mapped, as in a game), drawing about 20 frames a second and
 * writing the frame count to FILE after each one (so a test sees whether it runs or is frozen).
 *   fsgame [-f] [-o] [-n] [-x] [-H] [-c FILE]
 *     -f  fullscreen through _NET_WM_STATE_FULLSCREEN (SDL2/SDL3, GLFW: most games)
 *     -o  an override-redirect window covering the screen (SDL 1.2, glxgears -fullscreen)
 *     -n  no _NET_WM_PID (the detector falls back on XRes)
 *     -x  no GL at all (a fullscreen program that is not a game)
 *     -H  a hung game: SIGTERM and SIGINT ignored (only SIGKILL ends it)
 *   SIGUSR1: toggles fullscreen with a _NET_WM_STATE client message (F11 in Luanti)
 *   SIGUSR2: destroys the window and makes a new one 1.5 s later (OpenArena's vid_restart) */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <GL/glx.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t toggle, restart;
static void on_usr1(int s) { (void)s; toggle = 1; }
static void on_usr2(int s) { (void)s; restart = 1; }

static Display *dpy;
static XVisualInfo *vi;
static GLXContext ctx;
static int fs, ovr, nopid, nogl, isfs;

static Window make(void)
{
	int scr = DefaultScreen(dpy);
	XSetWindowAttributes a;
	memset(&a, 0, sizeof a);
	Visual *vis = nogl ? DefaultVisual(dpy, scr) : vi->visual;
	int depth = nogl ? DefaultDepth(dpy, scr) : vi->depth;
	a.colormap = XCreateColormap(dpy, RootWindow(dpy, scr), vis, AllocNone);
	a.override_redirect = ovr;
	a.event_mask = StructureNotifyMask | ExposureMask;
	int w = 320, h = 240;
	if (ovr) { w = DisplayWidth(dpy, scr); h = DisplayHeight(dpy, scr); }
	Window win = XCreateWindow(dpy, RootWindow(dpy, scr), 0, 0, w, h, 0, depth, InputOutput, vis,
		CWColormap | CWEventMask | CWOverrideRedirect | CWBorderPixel, &a);
	XStoreName(dpy, win, "fsgame");
	XClassHint ch = { "fsgame", "Fsgame" };
	XSetClassHint(dpy, win, &ch);
	if (!nopid) {
		long pid = getpid();
		XChangeProperty(dpy, win, XInternAtom(dpy, "_NET_WM_PID", False), XA_CARDINAL, 32,
			PropModeReplace, (unsigned char *)&pid, 1);
	}
	char host[256] = "";
	gethostname(host, sizeof host - 1);
	XTextProperty tp = { (unsigned char *)host, XA_STRING, 8, strlen(host) };
	XSetWMClientMachine(dpy, win, &tp);
	if (fs && !ovr) {
		Atom st = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
		XChangeProperty(dpy, win, XInternAtom(dpy, "_NET_WM_STATE", False), XA_ATOM, 32,
			PropModeReplace, (unsigned char *)&st, 1);
		isfs = 1;
	} else
		isfs = ovr;
	XMapRaised(dpy, win);
	if (!nogl) glXMakeCurrent(dpy, win, ctx);
	XFlush(dpy);
	if (!ovr) {	/* focus, as a program started from the menu gets it (source: a pager, honoured) */
		XEvent e;
		usleep(200000);
		memset(&e, 0, sizeof e);
		e.xclient.type = ClientMessage;
		e.xclient.window = win;
		e.xclient.message_type = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
		e.xclient.format = 32;
		e.xclient.data.l[0] = 2;
		XSendEvent(dpy, DefaultRootWindow(dpy), False,
			SubstructureRedirectMask | SubstructureNotifyMask, &e);
		XFlush(dpy);
	}
	return win;
}

static void send_fs(Window win, int on)
{
	XEvent e;
	memset(&e, 0, sizeof e);
	e.xclient.type = ClientMessage;
	e.xclient.window = win;
	e.xclient.message_type = XInternAtom(dpy, "_NET_WM_STATE", False);
	e.xclient.format = 32;
	e.xclient.data.l[0] = on ? 1 : 0;	/* _NET_WM_STATE_ADD / _REMOVE */
	e.xclient.data.l[1] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	e.xclient.data.l[3] = 1;
	XSendEvent(dpy, DefaultRootWindow(dpy), False,
		SubstructureRedirectMask | SubstructureNotifyMask, &e);
	XFlush(dpy);
}

int main(int argc, char **argv)
{
	const char *count = NULL;
	int c;
	int hung = 0;
	while ((c = getopt(argc, argv, "fonxHc:")) != -1)
		switch (c) {
		case 'f': fs = 1; break;
		case 'o': ovr = 1; break;
		case 'n': nopid = 1; break;
		case 'x': nogl = 1; break;
		case 'H': hung = 1; break;
		case 'c': count = optarg; break;
		default: fprintf(stderr, "usage: fsgame [-f] [-o] [-n] [-x] [-H] [-c FILE]\n"); return 2;
		}
	if (!(dpy = XOpenDisplay(NULL))) { fprintf(stderr, "fsgame: no display\n"); return 1; }
	if (!nogl) {
		int attr[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 1, None };
		if (!(vi = glXChooseVisual(dpy, DefaultScreen(dpy), attr))) { fprintf(stderr, "fsgame: no visual\n"); return 1; }
		ctx = glXCreateContext(dpy, vi, NULL, True);
	}
	if (hung) { signal(SIGTERM, SIG_IGN); signal(SIGINT, SIG_IGN); }
	signal(SIGUSR1, on_usr1);
	signal(SIGUSR2, on_usr2);
	Window win = make();
	unsigned long frames = 0;
	for (;;) {
		while (XPending(dpy)) { XEvent e; XNextEvent(dpy, &e); }
		if (toggle) { toggle = 0; isfs = !isfs; send_fs(win, isfs); }
		if (restart) {
			restart = 0;
			if (!nogl) glXMakeCurrent(dpy, None, NULL);
			XDestroyWindow(dpy, win);
			XSync(dpy, False);
			usleep(1500000);
			win = make();
		}
		if (!nogl) {
			glClearColor((frames % 20) / 20.0f, 0.2f, 0.4f, 1);
			glClear(GL_COLOR_BUFFER_BIT);
			glXSwapBuffers(dpy, win);
		}
		frames++;
		if (count) {
			FILE *f = fopen(count, "w");
			if (f) { fprintf(f, "%lu\n", frames); fclose(f); }
		}
		usleep(50000);
	}
}
