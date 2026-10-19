/* fsbench SF2 MIDI SECONDS [s16|float]: renders MIDI like OpenTTD (s16, defaults: reverb and chorus
 * on) or dsda-doom (float, gain 0.5), 512-frame chunks at 44.1 kHz, output discarded. Prints the
 * main thread's CPU seconds (RUSAGE_THREAD), so voice DSP done on other threads (libspeq) is out.
 * Settings come from the library's defaults plus FLUIDSYNTH_SETTINGS (patch 0002). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <fluidsynth.h>

int main(int argc, char **argv)
{
	double secs = argc > 3 ? atof(argv[3]) : 60;
	int s16 = !(argc > 4 && !strcmp(argv[4], "float"));
	fluid_settings_t *set = new_fluid_settings();
	fluid_settings_setnum(set, "synth.sample-rate", 44100);
	fluid_settings_setint(set, "synth.lock-memory", 0);
	fluid_settings_setstr(set, "player.timing-source", "sample");
	if (!s16) fluid_settings_setnum(set, "synth.gain", 0.5);
	fluid_synth_t *syn = new_fluid_synth(set);
	if (!syn || fluid_synth_sfload(syn, argv[1], 1) == FLUID_FAILED) return 1;
	fluid_player_t *pl = new_fluid_player(syn);
	fluid_player_add(pl, argv[2]);
	fluid_player_set_loop(pl, -1);
	fluid_player_play(pl);
	static short sb[2 * 512]; static float fb[2 * 512];
	long frames = (long)(secs * 44100); double sum = 0;
	for (long f = 0; f < frames; f += 512) {
		if (s16) { fluid_synth_write_s16(syn, 512, sb, 0, 2, sb, 1, 2); sum += sb[100]; }
		else { fluid_synth_write_float(syn, 512, fb, 0, 2, fb, 1, 2); sum += fb[100]; }
	}
	struct rusage ru; getrusage(RUSAGE_THREAD, &ru);
	printf("cpu=%.3f s audio=%.0f s sum=%g\n", ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 +
		ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6, secs, sum);
	return 0;
}
