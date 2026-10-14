/* One triangle with per-vertex colours in a 64x64 FBO: centre pixel and image checksum. */
#include "egl-common.h"

static const char *vs =
	"attribute vec2 pos;\n"
	"attribute vec3 attr;\n"
	"varying vec3 color;\n"
	"void main() { color = attr; gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char *fs =
	"precision mediump float;\n"
	"varying vec3 color;\n"
	"void main() { gl_FragColor = vec4(color, 1.0); }\n";

int main(int argc, char **argv)
{
	static const GLfloat pos[] = { -0.8f, -0.8f, 0.8f, -0.8f, 0.0f, 0.8f };
	static const GLfloat col[] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	const int w = 64, h = 64;

	egl_setup(w, h, 0);
	gl_program(vs, fs);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, col);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	print_pixel(readback(w, h, argc > 1 ? argv[1] : NULL), w, w / 2, h / 2);
	return 0;
}
