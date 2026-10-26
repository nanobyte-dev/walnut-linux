/*
 * ps3-xrunlog.so: an ALSA PCM plugin that logs every time a game's sound output ran dry, how long
 * it stayed dry and how loud the sound was on either side (docs/games/SUITE.md "Sound dropouts").
 * bench/harness/client.sh puts it in front of the game's default device for one run, without
 * touching any configuration file:
 *
 *   ALSA_CONFIG_PATH=/usr/share/alsa/alsa.conf:/usr/lib/ps3/lib/ps3-xrunlog.conf \
 *   PS3_XRUNLOG=<log file> <game>
 *
 * ps3-xrunlog.conf adds a configuration hook (ps3_xrunlog_hook, below) that runs after alsa.conf's
 * own (which loads /etc/alsa/conf.d, /etc/asound.conf and the user's ~/.asoundrc): it renames
 * whatever pcm.default those define to pcm.ps3xrun_orig and makes pcm.default this plugin with
 * that as its slave. SDL and OpenAL Soft load libasound with dlopen and open "default", so the
 * plugin sees their sound whatever way they reach the library; no LD_PRELOAD, no symbol tricks.
 *
 * The plugin is an extplug, a filter in the PCM chain: the game's samples are copied to the slave
 * unchanged, with no buffer of its own (each chunk goes on when the game commits it), so the
 * game's buffer, period and timing are the slave's as before. Per chunk it reads the queued
 * frames (snd_pcm_delay: an hwsync of dmix, no wait) to know when the written sound runs out, and
 * keeps the loudness (mean square per frame) of the last 20 ms written.
 *
 * A dropout: the stream is prepared again after frames were written (snd_pcm_recover or a
 * prepare after an xrun: SDL's "underrun occurred", OpenAL's silent recover; also a stop and
 * restart of the stream by the game, which a listener hears just the same), or a chunk arrives
 * after the queued sound already ran out without an xrun (a stop threshold at the boundary). It
 * goes dry at the end of the last write's queue (dry) and plays again when the restarted stream
 * runs (resume: worked out from the frames played by the next chunk). One line per dropout, once
 * the 20 ms after it are written (or at the next prepare, hw_free or close):
 *
 *   xrun pid= comm= n= dry_real= dry= detect= resume= gap_ms= before_db= after_db= via= resumed=
 *
 * dry, detect (the prepare or the late chunk), resume: CLOCK_MONOTONIC seconds; dry_real: the dry
 * moment on CLOCK_REALTIME (client.sh's t0 is $EPOCHREALTIME); gap_ms = resume - dry (with
 * resumed=0, the stream never ran again: up to the line). before_db / after_db: RMS of the 20 ms
 * written before the dropout and the first 20 ms written after it, in dB relative to full scale
 * (a full-scale square wave is 0 dB, a full-scale sine -3 dB); -inf for digital silence, nan
 * when unknown (a sample format it can't read, nothing written). via: prepare or late.
 * Also "open" (the stream's parameters, at hw_params) and "close" lines. Which dropouts can be
 * heard is decided by bench/harness/xruns.py, not here.
 *
 * Without PS3_XRUNLOG (or for capture streams) the plugin only copies.
 */
#define _GNU_SOURCE
#ifndef PIC
#define PIC	/* a shared module: alsa-lib's SND_DLSYM_BUILD_VERSION marks the symbols for its dlsym */
#endif
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define WINDOW_MS 20		/* the loudness windows before and after a dropout */
#define RING_MAX 4096		/* frames: 20 ms up to 192 kHz */
#define LATE_S 0.002		/* a chunk this long after the queue ran out is late (pointer steps) */

struct dropout {
	double detect, dry, resume;	/* CLOCK_MONOTONIC; resume < 0: not running again yet */
	double dry_real;
	double before;			/* mean square of the window before; NAN unknown */
	double after_sum;
	unsigned after_n;
	const char *via;
};

struct tap {
	snd_pcm_extplug_t ext;
	int fd;				/* the log; -1: copy only */
	int playback, readable;
	unsigned rate, channels, window;
	snd_pcm_format_t format;
	char comm[20];
	float ring[RING_MAX];		/* mean square of the last `window` frames written */
	unsigned pos, fill;
	unsigned long long seg;		/* frames written since the last prepare */
	unsigned long long frames;	/* in all */
	double dry;			/* when the written sound runs out; 0: unknown */
	int pending;
	struct dropout x;
	unsigned long n;
};

