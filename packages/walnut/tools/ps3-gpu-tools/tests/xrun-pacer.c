/*
 * xrun-pacer.so: a pretend sound card for testing ps3-xrunlog.so on the PC (bench/tests/selftest.py):
 * an ALSA ioplug that plays nothing but consumes frames in real time from its start, on
 * CLOCK_MONOTONIC, and goes into XRUN as soon as its position passes the frames written, like a
 * card that ran dry. Never built into the package; no sound reaches the PC's speakers.
 *
 *   pcm_type.xrunpacer { lib "<dir>/xrun-pacer.so" }
 *   pcm.default { type xrunpacer }
 */
#define _GNU_SOURCE
#ifndef PIC
#define PIC
#endif
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>

struct pacer {
	snd_pcm_ioplug_t io;
	double t0;
	int running;
};

static double mono(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int p_start(snd_pcm_ioplug_t *io)
{
	struct pacer *p = io->private_data;
	p->t0 = mono();
	p->running = 1;
	return 0;
}

static int p_stop(snd_pcm_ioplug_t *io)
{
	((struct pacer *)io->private_data)->running = 0;
	return 0;
}

static int p_prepare(snd_pcm_ioplug_t *io)
{
	((struct pacer *)io->private_data)->running = 0;
	return 0;
}

static snd_pcm_sframes_t p_pointer(snd_pcm_ioplug_t *io)
{
	struct pacer *p = io->private_data;
	snd_pcm_uframes_t played;

	if (!p->running)
		return 0;
	played = (snd_pcm_uframes_t)((mono() - p->t0) * io->rate);
	if (played > io->appl_ptr)
		return -EPIPE;	/* past the last frame written: an underrun */
	return played % io->buffer_size;
}

static snd_pcm_sframes_t p_transfer(snd_pcm_ioplug_t *io, const snd_pcm_channel_area_t *areas,
				    snd_pcm_uframes_t offset, snd_pcm_uframes_t size)
{
	(void)io; (void)areas; (void)offset;
	return size;
}

static int p_close(snd_pcm_ioplug_t *io)
{
	struct pacer *p = io->private_data;
	close(io->poll_fd);
	free(p);
	return 0;
}

static const snd_pcm_ioplug_callback_t p_callback = {
	.start = p_start,
	.stop = p_stop,
	.prepare = p_prepare,
	.pointer = p_pointer,
	.transfer = p_transfer,
	.close = p_close,
};

SND_PCM_PLUGIN_DEFINE_FUNC(xrunpacer)
{
	static const unsigned int access[] = { SND_PCM_ACCESS_RW_INTERLEAVED, SND_PCM_ACCESS_MMAP_INTERLEAVED };
	static const unsigned int formats[] = { SND_PCM_FORMAT_S16_LE, SND_PCM_FORMAT_S16_BE, SND_PCM_FORMAT_FLOAT_LE };
	struct pacer *p;
	int err;

	(void)root; (void)conf;
	if (stream != SND_PCM_STREAM_PLAYBACK)
		return -EINVAL;
	p = calloc(1, sizeof(*p));
	if (!p)
		return -ENOMEM;
	p->io.version = SND_PCM_IOPLUG_VERSION;
	p->io.name = "xrun pacer";
	p->io.callback = &p_callback;
	p->io.private_data = p;
	p->io.mmap_rw = 0;
	p->io.poll_fd = eventfd(1, EFD_CLOEXEC | EFD_NONBLOCK);	/* always ready: clients poll avail */
	p->io.poll_events = POLLOUT;
	err = snd_pcm_ioplug_create(&p->io, name, stream, mode);
	if (err < 0) {
		close(p->io.poll_fd);
		free(p);
		return err;
	}
	snd_pcm_ioplug_set_param_list(&p->io, SND_PCM_IOPLUG_HW_ACCESS, 2, access);
	snd_pcm_ioplug_set_param_list(&p->io, SND_PCM_IOPLUG_HW_FORMAT, 3, formats);
	snd_pcm_ioplug_set_param_minmax(&p->io, SND_PCM_IOPLUG_HW_CHANNELS, 1, 2);
	snd_pcm_ioplug_set_param_minmax(&p->io, SND_PCM_IOPLUG_HW_RATE, 8000, 192000);
	snd_pcm_ioplug_set_param_minmax(&p->io, SND_PCM_IOPLUG_HW_PERIOD_BYTES, 256, 65536);
	snd_pcm_ioplug_set_param_minmax(&p->io, SND_PCM_IOPLUG_HW_PERIODS, 2, 64);
	*pcmp = p->io.pcm;
	return 0;
}
SND_PCM_PLUGIN_SYMBOL(xrunpacer);
