/* ccbench: a timedemo for ClassiCube. A plugin (plugins/ccbench.so next to the game) that flies the
 * camera along keyframes read from a text file and prints the frame rate, then closes the game.
 *
 * It does nothing unless CC_BENCH names the path file, so it is harmless in a normal game.
 *
 * The path is frame-based, like a Quake timedemo: frame k is drawn from the pose interpolated at k,
 * so every run draws the same frames, whatever its speed, and only the duration changes. After the
 * map has loaded, the camera holds the first pose until no chunk has been built for a while (the
 * warm-up); a 128x128x128 map lies within the default view distance from everywhere, so by then
 * every chunk is built and the run builds none (their build budget depends on the frame time).
 *
 * The path file is text (no binary I/O, so byte order doesn't matter):
 *   # comment
 *   warmup MIN QUIET MAX    seconds: at least MIN, until QUIET without chunk builds, at most MAX
 *   FRAME X Y Z YAW PITCH   a keyframe; frames ascend from 0; yaw/pitch in degrees, linear between
 *                           keyframes (no wrapping: 0 -> 360 is a full turn); yaw 0 looks north
 *                           (-Z), 90 east (+X); pitch > 0 looks down
 *
 * Output (stdout, lines starting "ccbench: "):
 *   ccbench: start after N.NN s warm-up          the first measured frame (after the map loaded)
 *   ccbench: per-second fps: a b c ...           frames in each whole second of the run
 *   ccbench: chunks built: W in the warm-up, R during the run      (R = 0: the same frames)
 *   ccbench: N frames S seconds F fps, frame ms min/avg/p99/max A/B/C/D
 * then the game closes as if its window was closed (options are saved, plugins freed).
 * CC_BENCH_SHOTS=<dir>: the back buffer of every keyframe as <dir>/frame-NNNNN.ppm (for checking
 * the path; the time they take is left out of the numbers).
 * CC_BENCH_PASSES=N: the path flown N times, each pass with its own report (after a line
 *   "ccbench: pass K of N"), the game closed after the last: the first passes are a warm-up for
 *   the harness (bench/client.sh counts the later ones). The camera jumps back to the first
 *   pose between passes; the map is built by then, so no pass builds chunks.
 *
 * Built against the headers of the ClassiCube it runs in (Makefile); uses only exported symbols
 * (CC_API/CC_VAR): Game, Camera, Entities, WindowInfo, WindowEvents, Event_RaiseVoid, and
 * glReadPixels/glPixelStorei from the libGL the game links.
 */
#include "src/PluginAPI.h"
#include "src/Game.h"
#include "src/Camera.h"
#include "src/Window.h"
#include "src/Event.h"
#include "src/Entity.h"
#include "src/Vectors.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEG2RAD 0.017453292519943295

struct key { int frame; float x, y, z, yaw, pitch; };

static struct key* keys;
static int nkeys, nframes;
static double warm_min = 2, warm_quiet = 1, warm_max = 30;

enum { OFF, WAIT_MAP, WARMUP, RUN, DONE };
static int state = OFF;
static int frame;                 /* frame being drawn (0..nframes) */
static double t_map, t_quiet, t_last;
static int last_chunks;
static double* ms;                /* frame times, ms (nframes) */
static int resets;                /* times the game replaced our camera during the run */

static struct Camera cam;
static struct Camera* orig;
static int hook = -1;
static long chunks_warm, chunks_run;  /* chunks built in the warm-up and during the run */
static const char* shots;         /* CC_BENCH_SHOTS: a directory for keyframe screenshots */
static int passes = 1, pass = 1;  /* CC_BENCH_PASSES: times the path is flown; the current one */

/* from the game's libGL (the plugin is loaded into the game) */
extern void glReadPixels(int x, int y, int w, int h, unsigned fmt, unsigned type, void* px);
extern void glPixelStorei(unsigned pname, int param);
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_RGB            0x1907
#define GL_UNSIGNED_BYTE  0x1401

static void say(const char* fmt, ...) {
	char buf[1024];
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
	va_end(ap);
	if (n < 0) return;
	if (n > (int)sizeof(buf) - 2) n = sizeof(buf) - 2;
	buf[n++] = '\n';
	/* the game logs with write() to fd 1 as well: no stdio buffer to interleave with */
	if (write(1, buf, n) < 0) { }
}

