/*
 * ps3-xrun-test: proves that ps3-xrunlog.so logs sound dropouts, by making some (docs/games/SUITE.md
 * "Sound dropouts"). It plays a quiet 440 Hz tone and stops writing on purpose, each time until the
 * queued sound has run out plus a known gap, then reads the plugin's log and compares.
 *
 *   ps3-xrun-test [-m] [-D DEVICE] [-a DBFS] [-g GAPS] [-l LIBASOUND] [-n]
 *
 *   -m        mmap writes and a silent snd_pcm_recover, as OpenAL Soft does; default: snd_pcm_writei
 *             and snd_pcm_recover(.., 0) as SDL does (alsa-lib prints "underrun occurred")
 *   -D        the device (default "default": the one ps3-xrunlog wraps)
 *   -a        the tone's loudness, RMS dBFS (default -40: quiet)
 *   -g        the gaps in ms, comma-separated (default 10,40,150,s60); an "s" makes that one silent
 *             on both sides (not audible by bench/xruns.py's rule)
 *   -l        the libasound to dlopen (default libasound.so.2), loaded with dlopen and its functions
 *             with dlsym, as SDL and OpenAL Soft do
 *   -n        don't set up the log: just play (for listening)
 *
 * Unless ALSA_CONFIG_PATH and PS3_XRUNLOG are both set already, it sets them as the harness does
 * (/usr/share/alsa/alsa.conf:/usr/lib/ps3/lib/ps3-xrunlog.conf, /tmp/ps3-xrun-test.<pid>.log).
 * 48 kHz, stereo, S16, a 32 ms buffer (OpenAL's on the PS3), non-blocking: it polls the free space
 * every millisecond like SDL3. Exit status 0 when every gap was logged once, within max(6 ms, 20%),
 * with the tone's loudness (-inf: silence) before and after each.
 */
#define _GNU_SOURCE
#include <alsa/asoundlib.h>
#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define RATE 48000
#define CH 2
#define MAXGAPS 16

#define FN(x) static __typeof__(x) *p_##x
FN(snd_pcm_open); FN(snd_pcm_close); FN(snd_pcm_set_params); FN(snd_pcm_get_params);
FN(snd_pcm_writei); FN(snd_pcm_avail); FN(snd_pcm_avail_update); FN(snd_pcm_mmap_begin);
FN(snd_pcm_mmap_commit); FN(snd_pcm_recover); FN(snd_pcm_state); FN(snd_pcm_start); FN(snd_pcm_delay);
FN(snd_pcm_drain); FN(snd_pcm_nonblock); FN(snd_strerror);

static int mmap_mode;
static double amp, phase;
static snd_pcm_uframes_t period = 256;

static void nap(double s)
{
	struct timespec ts = { (time_t)s, (long)((s - (time_t)s) * 1e9) };
	while (nanosleep(&ts, &ts) && errno == EINTR)
		;
}

static short next_sample(double a)
{
	double v = a * sin(phase);
	phase += 2 * M_PI * 440 / RATE;
	if (phase > 2 * M_PI)
		phase -= 2 * M_PI;
	return (short)lrint(v * 32767);
}

/* write `seconds` of tone at amplitude a (0: silence), recovering from underruns as the clients do */
static int play(snd_pcm_t *pcm, double seconds, double a)
{
	long total = (long)(seconds * RATE), done = 0;
	short buf[4096 * CH];

	while (done < total) {
		snd_pcm_sframes_t av = mmap_mode ? p_snd_pcm_avail_update(pcm) : p_snd_pcm_avail(pcm);
		if (av < 0) {
			int err = p_snd_pcm_recover(pcm, (int)av, mmap_mode);
			if (err < 0) {
				fprintf(stderr, "ps3-xrun-test: recover: %s\n", p_snd_strerror(err));
				return -1;
			}
			continue;
		}
		snd_pcm_uframes_t n = period;
		if ((long)n > total - done)
			n = total - done;
		if ((snd_pcm_uframes_t)av < n) {
			if (mmap_mode && p_snd_pcm_state(pcm) == SND_PCM_STATE_PREPARED)
				p_snd_pcm_start(pcm);	/* OpenAL starts the stream itself once it is full */
			nap(0.001);
			continue;
		}
		if (mmap_mode) {
			const snd_pcm_channel_area_t *areas;
			snd_pcm_uframes_t off, frames = n;
			int err = p_snd_pcm_mmap_begin(pcm, &areas, &off, &frames);
			if (err < 0) {
				p_snd_pcm_recover(pcm, err, 1);
				continue;
			}
			for (snd_pcm_uframes_t f = 0; f < frames; f++) {
				short s = next_sample(a);
				for (int c = 0; c < CH; c++)
					*(short *)((char *)areas[c].addr + (areas[c].first + (off + f) * areas[c].step) / 8) = s;
			}
			snd_pcm_sframes_t r = p_snd_pcm_mmap_commit(pcm, off, frames);
			if (r < 0 || (snd_pcm_uframes_t)r != frames) {
				p_snd_pcm_recover(pcm, r < 0 ? (int)r : -EPIPE, 1);
				continue;
			}
			done += r;
		} else {
			for (snd_pcm_uframes_t f = 0; f < n; f++) {
				short s = next_sample(a);
				for (int c = 0; c < CH; c++)
					buf[f * CH + c] = s;
			}
			snd_pcm_sframes_t r = p_snd_pcm_writei(pcm, buf, n);
			if (r == -EAGAIN) {
				nap(0.001);
				continue;
			}
			if (r < 0) {
				if (p_snd_pcm_recover(pcm, (int)r, 0) < 0)
					return -1;
				continue;
			}
			phase -= 2 * M_PI * 440 / RATE * (n - r);	/* the frames not taken come again */
			done += r;
		}
	}
	return 0;
}

