/* oalbench SECONDS NSOURCES SRCRATE float|int16: OpenAL Soft's mixer through a loopback device
 * (ALC_SOFT_loopback), rendered as fast as it goes: N looping mono 16-bit sources at SRCRATE Hz,
 * spread around the listener, into 48 kHz stereo of the given type in 512-frame periods like the
 * ALSA backend's. Config (resampler, dither, limiter) through ALSOFT_CONF. Prints a checksum. */
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <math.h>
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

int main(int argc, char **argv)
{
	double secs = argc > 1 ? atof(argv[1]) : 10;
	int ns = argc > 2 ? atoi(argv[2]) : 16, srate = argc > 3 ? atoi(argv[3]) : 44100;
	int is16 = argc > 4 && !strcmp(argv[4], "int16");
	LPALCLOOPBACKOPENDEVICESOFT lopen = (LPALCLOOPBACKOPENDEVICESOFT)alcGetProcAddress(NULL, "alcLoopbackOpenDeviceSOFT");
	LPALCRENDERSAMPLESSOFT render = (LPALCRENDERSAMPLESSOFT)alcGetProcAddress(NULL, "alcRenderSamplesSOFT");
	if (!lopen || !render) { fprintf(stderr, "no ALC_SOFT_loopback\n"); return 1; }
	ALCdevice *dev = lopen(NULL);
	ALCint attr[] = { ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT, ALC_FORMAT_TYPE_SOFT,
		is16 ? ALC_SHORT_SOFT : ALC_FLOAT_SOFT, ALC_FREQUENCY, 48000, 0 };
	ALCcontext *ctx = alcCreateContext(dev, attr);
	if (!ctx) { fprintf(stderr, "no context\n"); return 1; }
	alcMakeContextCurrent(ctx);
	int len = srate;	/* a second of sound per buffer, different pitches per source */
	short *pcm = malloc(len * 2);
	ALuint *buf = calloc(ns, sizeof *buf), *src = calloc(ns, sizeof *src);
	alGenBuffers(ns, buf); alGenSources(ns, src);
	unsigned r = 1;
	for (int i = 0; i < ns; i++) {
		double f = 110.0 * (1 + i);
		for (int k = 0; k < len; k++) {
			r = r * 1103515245u + 12345u;
			pcm[k] = (short)(12000 * sin(2 * M_PI * f * k / srate) + (int)((r >> 16) & 2047) - 1024);
		}
		if (getenv("STEREO") && i % 2) {	/* every other source a stereo buffer (music) */
			short *st = malloc(len * 4);
			for (int k = 0; k < len; k++) { st[2 * k] = pcm[k]; st[2 * k + 1] = (short)(pcm[(k * 7) % len] / 2); }
			alBufferData(buf[i], AL_FORMAT_STEREO16, st, len * 4, srate);
			free(st);
		} else
			alBufferData(buf[i], AL_FORMAT_MONO16, pcm, len * 2, srate);
		alSourcei(src[i], AL_BUFFER, buf[i]);
		alSourcei(src[i], AL_LOOPING, AL_TRUE);
		alSource3f(src[i], AL_POSITION, (float)cos(i * 0.7) * 3, 0, (float)sin(i * 0.7) * 3);
		alSourcef(src[i], AL_GAIN, 0.4f);
		alSourcePlay(src[i]);
	}
	int frames = 512, periods = (int)(secs * 48000 / frames);
	void *out = malloc(frames * 2 * 4);
	double sum = 0; unsigned long long h = 1469598103934665603ULL;
	for (int p = 0; p < periods; p++) {
		if (p % 47 == 0)	/* games move their sources: parameter updates now and then */
			for (int i = 0; i < ns; i++)
				alSource3f(src[i], AL_POSITION, (float)cos(i * 0.7 + p * 0.01) * 3, 0, (float)sin(i * 0.7 + p * 0.01) * 3);
		render(dev, out, frames);
		{ const unsigned char *b = out; for (int k = 0; k < frames * 2 * (is16 ? 2 : 4); k++) h = (h ^ b[k]) * 1099511628211ULL; }
		if (is16) { short *s = out; for (int k = 0; k < frames * 2; k += 61) sum += s[k]; }
		else { float *s = out; for (int k = 0; k < frames * 2; k += 61) sum += s[k] * 32768.0; }
	}
	printf("periods=%d sources=%d rate=%d type=%s checksum=%.0f hash=%016llx\n", periods, ns, srate, is16 ? "int16" : "float", sum, h);
	alcMakeContextCurrent(NULL); alcDestroyContext(ctx); alcCloseDevice(dev);
	dumpmaps();
	return 0;
}
