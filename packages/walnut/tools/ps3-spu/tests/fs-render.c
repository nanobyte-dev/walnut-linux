/* fs-render SF2 MIDI OUT.f32 [interp] [fx] [seed]: renders a MIDI file with libfluidsynth to raw
 * float32 stereo at 44.1 kHz, the way a game does: fluid_synth_write_float in chunks of 1 to
 * 3000 frames (seeded), so the mixer renders 1 to 47 blocks at a time and block edges fall
 * anywhere. interp: fluid_interp (0 none, 1 linear, 4 cubic: the default, 5-7 sinc); fx 1:
 * reverb and chorus on (dsda-doom's defaults are off). Gain 0.5 and polyphony 256 as
 * dsda-doom sets them. For tests/fluidsynth-compare.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <fluidsynth.h>

int main(int argc, char **argv)
{
	fluid_settings_t *set;
	fluid_synth_t *syn;
	fluid_player_t *pl;
	static float buf[2 * 3000];
	unsigned seed, n;
	long frames = 0;
	int interp, fx;
	FILE *out;

	if (argc < 4) {
		fprintf(stderr, "usage: %s SF2 MIDI OUT.f32 [interp] [fx] [seed]\n", argv[0]);
		return 2;
	}
	interp = argc > 4 ? atoi(argv[4]) : FLUID_INTERP_DEFAULT;
	fx = argc > 5 ? atoi(argv[5]) : 0;
	seed = argc > 6 ? strtoul(argv[6], NULL, 0) : 1;
	set = new_fluid_settings();
	fluid_settings_setnum(set, "synth.sample-rate", 44100);
	fluid_settings_setint(set, "synth.reverb.active", fx);
	fluid_settings_setint(set, "synth.chorus.active", fx);
	fluid_settings_setnum(set, "synth.gain", 0.5);
	fluid_settings_setint(set, "synth.polyphony", 256);
	fluid_settings_setint(set, "synth.threadsafe-api", 0);
	fluid_settings_setint(set, "synth.cpu-cores", 1);
	fluid_settings_setstr(set, "player.timing-source", "sample");
	syn = new_fluid_synth(set);
	if (!syn || fluid_synth_sfload(syn, argv[1], 1) == FLUID_FAILED) {
		fprintf(stderr, "%s: can't load %s\n", argv[0], argv[1]);
		return 1;
	}
	fluid_synth_set_interp_method(syn, -1, interp);
	pl = new_fluid_player(syn);
	if (!pl || fluid_player_add(pl, argv[2]) != FLUID_OK || fluid_player_play(pl) != FLUID_OK) {
		fprintf(stderr, "%s: can't play %s\n", argv[0], argv[2]);
		return 1;
	}
	out = fopen(argv[3], "wb");
	if (!out)
		return 1;
	while (fluid_player_get_status(pl) == FLUID_PLAYER_PLAYING || frames < 44100) {
		seed = seed * 1103515245 + 12345;
		n = 1 + (seed >> 8) % 3000;
		if (fluid_synth_write_float(syn, n, buf, 0, 2, buf, 1, 2) != FLUID_OK)
			return 1;
		if (fwrite(buf, sizeof(float), 2 * n, out) != 2 * n)
			return 1;
		frames += n;
		if (frames > 44100L * 600)
			break;
	}
	/* two seconds of tails */
	for (n = 0; n < 2 * 44100 / 1000; n++) {
		fluid_synth_write_float(syn, 1000, buf, 0, 2, buf, 1, 2);
		fwrite(buf, sizeof(float), 2000, out);
	}
	fclose(out);
	delete_fluid_player(pl);
	delete_fluid_synth(syn);
	delete_fluid_settings(set);
	fprintf(stderr, "%s: %ld frames\n", argv[2], frames);
	return 0;
}