/* stop writing until the queued sound has run out, then gap_ms more */
static void starve(snd_pcm_t *pcm, double gap_ms)
{
	snd_pcm_sframes_t d = 0;
	if (p_snd_pcm_delay(pcm, &d) < 0 || d < 0)
		d = 0;
	nap((double)d / RATE + gap_ms / 1000);
}

struct got { double gap, before, after; };

static int read_log(const char *path, struct got *g, int max)
{
	char line[1024], key[32];
	int n = 0;
	FILE *f = fopen(path, "r");

	if (!f)
		return -1;
	snprintf(key, sizeof(key), "\tpid=%d\t", (int)getpid());
	while (fgets(line, sizeof(line), f)) {
		char *p;
		if (strncmp(line, "xrun\t", 5) || !strstr(line, key))
			continue;
		if (n < max) {
			g[n].gap = (p = strstr(line, "\tgap_ms=")) ? strtod(p + 8, NULL) : NAN;
			g[n].before = (p = strstr(line, "\tbefore_db=")) ? strtod(p + 11, NULL) : NAN;
			g[n].after = (p = strstr(line, "\tafter_db=")) ? strtod(p + 10, NULL) : NAN;
		}
		n++;
	}
	fclose(f);
	return n;
}

static int loud_ok(double got, int silent, double want)
{
	return silent ? isinf(got) && got < 0 : fabs(got - want) <= 1.5;
}

