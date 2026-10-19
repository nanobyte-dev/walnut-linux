/* alsaconv SECONDS float|s16: the ALSA path OpenAL's backend writes to on the PS3, plug in front of
 * an S16 slave, here a null PCM: float frames make plug convert them (the lfloat plugin), S16 frames
 * pass straight. 512-frame mmap periods like OpenAL's ALSA backend. */
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dumpmaps(void)
{
	const char *o = getenv("DUMPMAPS"); if (!o) return;
	FILE *i = fopen("/proc/self/maps", "r"), *w = fopen(o, "w"); int c;
	if (i && w) while ((c = fgetc(i)) != EOF) fputc(c, w);
	if (i) fclose(i); if (w) fclose(w);
}

static const char conf[] =
	"pcm.bench { type plug slave { pcm { type null } format S16 rate 48000 channels 2 } }\n";

int main(int argc, char **argv)
{
	double secs = argc > 1 ? atof(argv[1]) : 10;
	int isf = !(argc > 2 && !strcmp(argv[2], "s16"));
	snd_config_t *top; snd_input_t *in; snd_pcm_t *pcm;
	snd_config_top(&top);
	snd_input_buffer_open(&in, conf, strlen(conf));
	if (snd_config_load(top, in) < 0) return 1;
	snd_input_close(in);
	{ int e = snd_pcm_open_lconf(&pcm, "bench", SND_PCM_STREAM_PLAYBACK, 0, top); if (e < 0) { fprintf(stderr, "open: %s\n", snd_strerror(e)); return 1; } }
	if (snd_pcm_set_params(pcm, isf ? SND_PCM_FORMAT_FLOAT : SND_PCM_FORMAT_S16, SND_PCM_ACCESS_MMAP_INTERLEAVED,
			2, 48000, 0, 32000) < 0) { fprintf(stderr, "params\n"); return 1; }
	snd_pcm_uframes_t per = 512;
	long total = (long)(secs * 48000), done = 0;
	unsigned r = 1;
	while (done < total) {
		const snd_pcm_channel_area_t *a; snd_pcm_uframes_t off, n = per;
		snd_pcm_sframes_t av = snd_pcm_avail_update(pcm);
		if (av < 0) { snd_pcm_prepare(pcm); continue; }
		if (snd_pcm_mmap_begin(pcm, &a, &off, &n) < 0) return 1;
		char *p = (char *)a[0].addr + off * (a[0].step / 8);
		for (snd_pcm_uframes_t k = 0; k < n * 2; k++) {
			r = r * 1103515245u + 12345u;
			if (isf) ((float *)p)[k] = ((int)(r >> 16) - 32768) / 40000.0f;
			else ((short *)p)[k] = (short)(((int)(r >> 16) - 32768) * 4 / 5);
		}
		snd_pcm_mmap_commit(pcm, off, n);
		if (snd_pcm_state(pcm) == SND_PCM_STATE_PREPARED) snd_pcm_start(pcm);
		done += n;
	}
	printf("frames=%ld format=%s\n", done, isf ? "float" : "s16");
	snd_pcm_close(pcm);
	dumpmaps();
	return 0;
}
