/* Renders a triangle covering a W x H framebuffer (default 1427 x 1): tiny render-target
 * heights/widths, as glamor uses for 1-pixel strips (graph exception 0x106, docs/RSX.md).
 * Usage: egl-thin [W H] */
#include "egl-common.h"

static const char *vs =
	"attribute vec2 pos;\n"
	"void main() { gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char *fs =
	"precision mediump float;\n"
	"void main() { gl_FragColor = vec4(1.0, 0.5, 0.25, 1.0); }\n";

int main(int argc, char **argv)
{
	static const GLfloat pos[] = { -1, -1, 3, -1, -1, 3 };
	int w = argc > 2 ? atoi(argv[1]) : 1427, h = argc > 2 ? atoi(argv[2]) : 1;
	uint8_t *px;

	egl_setup(w, h, 0);
	gl_program(vs, fs);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos);
	glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	px = readback(w, h, NULL);
	print_pixel(px, w, w / 2, h / 2);
	return 0;
}
