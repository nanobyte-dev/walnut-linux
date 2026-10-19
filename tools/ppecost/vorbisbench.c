/* vorbisbench FILE SECONDS [float]: decode like the games do (ov_read to 16-bit big-endian, 4 KB
 * at a time, or ov_read_float), loop the file until SECONDS of audio; prints a checksum */
#include <vorbis/vorbisfile.h>
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
	double secs = atof(argv[2]); int fl = argc > 3 && !strcmp(argv[3], "float");
	long done = 0, want = 0; double sum = 0; char buf[4096]; int bs;
	while (!want || done < want) {
		OggVorbis_File vf;
		if (ov_fopen(argv[1], &vf) < 0) return 1;
		vorbis_info *vi = ov_info(&vf, -1);
		want = (long)(secs * vi->rate);
		for (;;) {
			long n;
			if (fl) { float **pcm; n = ov_read_float(&vf, &pcm, 1024, &bs); if (n > 0) sum += pcm[0][0]; }
			else { n = ov_read(&vf, buf, sizeof buf, 1, 2, 1, &bs); if (n > 0) { sum += ((short *)buf)[0]; n /= 2 * vi->channels; } }
			if (n <= 0) break;
			done += n; if (done >= want) break;
		}
		ov_clear(&vf);
	}
	printf("frames=%ld checksum=%.3f\n", done, sum);
	dumpmaps();
	return 0;
}
