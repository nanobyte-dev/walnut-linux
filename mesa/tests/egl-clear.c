/* Clears a 64x64 FBO to red and reads it back. Exit status 0 if every pixel is 255,0,0,255. */
#include "egl-common.h"

int main(int argc, char **argv)
{
	const int w = 64, h = 64;
	uint8_t *px;
	int bad = 0;

	egl_setup(w, h, 0);
	glClearColor(1, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	px = readback(w, h, argc > 1 ? argv[1] : NULL);
	print_pixel(px, w, w / 2, h / 2);
	for (int i = 0; i < w * h; i++)
		bad += px[4 * i] != 255 || px[4 * i + 1] || px[4 * i + 2] || px[4 * i + 3] != 255;
	printf("%s: %d of %d pixels wrong\n", bad ? "FAIL" : "PASS", bad, w * h);
	return bad != 0;
}
