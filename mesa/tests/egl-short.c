/* glamor-style fills in a 64x64 FBO: pixel-space GL_SHORT positions from a VBO, scaled by a
 * uniform matrix, colour from a uniform. Rectangles are asymmetric so swapped x/y shows.
 *   red   : GL_SHORT, VBO, x 4..20, y 8..40
 *   green : GL_FLOAT, VBO, x 30..60, y 4..12
 *   blue  : GL_SHORT, client array, x 30..60, y 40..44
 * EGL_SHORT_SRC=float|short and/or vbo|client: force every rectangle's attribute source.
 * EGL_SHORT_PRIM=tris: two triangles instead of a strip.
 * EGL_SHORT_FS=lit: literal fragment colour (magenta); =var: that plus a varying it reads; EGL_SHORT_VS=const: matrix in the shader.
 */
#include "egl-common.h"

static const char *vs =
	"attribute vec2 pos;\n"
	"uniform vec4 v_matrix;\n"
	"void main() { gl_Position = vec4(pos * v_matrix.xy + v_matrix.zw, 0.0, 1.0); }\n";
static const char *vs_const =
	"attribute vec2 pos;\n"
	"uniform vec4 v_matrix;\n"
	"varying vec2 vp;\n"
	"void main() { vp = pos; gl_Position = vec4(pos / 32.0 - 1.0, 0.0, 1.0); }\n";
static const char *fs =
	"precision mediump float;\n"
	"uniform vec4 fg;\n"
	"void main() { gl_FragColor = fg; }\n";
static const char *fs_lit =
	"precision mediump float;\n"
	"uniform vec4 fg;\n"
	"void main() { gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0); }\n";
static const char *fs_var =
	"precision mediump float;\n"
	"varying vec2 vp;\n"
	"void main() { gl_FragColor = vec4(1.0, vp.x / 64.0, 1.0, 1.0); }\n";

static void rect(GLuint prog, GLenum type, int vbo, int x0, int y0, int x1, int y1,
		 float r, float g, float b)
{
	const int tris = getenv("EGL_SHORT_PRIM") && !strcmp(getenv("EGL_SHORT_PRIM"), "tris");
	GLshort s[12] = { x0, y0, x1, y0, x0, y1, x1, y1, x0, y1, x1, y0 };
	GLfloat f[12] = { x0, y0, x1, y0, x0, y1, x1, y1, x0, y1, x1, y0 };
	const char *src = getenv("EGL_SHORT_SRC");

	if (src && strstr(src, "float"))
		type = GL_FLOAT;
	if (src && strstr(src, "short"))
		type = GL_SHORT;
	if (src && strstr(src, "vbo"))
		vbo = 1;
	if (src && strstr(src, "client"))
		vbo = 0;
	const void *data = type == GL_SHORT ? (const void *)s : (const void *)f;
	GLsizeiptr size = type == GL_SHORT ? sizeof(s) : sizeof(f);
	GLuint buf;

	glUniform4f(glGetUniformLocation(prog, "fg"), r, g, b, 1);
	if (vbo) {
		glGenBuffers(1, &buf);
		glBindBuffer(GL_ARRAY_BUFFER, buf);
		glBufferData(GL_ARRAY_BUFFER, size, data, GL_STREAM_DRAW);
		glVertexAttribPointer(0, 2, type, GL_FALSE, 0, NULL);
	} else {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glVertexAttribPointer(0, 2, type, GL_FALSE, 0, data);
	}
	glEnableVertexAttribArray(0);
	if (tris)
		glDrawArrays(GL_TRIANGLES, 0, 6);
	else
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

int main(int argc, char **argv)
{
	const int w = 64, h = 64;
	GLuint prog;
	uint8_t *px;

	egl_setup(w, h, 0);
	const char *m = getenv("EGL_SHORT_VS"), *c = getenv("EGL_SHORT_FS");
	prog = gl_program(m && !strcmp(m, "const") ? vs_const : vs, c && !strcmp(c, "lit") ? fs_lit :
			   c && !strcmp(c, "var") ? fs_var : fs);
	glUniform4f(glGetUniformLocation(prog, "v_matrix"), 2.0f / w, 2.0f / h, -1, -1);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	rect(prog, GL_SHORT, 1, 4, 8, 20, 40, 1, 0, 0);
	rect(prog, GL_FLOAT, 1, 30, 4, 60, 12, 0, 1, 0);
	rect(prog, GL_SHORT, 0, 30, 40, 60, 44, 0, 0, 1);
	px = readback(w, h, argc > 1 ? argv[1] : NULL);
	print_pixel(px, w, 12, 24);	/* red */
	print_pixel(px, w, 24, 12);	/* black (red with x/y swapped would be here) */
	print_pixel(px, w, 45, 8);	/* green */
	print_pixel(px, w, 45, 42);	/* blue */
	return 0;
}
