/* es2gears-like: three lit gears, rotated over 100 frames in a 256x256 FBO with depth;
 * checksum of the last frame.
 */
#include <math.h>
#include "egl-common.h"

static const char *vs =
	"attribute vec3 pos;\n"
	"attribute vec3 attr;\n"
	"uniform mat4 mvp, model;\n"
	"uniform vec3 color;\n"
	"varying vec4 c;\n"
	"void main() {\n"
	"  vec3 n = normalize((model * vec4(attr, 0.0)).xyz);\n"
	"  float d = max(dot(n, normalize(vec3(5.0, 5.0, 10.0))), 0.0);\n"
	"  c = vec4(color * (0.2 + 0.8 * d), 1.0);\n"
	"  gl_Position = mvp * vec4(pos, 1.0);\n"
	"}\n";
static const char *fs =
	"precision mediump float;\n"
	"varying vec4 c;\n"
	"void main() { gl_FragColor = c; }\n";

typedef GLfloat mat4[16];	/* column-major */

static void mul(mat4 r, const mat4 a, const mat4 b)
{
	mat4 t;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			t[j * 4 + i] = a[i] * b[j * 4] + a[4 + i] * b[j * 4 + 1] +
				       a[8 + i] * b[j * 4 + 2] + a[12 + i] * b[j * 4 + 3];
	memcpy(r, t, sizeof(t));
}

static void rotate(mat4 m, float deg, float x, float y, float z)
{
	float s = sinf(deg * (float)M_PI / 180), c = cosf(deg * (float)M_PI / 180);
	mat4 r = { x * x * (1 - c) + c, y * x * (1 - c) + z * s, x * z * (1 - c) - y * s, 0,
		   x * y * (1 - c) - z * s, y * y * (1 - c) + c, y * z * (1 - c) + x * s, 0,
		   x * z * (1 - c) + y * s, y * z * (1 - c) - x * s, z * z * (1 - c) + c, 0,
		   0, 0, 0, 1 };
	mul(m, m, r);
}

static void translate(mat4 m, float x, float y, float z)
{
	mat4 t = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1 };
	mul(m, m, t);
}

/* Gear geometry as a triangle list of (position, normal) pairs */
struct gear { GLfloat *v; int n; };

static void quad(struct gear *g, const float p[4][3], float nx, float ny, float nz)
{
	static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
	for (int i = 0; i < 6; i++) {
		GLfloat *v = g->v + 6 * g->n++;
		memcpy(v, p[idx[i]], 3 * sizeof(float));
		v[3] = nx, v[4] = ny, v[5] = nz;
	}
}

static struct gear make_gear(float r_in, float r_out, float width, int teeth, float depth)
{
	struct gear g = { malloc(teeth * 9 * 6 * 6 * sizeof(GLfloat)), 0 };
	float r0 = r_in, r1 = r_out - depth / 2, r2 = r_out + depth / 2;
	float da = 2 * (float)M_PI / teeth / 4, z = width / 2;

	for (int i = 0; i < teeth; i++) {
		float a = i * 2 * (float)M_PI / teeth;
		/* tooth outline, then the inner ring */
		float pr[5] = { r1, r2, r2, r1, r1 };
		float pa[5] = { a, a + da, a + 2 * da, a + 3 * da, a + 4 * da };
#define P(r, t, zz) { (r) * cosf(t), (r) * sinf(t), (zz) }
		for (int s = -1; s <= 1; s += 2) {	/* front (+z) and back (-z) faces */
			float ring[4][3] = { P(r0, a, s * z), P(r1, a, s * z),
					     P(r1, a + 4 * da, s * z), P(r0, a + 4 * da, s * z) };
			float tooth[4][3] = { P(r1, pa[0], s * z), P(r2, pa[1], s * z),
					      P(r2, pa[2], s * z), P(r1, pa[3], s * z) };
			quad(&g, ring, 0, 0, s);
			quad(&g, tooth, 0, 0, s);
		}
		for (int k = 0; k < 4; k++) {	/* outward faces along the outline */
			float x0 = pr[k] * cosf(pa[k]), y0 = pr[k] * sinf(pa[k]);
			float x1 = pr[k + 1] * cosf(pa[k + 1]), y1 = pr[k + 1] * sinf(pa[k + 1]);
			float len = hypotf(x1 - x0, y1 - y0);
			float side[4][3] = { { x0, y0, z }, { x0, y0, -z }, { x1, y1, -z }, { x1, y1, z } };
			quad(&g, side, (y1 - y0) / len, -(x1 - x0) / len, 0);
		}
		float inner[4][3] = { P(r0, a, -z), P(r0, a, z), P(r0, a + 4 * da, z), P(r0, a + 4 * da, -z) };
		quad(&g, inner, -cosf(a + 2 * da), -sinf(a + 2 * da), 0);
#undef P
	}
	return g;
}