static double now(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void close_game(void) {
	/* what the SDL window backend does on SDL_EVENT_QUIT: the main loop ends, Game_Free runs */
	WindowInfo.Exists = false;
	Event_RaiseVoid(&WindowEvents.Closing);
}

/* --- the path ---------------------------------------------------------------------------- */
static int load_path(const char* file) {
	char line[512];
	int cap = 0, n = 0, ln = 0;
	FILE* f = fopen(file, "r");
	if (!f) { say("ccbench: error: can't open %s", file); return 0; }

	while (fgets(line, sizeof(line), f)) {
		struct key k;
		char* p = line;
		ln++;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '#' || *p == '\n' || *p == '\r' || !*p) continue;
		if (!strncmp(p, "warmup", 6)) {
			if (sscanf(p + 6, "%lf %lf %lf", &warm_min, &warm_quiet, &warm_max) != 3) goto bad;
			continue;
		}
		if (sscanf(p, "%d %f %f %f %f %f", &k.frame, &k.x, &k.y, &k.z, &k.yaw, &k.pitch) != 6)
			goto bad;
		if (n == 0 ? k.frame != 0 : k.frame <= keys[n - 1].frame) {
			say("ccbench: error: %s:%d: frames must start at 0 and ascend", file, ln);
			fclose(f); return 0;
		}
		if (n == cap) {
			cap = cap ? cap * 2 : 32;
			keys = realloc(keys, cap * sizeof(*keys));
			if (!keys) { fclose(f); return 0; }
		}
		keys[n++] = k;
	}
	fclose(f);
	if (n < 2) { say("ccbench: error: %s: needs at least two keyframes", file); return 0; }
	nkeys = n; nframes = keys[n - 1].frame;
	return 1;
bad:
	say("ccbench: error: %s:%d: can't parse: %s", file, ln, line);
	fclose(f);
	return 0;
}

static struct key pose(int f) {
	struct key r;
	int i = 0;
	float s;
	if (f <= 0) return keys[0];
	if (f >= nframes) return keys[nkeys - 1];
	while (keys[i + 1].frame <= f) i++;
	s = (float)(f - keys[i].frame) / (float)(keys[i + 1].frame - keys[i].frame);
#define LERP(c) r.c = keys[i].c + (keys[i + 1].c - keys[i].c) * s
	LERP(x); LERP(y); LERP(z); LERP(yaw); LERP(pitch);
#undef LERP
	r.frame = f;
	return r;
}

/* --- the camera: the first-person camera with our position and orientation ---------------- */
static Vec2 Bench_GetOrientation(void) {
	struct key k = pose(frame);
	Vec2 v;
	v.x = (float)(k.yaw * DEG2RAD);
	v.y = (float)(k.pitch * DEG2RAD);
	return v;
}

static Vec3 Bench_GetPosition(float t) {
	static const struct Matrix ident = Matrix_IdentityValue;
	struct key k = pose(frame);
	Vec3 v;
	(void)t;
	/* no view bobbing: the first-person camera recomputes these in its GetPosition */
	Camera.TiltM = ident;
	Camera.TiltPitch = 0;
	Camera.BobbingVer = Camera.BobbingHor = 0;
	/* The map renderer recomputes which chunks are in view only when the camera position or the
	 * player's yaw/pitch change: turning on the spot would keep the view before the turn. The
	 * player (hidden in first person) turns with the camera; it only affects block picking. */
	if (Entities.CurPlayer) {
		Entities.CurPlayer->Base.Yaw = k.yaw;
		Entities.CurPlayer->Base.Pitch = k.pitch;
	}
	v.x = k.x; v.y = k.y; v.z = k.z;
	return v;
}

static void use_camera(void) {
	if (Camera.Active == &cam) return;
	if (!orig) {
		orig = Camera.Active;
		cam = *orig;    /* projection, view matrix, mouse, picking: as the game's camera */
		cam.isThirdPerson = false;
		cam.GetOrientation = Bench_GetOrientation;
		cam.GetPosition = Bench_GetPosition;
	}
	if (state == RUN) resets++;
	Camera.Active = &cam;
}

/* The back buffer of frame f as <shots>/frame-NNNNN.ppm (binary PPM: bytes, no byte order) */
static void shot(int f) {
	char path[1024];
	int w = Game.Width, h = Game.Height, y;
	unsigned char* px = malloc((size_t)w * h * 3);
	FILE* o;
	if (!px) return;
	snprintf(path, sizeof(path), "%s/frame-%05d.ppm", shots, f);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
	if ((o = fopen(path, "wb"))) {
		fprintf(o, "P6\n%d %d\n255\n", w, h);
		for (y = h - 1; y >= 0; y--) fwrite(px + (size_t)y * w * 3, 1, (size_t)w * 3, o);
		fclose(o);
	} else {
		say("ccbench: warning: can't write %s", path);
	}
	free(px);
}

static int is_key(int f) {
	int i;
	for (i = 0; i < nkeys; i++) if (keys[i].frame == f) return 1;
	return 0;
}

/* chunks built since the last frame (Game.ChunkUpdates: since the HUD's one-second reset) */
static int chunks_built(void) {
	int c = Game.ChunkUpdates, n = c >= last_chunks ? c - last_chunks : c;
	last_chunks = c;
	return n;
}

/* --- per frame: after the frame's 2D, before the buffer swap ------------------------------ */
static int cmp(const void* a, const void* b) {
	double x = *(const double*)a, y = *(const double*)b;
	return x < y ? -1 : x > y;
}