int main(int argc, char **argv)
{
	const char *dev = "default", *lib = "libasound.so.2", *gaps_arg = "10,40,150,s60";
	const char *conf = "/usr/lib/ps3/lib/ps3-xrunlog.conf", *base = "/usr/share/alsa/alsa.conf";
	double dbfs = -40, gap[MAXGAPS];
	int silent[MAXGAPS], ngaps = 0, nolog = 0, opt, bad = 0;
	char logpath[256] = "", cfg[1024];
	snd_pcm_t *pcm;
	snd_pcm_uframes_t buffer = 0;

	while ((opt = getopt(argc, argv, "mD:a:g:l:n")) != -1) {
		switch (opt) {
		case 'm': mmap_mode = 1; break;
		case 'D': dev = optarg; break;
		case 'a': dbfs = atof(optarg); break;
		case 'g': gaps_arg = optarg; break;
		case 'l': lib = optarg; break;
		case 'n': nolog = 1; break;
		default:
			fprintf(stderr, "usage: ps3-xrun-test [-m] [-D DEVICE] [-a DBFS] [-g GAPS] [-l LIBASOUND] [-n]\n");
			return 2;
		}
	}
	for (const char *s = gaps_arg; *s && ngaps < MAXGAPS; ) {
		int sl = *s == 's';
		char *end;
		double v = strtod(s + sl, &end);
		if (end == s + sl) {
			fprintf(stderr, "ps3-xrun-test: -g %s: gaps in ms, comma-separated\n", gaps_arg);
			return 2;
		}
		silent[ngaps] = sl;
		gap[ngaps++] = v;
		s = *end == ',' ? end + 1 : end;
	}
	amp = sqrt(2) * pow(10, dbfs / 20);	/* a sine's peak for that RMS */
	if (!nolog) {
		if (getenv("ALSA_CONFIG_PATH") && getenv("PS3_XRUNLOG")) {
			snprintf(logpath, sizeof(logpath), "%s", getenv("PS3_XRUNLOG"));
		} else {
			if (access(conf, R_OK) || access(base, R_OK)) {
				fprintf(stderr, "ps3-xrun-test: no %s or %s (ps3-gpu-tools with ps3-xrunlog)\n", conf, base);
				return 2;
			}
			snprintf(cfg, sizeof(cfg), "%s:%s", base, conf);
			snprintf(logpath, sizeof(logpath), "/tmp/ps3-xrun-test.%d.log", (int)getpid());
			setenv("ALSA_CONFIG_PATH", cfg, 1);
			setenv("PS3_XRUNLOG", logpath, 1);
		}
	}
	void *h = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
	if (!h) {
		fprintf(stderr, "ps3-xrun-test: %s\n", dlerror());
		return 2;
	}
#define LOAD(x) if (!(p_##x = (__typeof__(x) *)dlsym(h, #x))) { fprintf(stderr, "ps3-xrun-test: no %s\n", #x); return 2; }
	LOAD(snd_pcm_open); LOAD(snd_pcm_close); LOAD(snd_pcm_set_params); LOAD(snd_pcm_get_params);
	LOAD(snd_pcm_writei); LOAD(snd_pcm_avail); LOAD(snd_pcm_avail_update); LOAD(snd_pcm_mmap_begin);
	LOAD(snd_pcm_mmap_commit); LOAD(snd_pcm_recover); LOAD(snd_pcm_state); LOAD(snd_pcm_start);
	LOAD(snd_pcm_delay); LOAD(snd_pcm_drain); LOAD(snd_pcm_nonblock); LOAD(snd_strerror);
	int err = p_snd_pcm_open(&pcm, dev, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
	if (err >= 0)
		err = p_snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16,
					   mmap_mode ? SND_PCM_ACCESS_MMAP_INTERLEAVED : SND_PCM_ACCESS_RW_INTERLEAVED,
					   CH, RATE, 1, 32000);
	if (err < 0) {
		fprintf(stderr, "ps3-xrun-test: %s: %s\n", dev, p_snd_strerror(err));
		return 2;
	}
	p_snd_pcm_get_params(pcm, &buffer, &period);
	printf("ps3-xrun-test: %s, %s, tone %.0f dBFS, buffer %lu frames, period %lu; log %s\n", dev,
	       mmap_mode ? "mmap (as OpenAL Soft)" : "snd_pcm_writei (as SDL)", dbfs, (unsigned long)buffer,
	       (unsigned long)period, *logpath ? logpath : "(none)");
	for (int i = 0; i < ngaps; i++) {
		if (i && silent[i - 1])
			play(pcm, 0.1, 0);	/* the 20 ms after a silent gap are silent too */
		if (play(pcm, 0.5, silent[i] ? 0 : amp) < 0)
			return 1;
		starve(pcm, gap[i]);
	}
	if (ngaps && silent[ngaps - 1])
		play(pcm, 0.1, 0);
	play(pcm, 0.3, amp);
	p_snd_pcm_nonblock(pcm, 0);
	p_snd_pcm_drain(pcm);
	p_snd_pcm_close(pcm);
	if (nolog)
		return 0;

	struct got g[MAXGAPS * 2];
	int n = read_log(logpath, g, MAXGAPS * 2);
	if (n < 0) {
		printf("FAIL: no log at %s (is ps3-xrunlog in front of %s?)\n", logpath, dev);
		return 1;
	}
	printf("%-6s %-9s %-9s %-9s %-9s %s\n", "gap", "expect_ms", "logged_ms", "before_db", "after_db", "audible");
	for (int i = 0; i < ngaps || i < n; i++) {
		char e[16] = "-", l[16] = "-", b[16] = "-", a[16] = "-";
		int ok = i < ngaps && i < n;
		if (i < ngaps)
			snprintf(e, sizeof(e), "%s%.0f", silent[i] ? "s" : "", gap[i]);
		if (i < n) {
			snprintf(l, sizeof(l), "%.1f", g[i].gap);
			snprintf(b, sizeof(b), "%.1f", g[i].before);
			snprintf(a, sizeof(a), "%.1f", g[i].after);
		}
		if (ok) {
			double tol = gap[i] * 0.2 > 6 ? gap[i] * 0.2 : 6;
			/* after a gap comes the next one's sound, or 0.1 s of silence after a silent one */
			int quiet_after = silent[i] || (i + 1 < ngaps && silent[i + 1]);
			ok = fabs(g[i].gap - gap[i]) <= tol && loud_ok(g[i].before, silent[i], dbfs) &&
			     loud_ok(g[i].after, quiet_after, dbfs);
		}
		int audible = i < n && g[i].gap >= 20 && (g[i].before >= -50 || g[i].after >= -50);
		printf("%-6d %-9s %-9s %-9s %-9s %s%s\n", i + 1, e, l, b, a, i < n ? (audible ? "yes" : "no") : "-",
		       ok ? "" : "   <- wrong");
		bad += !ok;
	}
	if (bad)
		printf("FAIL: %d of %d dropouts not as made (%d logged)\n", bad, ngaps, n);
	else
		printf("ok: %d dropouts logged, gaps and loudness as made\n", n);
	return bad ? 1 : 0;
}