static double clock_s(clockid_t c)
{
	struct timespec ts;

	clock_gettime(c, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void say(struct tap *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void say(struct tap *t, const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n > 0 && t->fd >= 0)
		(void)!write(t->fd, buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
}

/* sample formats whose loudness it reads */
static int readable(snd_pcm_format_t f)
{
	switch (f) {
	case SND_PCM_FORMAT_S8: case SND_PCM_FORMAT_U8:
	case SND_PCM_FORMAT_S16_LE: case SND_PCM_FORMAT_S16_BE:
	case SND_PCM_FORMAT_S24_LE: case SND_PCM_FORMAT_S24_BE:
	case SND_PCM_FORMAT_S32_LE: case SND_PCM_FORMAT_S32_BE:
	case SND_PCM_FORMAT_FLOAT_LE: case SND_PCM_FORMAT_FLOAT_BE:
	case SND_PCM_FORMAT_FLOAT64_LE: case SND_PCM_FORMAT_FLOAT64_BE:
		return 1;
	default:
		return 0;
	}
}

static unsigned long long be(const unsigned char *p, int n)
{
	unsigned long long v = 0;
	for (int i = 0; i < n; i++)
		v = v << 8 | p[i];
	return v;
}

static unsigned long long le(const unsigned char *p, int n)
{
	unsigned long long v = 0;
	for (int i = n - 1; i >= 0; i--)
		v = v << 8 | p[i];
	return v;
}

/* one sample as a fraction of full scale */
static double sample(const unsigned char *p, snd_pcm_format_t f)
{
	unsigned long long v;
	union { unsigned int u; float f; } f32;
	union { unsigned long long u; double d; } f64;

	switch (f) {
	case SND_PCM_FORMAT_S8: return (signed char)p[0] / 128.0;
	case SND_PCM_FORMAT_U8: return (p[0] - 128) / 128.0;
	case SND_PCM_FORMAT_S16_LE: return (short)le(p, 2) / 32768.0;
	case SND_PCM_FORMAT_S16_BE: return (short)be(p, 2) / 32768.0;
	case SND_PCM_FORMAT_S24_LE: v = le(p, 3); break;	/* the low 3 bytes of a 4-byte word */
	case SND_PCM_FORMAT_S24_BE: v = be(p + 1, 3); break;
	case SND_PCM_FORMAT_S32_LE: return (int)le(p, 4) / 2147483648.0;
	case SND_PCM_FORMAT_S32_BE: return (int)be(p, 4) / 2147483648.0;
	case SND_PCM_FORMAT_FLOAT_LE: f32.u = le(p, 4); return f32.f;
	case SND_PCM_FORMAT_FLOAT_BE: f32.u = be(p, 4); return f32.f;
	case SND_PCM_FORMAT_FLOAT64_LE: f64.u = le(p, 8); return f64.d;
	case SND_PCM_FORMAT_FLOAT64_BE: f64.u = be(p, 8); return f64.d;
	default: return 0;
	}
	return (v & 0x800000 ? (long long)v - 0x1000000 : (long long)v) / 8388608.0;
}

static double window_ms(const struct tap *t)
{
	double s = 0;

	if (!t->readable || !t->fill)
		return NAN;
	for (unsigned i = 0; i < t->fill; i++)
		s += t->ring[i];
	return s / t->fill;
}

static void db(char *buf, size_t n, double ms)
{
	if (isnan(ms))
		snprintf(buf, n, "nan");
	else if (ms <= 1e-15)
		snprintf(buf, n, "-inf");
	else
		snprintf(buf, n, "%.1f", 10 * log10(ms));
}

static void emit(struct tap *t, double now)
{
	struct dropout *x = &t->x;
	int resumed = x->resume >= 0;
	char b[16], a[16], r[24];

	if (!t->pending)
		return;
	t->pending = 0;
	t->n++;
	db(b, sizeof(b), x->before);
	db(a, sizeof(a), t->readable && x->after_n ? x->after_sum / x->after_n : NAN);
	if (resumed)
		snprintf(r, sizeof(r), "%.6f", x->resume);
	else
		snprintf(r, sizeof(r), "-");
	say(t, "xrun\tpid=%d\tcomm=%s\tn=%lu\tdry_real=%.6f\tdry=%.6f\tdetect=%.6f\tresume=%s\tgap_ms=%.1f\t"
	    "before_db=%s\tafter_db=%s\tvia=%s\tresumed=%d\n", (int)getpid(), t->comm, t->n, x->dry_real, x->dry,
	    x->detect, r, ((resumed ? x->resume : now) - x->dry) * 1000, b, a, x->via, resumed);
}

static void begin(struct tap *t, double now, const char *via)
{
	struct dropout *x = &t->x;

	t->pending = 1;
	x->detect = now;
	x->dry = t->dry > 0 && t->dry < now ? t->dry : now;	/* unknown, or not yet out: from now */
	x->dry_real = clock_s(CLOCK_REALTIME) - (now - x->dry);
	x->before = window_ms(t);
	x->after_sum = 0;
	x->after_n = 0;
	x->resume = -1;
	x->via = via;
}

/* the loudness of the frames written: the ring before a dropout, the window after it */
static void loudness(struct tap *t, const snd_pcm_channel_area_t *areas, snd_pcm_uframes_t off,
		     snd_pcm_uframes_t size)
{
	if (!t->readable)
		return;
	for (snd_pcm_uframes_t f = 0; f < size; f++) {
		double ms = 0;
		for (unsigned c = 0; c < t->channels; c++) {
			const snd_pcm_channel_area_t *a = &areas[c];
			const unsigned char *p = (const unsigned char *)a->addr +
				(a->first + (off + f) * (unsigned long long)a->step) / 8;
			double s = sample(p, t->format);
			ms += s * s;
		}
		ms /= t->channels;
		t->ring[t->pos] = (float)ms;
		t->pos = (t->pos + 1) % t->window;
		if (t->fill < t->window)
			t->fill++;
		if (t->pending && t->x.after_n < t->window) {
			t->x.after_sum += ms;
			t->x.after_n++;
		}
	}
}

static void observe(struct tap *t, const snd_pcm_channel_area_t *areas, snd_pcm_uframes_t off,
		    snd_pcm_uframes_t size)
{
	double now = clock_s(CLOCK_MONOTONIC);
	snd_pcm_sframes_t d = 0;
	/* the frames queued before this chunk (the plugin commits it to the slave after this call) */
	int have = snd_pcm_delay(t->ext.pcm, &d) == 0;

	if (t->pending && t->x.resume < 0 && have && snd_pcm_state(t->ext.pcm) == SND_PCM_STATE_RUNNING) {
		/* running again: it started when the frames it has played since were the first */
		double played = ((double)t->seg - d) / t->rate;
		t->x.resume = now - (played > 0 ? played : 0);
		if (t->x.resume < t->x.detect)
			t->x.resume = t->x.detect;
	}
	if (!t->pending && t->seg && t->dry > 0 && now > t->dry + LATE_S && have && d <= 0 &&
	    snd_pcm_state(t->ext.pcm) == SND_PCM_STATE_RUNNING) {
		begin(t, now, "late");	/* ran out without an xrun: this chunk plays at once */
		t->x.resume = now;
	}
	if (have)	/* behind the pointer (d < 0): this chunk is all that is left */
		t->dry = now + (double)((d > 0 ? d : 0) + (snd_pcm_sframes_t)size) / t->rate;
	t->seg += size;
	t->frames += size;
	loudness(t, areas, off, size);
	if (t->pending && t->x.resume >= 0 && (t->x.after_n >= t->window || !t->readable))
		emit(t, now);
}

static snd_pcm_sframes_t tap_transfer(snd_pcm_extplug_t *ext, const snd_pcm_channel_area_t *dst,
				      snd_pcm_uframes_t dst_off, const snd_pcm_channel_area_t *src,
				      snd_pcm_uframes_t src_off, snd_pcm_uframes_t size)
{
	struct tap *t = ext->private_data;

	snd_pcm_areas_copy(dst, dst_off, src, src_off, ext->channels, size, ext->format);
	if (t->fd >= 0 && t->playback && t->rate)
		observe(t, src, src_off, size);
	return size;
}

/* every prepare (and reset): after a recover, the dropout begins */
static int tap_init(snd_pcm_extplug_t *ext)
{
	struct tap *t = ext->private_data;
	double now = clock_s(CLOCK_MONOTONIC);

	if (t->fd < 0 || !t->playback || !t->seg)
		return 0;	/* nothing written since the last prepare: nothing ran dry */
	emit(t, now);		/* one still waiting: it never got its 20 ms after */
	begin(t, now, "prepare");
	t->seg = 0;
	t->dry = 0;
	return 0;
}

static int tap_hw_params(snd_pcm_extplug_t *ext, snd_pcm_hw_params_t *params)
{
	struct tap *t = ext->private_data;
	snd_pcm_uframes_t buffer = 0, period = 0;
	int dir = 0;

	emit(t, clock_s(CLOCK_MONOTONIC));
	t->rate = ext->rate;
	t->channels = ext->channels;
	t->format = ext->format;
	t->readable = readable(ext->format) && ext->channels > 0;
	t->window = ext->rate * WINDOW_MS / 1000;
	if (t->window < 1)
		t->window = 1;
	if (t->window > RING_MAX)
		t->window = RING_MAX;
	t->pos = t->fill = 0;
	t->seg = 0;
	t->dry = 0;
	snd_pcm_hw_params_get_buffer_size(params, &buffer);
	snd_pcm_hw_params_get_period_size(params, &period, &dir);
	if (t->playback)
		say(t, "open\tpid=%d\tcomm=%s\trate=%u\tchannels=%u\tformat=%s\tbuffer=%lu\tperiod=%lu\t"
		    "mono=%.6f\treal=%.6f\n", (int)getpid(), t->comm, t->rate, t->channels,
		    snd_pcm_format_name(t->format), (unsigned long)buffer, (unsigned long)period,
		    clock_s(CLOCK_MONOTONIC), clock_s(CLOCK_REALTIME));
	return 0;
}

static int tap_hw_free(snd_pcm_extplug_t *ext)
{
	emit(ext->private_data, clock_s(CLOCK_MONOTONIC));
	return 0;
}

static int tap_close(snd_pcm_extplug_t *ext)
{
	struct tap *t = ext->private_data;

	emit(t, clock_s(CLOCK_MONOTONIC));
	if (t->fd >= 0) {
		if (t->playback && t->rate)
			say(t, "close\tpid=%d\tcomm=%s\tframes=%llu\txruns=%lu\n", (int)getpid(), t->comm,
			    t->frames, t->n);
		close(t->fd);
	}
	free(t);
	return 0;
}

static const snd_pcm_extplug_callback_t tap_callback = {
	.transfer = tap_transfer,
	.close = tap_close,
	.hw_params = tap_hw_params,
	.hw_free = tap_hw_free,
	.init = tap_init,
};

SND_PCM_PLUGIN_DEFINE_FUNC(ps3xrun)
{
	snd_config_iterator_t i, next;
	snd_config_t *slave = NULL;
	struct tap *t;
	const char *log = getenv("PS3_XRUNLOG");
	int err, fd;

	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0 || !strcmp(id, "comment") || !strcmp(id, "type") ||
		    !strcmp(id, "hint"))
			continue;
		if (strcmp(id, "slave") == 0) {
			slave = n;
			continue;
		}
		SNDERR("ps3xrun: unknown field %s", id);
		return -EINVAL;
	}
	if (!slave) {
		SNDERR("ps3xrun: no slave");
		return -EINVAL;
	}
	t = calloc(1, sizeof(*t));
	if (!t)
		return -ENOMEM;
	t->fd = -1;
	t->playback = stream == SND_PCM_STREAM_PLAYBACK;
	if (t->playback && log && *log) {
		t->fd = open(log, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
		if ((fd = open("/proc/self/comm", O_RDONLY | O_CLOEXEC)) >= 0) {
			ssize_t r = read(fd, t->comm, sizeof(t->comm) - 1);
			t->comm[r > 0 ? r : 0] = 0;
			t->comm[strcspn(t->comm, "\n\t ")] = 0;
			close(fd);
		}
	}
	t->ext.version = SND_PCM_EXTPLUG_VERSION;
	t->ext.name = "ps3-xrunlog: the game's sound dropouts";
	t->ext.callback = &tap_callback;
	t->ext.private_data = t;
	err = snd_pcm_extplug_create(&t->ext, name, root, slave, stream, mode);
	if (err < 0) {
		if (t->fd >= 0)
			close(t->fd);
		free(t);
		return err;
	}
	*pcmp = t->ext.pcm;
	return 0;
}
SND_PCM_PLUGIN_SYMBOL(ps3xrun);

/*
 * The configuration hook (ps3-xrunlog.conf: @hooks.1, after alsa.conf's loader): pcm.default,
 * as the system's and the user's files left it, becomes pcm.ps3xrun_orig, and pcm.default this
 * plugin in front of it. Never fails: on any error the configuration stays as it was.
 */
int ps3_xrunlog_hook(snd_config_t *root, snd_config_t *config, snd_config_t **dst,
		     snd_config_t *private_data)
{
	static const char text[] =
		"pcm.default { type ps3xrun slave.pcm \"ps3xrun_orig\" "
		"hint { show on description \"Default ALSA Output (ps3-xrunlog)\" } }\n";
	snd_config_t *pcm, *def, *n;
	snd_input_t *in;
	int err;

	(void)config;
	(void)private_data;
	*dst = NULL;
	if (snd_config_search(root, "pcm", &pcm) < 0 || snd_config_search(pcm, "default", &def) < 0 ||
	    snd_config_search(pcm, "ps3xrun_orig", &n) >= 0)
		return 0;
	if (snd_config_set_id(def, "ps3xrun_orig") < 0)
		return 0;
	err = snd_input_buffer_open(&in, text, sizeof(text) - 1);
	if (err >= 0) {
		err = snd_config_load(root, in);
		snd_input_close(in);
	}
	if (err < 0) {
		if (snd_config_search(pcm, "default", &n) >= 0)
			snd_config_delete(n);
		snd_config_set_id(def, "default");
	}
	return 0;
}
SND_DLSYM_BUILD_VERSION(ps3_xrunlog_hook, SND_CONFIG_DLSYM_VERSION_HOOK);
