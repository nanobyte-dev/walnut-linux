/* A quad textured with a 4x4 RGBA checkerboard (nearest filtering) in a 64x64 FBO. */
#include "egl-common.h"

static const char *vs =
	"attribute vec2 pos;\n"
	"attribute vec2 attr;\n"
	"varying vec2 uv;\n"
	"void main() { uv = attr; gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char *fs =
	"precision mediump float;\n"
	"uniform sampler2D tex;\n"
	"varying vec2 uv;\n"
	"void main() { gl_FragColor = texture2D(tex, uv); }\n";

int main(int argc, char **argv)
{
	static const GLfloat pos[] = { -0.75f, -0.75f, 0.75f, -0.75f, -0.75f, 0.75f, 0.75f, 0.75f };
	static const GLfloat uv[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
	const int w = 64, h = 64;
	uint8_t texels[4 * 4 * 4];
	uint8_t *px;
	GLuint tex;

	/* distinct colours per texel, so swapped bytes or a wrong layout show */
	for (int i = 0; i < 16; i++) {
		texels[4 * i + 0] = (i & 1) ? 255 : 32;
		texels[4 * i + 1] = (uint8_t)(i * 16);
		texels[4 * i + 2] = ((i >> 2) & 1) ? 200 : 0;
		texels[4 * i + 3] = 255;
	}
	egl_setup(w, h, 0);
	gl_program(vs, fs);
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	px = readback(w, h, argc > 1 ? argv[1] : NULL);
	/* texel centres: (row 0, col 0) and (row 3, col 3) */
	print_pixel(px, w, 14, 14);
	print_pixel(px, w, 50, 50);
	return 0;
}