int main(int argc, char **argv)
{
	const int w = 256, h = 256, frames = 100;
	static const struct { float r_in, r_out, width; int teeth; float x, y, speed, phase, rgb[3]; } spec[3] = {
		{ 1.0f, 4.0f, 1.0f, 20, -3.0f, -2.0f, 1, 0, { 0.8f, 0.1f, 0.0f } },
		{ 0.5f, 2.0f, 2.0f, 10, 3.1f, -2.0f, -2, -9, { 0.0f, 0.8f, 0.2f } },
		{ 1.3f, 2.0f, 0.5f, 10, -3.1f, 4.2f, -2, -25, { 0.2f, 0.2f, 1.0f } },
	};
	struct gear g[3];
	GLint u_mvp, u_model, u_color;
	GLuint prog, vbo[3];

	egl_setup(w, h, 1);
	prog = gl_program(vs, fs);
	u_mvp = glGetUniformLocation(prog, "mvp");
	u_model = glGetUniformLocation(prog, "model");
	u_color = glGetUniformLocation(prog, "color");
	glGenBuffers(3, vbo);
	for (int i = 0; i < 3; i++) {
		g[i] = make_gear(spec[i].r_in, spec[i].r_out, spec[i].width, spec[i].teeth, 0.7f);
		glBindBuffer(GL_ARRAY_BUFFER, vbo[i]);
		glBufferData(GL_ARRAY_BUFFER, g[i].n * 6 * sizeof(GLfloat), g[i].v, GL_STATIC_DRAW);
	}
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnable(GL_DEPTH_TEST);
	glClearColor(0, 0, 0, 1);

	/* frustum(-1, 1, -1, 1, 5, 60) */
	const mat4 proj = { 5, 0, 0, 0, 0, 5, 0, 0, 0, 0, -65.0f / 55, -1, 0, 0, -600.0f / 55, 0 };
	for (int f = 0; f < frames; f++) {
		float angle = 2.0f * f;
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		for (int i = 0; i < 3; i++) {
			mat4 view = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }, model, mvp;
			translate(view, 0, 0, -40);
			rotate(view, 20, 1, 0, 0);
			rotate(view, 30, 0, 1, 0);
			memcpy(model, view, sizeof(model));
			translate(model, spec[i].x, spec[i].y, 0);
			rotate(model, spec[i].speed * angle + spec[i].phase, 0, 0, 1);
			mul(mvp, proj, model);
			glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
			glUniformMatrix4fv(u_model, 1, GL_FALSE, model);
			glUniform3fv(u_color, 1, spec[i].rgb);
			glBindBuffer(GL_ARRAY_BUFFER, vbo[i]);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void *)0);
			glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat),
					      (void *)(3 * sizeof(GLfloat)));
			glDrawArrays(GL_TRIANGLES, 0, g[i].n);
		}
		glFlush();
	}
	printf("frames: %d\n", frames);
	print_pixel(readback(w, h, argc > 1 ? argv[1] : NULL), w, w / 2, h / 2);
	return 0;
}