static void report(void) {
	double secs, sum = 0, *s;
	char line[8192];
	int i, len = 0, sec, cnt = 0;

	s = malloc(nframes * sizeof(double));
	if (!s) return;
	memcpy(s, ms, nframes * sizeof(double));
	qsort(s, nframes, sizeof(double), cmp);
	for (i = 0; i < nframes; i++) sum += s[i];
	secs = sum / 1000;   /* the run's time, less any screenshots */

	/* frames per whole second of the run (the last, partial second left out) */
	len = snprintf(line, sizeof(line), "ccbench: per-second fps:");
	{
		double t = 0;
		sec = 1;
		for (i = 0; i < nframes; i++) {
			cnt++;
			t += ms[i] / 1000;
			while (t >= sec) {
				if (len < (int)sizeof(line) - 16) len += snprintf(line + len, sizeof(line) - len, " %d", cnt);
				cnt = 0; sec++;
			}
		}
	}
	say("%s", line);
	if (passes > 1) say("ccbench: pass %d of %d", pass, passes);
	say("ccbench: chunks built: %ld in the warm-up, %ld during the run", chunks_warm, chunks_run);
	if (resets) say("ccbench: warning: the game switched the camera %d times during the run", resets);
	say("ccbench: %d frames %.2f seconds %.2f fps, frame ms min/avg/p99/max %.1f/%.1f/%.1f/%.1f",
		nframes, secs, nframes / secs, s[0], sum / nframes, s[(nframes * 99) / 100], s[nframes - 1]);
	free(s);
}

static void Bench_Frame(float delta) {
	double t = now();
	(void)delta;

	switch (state) {
	case WARMUP: {
		int n = chunks_built();
		use_camera();
		chunks_warm += n;
		if (n) t_quiet = t;
		if ((t - t_map >= warm_min && t - t_quiet >= warm_quiet) || t - t_map >= warm_max) {
			if (t - t_map >= warm_max) say("ccbench: warning: still building chunks after %.0f s", warm_max);
			say("ccbench: start after %.2f s warm-up", t - t_map);
			/* this frame (pose 0) is frame 0: the run is timed from its end */
			state = RUN; t_last = t;
			if (shots) { shot(0); t_last = now(); }
			frame = 1;
		}
		break;
	}
	case RUN:
		ms[frame - 1] = (t - t_last) * 1000;
		t_last = t;
		chunks_run += chunks_built();
		/* screenshots are for checking the path, not timing: their time is left out */
		if (shots && is_key(frame)) { shot(frame); t_last = now(); }
		if (frame == nframes) {
			report();
			if (pass < passes) {	/* the next pass: from the first pose, timed from now */
				pass++;
				chunks_run = 0; resets = 0;
				frame = 1;
				use_camera();
				t_last = now();
				break;
			}
			state = DONE;
			close_game();
			break;
		}
		frame++;
		use_camera();
		break;
	}
}

/* --- the component ------------------------------------------------------------------------- */
static void Bench_Init(void) {
	const char* file = getenv("CC_BENCH");
	int i;
	if (!file || !*file) return;

	for (i = 0; i < (int)(sizeof(Game.Draw2DHooks) / sizeof(Game.Draw2DHooks[0])); i++)
		if (!Game.Draw2DHooks[i]) { hook = i; break; }
	if (hook < 0) { say("ccbench: error: no free Draw2D hook"); state = DONE; return; }
	if (!load_path(file) || !(ms = calloc(nframes, sizeof(double)))) {
		say("ccbench: closing the game");
		state = DONE; close_game();
		return;
	}
	shots = getenv("CC_BENCH_SHOTS");
	if (shots && !*shots) shots = NULL;
	if (getenv("CC_BENCH_PASSES") && atoi(getenv("CC_BENCH_PASSES")) > 1) passes = atoi(getenv("CC_BENCH_PASSES"));
	Game.Draw2DHooks[hook] = Bench_Frame;
	state = WAIT_MAP;
	say("ccbench: %s: %d keyframes, %d frames; warm-up %g-%g s, %g s without chunk builds",
		file, nkeys, nframes, warm_min, warm_max, warm_quiet);
}

static void Bench_OnNewMapLoaded(void) {
	if (state != WAIT_MAP) return;
	t_map = t_quiet = now();
	last_chunks = Game.ChunkUpdates;
	chunks_warm = chunks_run = 0;
	frame = 0;
	state = WARMUP;
	use_camera();
	say("ccbench: map loaded, warming up");
}

static void Bench_Free(void) {
	if (state == WARMUP || state == RUN)
		say("ccbench: the game closed before the end (frame %d of %d, pass %d of %d)", frame, nframes, pass, passes);
	if (hook >= 0 && Game.Draw2DHooks[hook] == Bench_Frame) Game.Draw2DHooks[hook] = NULL;
	if (orig && Camera.Active == &cam) Camera.Active = orig;
	free(keys); keys = NULL;
	free(ms); ms = NULL;
	state = OFF;
}

PLUGIN_EXPORT int Plugin_ApiVersion = GAME_API_VER;
PLUGIN_EXPORT struct IGameComponent Plugin_Component = {
	Bench_Init,           /* Init */
	Bench_Free,           /* Free */
	NULL,                 /* Reset */
	NULL,                 /* OnNewMap */
	Bench_OnNewMapLoaded, /* OnNewMapLoaded */
};
