// glbatch-test.sh: X-Moto's GLBatch layer on llvmpipe (EGL surfaceless, an FBO): a random scene of DrawLib
// primitives, state changes, game draws and static ranges, N frames; prints a checksum of each
// frame's pixels and any GL error. Run with XMOTO_GL_STREAM=0 and =1 and compare.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <math.h>
#include "include/xm_OpenGL.h"

static unsigned rng = 12345;
static unsigned rnd(unsigned n) { rng = rng * 1103515245u + 12345u; return (rng >> 8) % n; }
static float rf() { return rnd(100000) / 100000.0f; }

int main(int argc, char **argv) {
  int frames = argc > 1 ? atoi(argv[1]) : 20;
  const int W = 640, H = 480;
  EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
  if (!eglInitialize(dpy, NULL, NULL)) { fprintf(stderr, "egl init\n"); return 1; }
  eglBindAPI(EGL_OPENGL_API);
  EGLint ca[] = { EGL_CONTEXT_MAJOR_VERSION, 2, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE };
  EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
  if (!ctx || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { fprintf(stderr, "ctx\n"); return 1; }
  if (!gladLoadGLLoader((GLADloadproc)eglGetProcAddress)) { fprintf(stderr, "glad\n"); return 1; }
  fprintf(stderr, "GL %s %s\n", glad_glGetString(GL_RENDERER), glad_glGetString(GL_VERSION));
  GLuint fb, rb;
  glad_glGenFramebuffersEXT(1, &fb); glad_glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);
  glad_glGenRenderbuffersEXT(1, &rb); glad_glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, rb);
  glad_glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, W, H);
  glad_glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, rb);
  xmgl::init();
  // a texture, a static buffer (x,y,u,v), client arrays of the game
  GLuint tex[2];
  glGenTextures(2, tex);
  for (int t = 0; t < 2; t++) {
    unsigned char px[16 * 16 * 4];
    for (int i = 0; i < 16 * 16 * 4; i++) px[i] = (unsigned char)(i * (t + 3) * 7);
    glBindTexture(GL_TEXTURE_2D, tex[t]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
  }
  std::vector<float> st;
  for (int i = 0; i < 3000; i++) { st.push_back(rf() * W); st.push_back(rf() * H); st.push_back(rf()); st.push_back(rf()); }
  GLuint sbuf;
  glGenBuffersARB(1, &sbuf);
  glBindBufferARB(GL_ARRAY_BUFFER_ARB, sbuf);
  glBufferDataARB(GL_ARRAY_BUFFER_ARB, st.size() * 4, st.data(), GL_STATIC_DRAW_ARB);
  glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
  float gameArr[64];
  int errors = 0;
  for (int f = 0; f < frames; f++) {
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, W, 0, H, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    int nprim = 2000 + rnd(4000);
    for (int p = 0; p < nprim; p++) {
      unsigned k = rnd(100);
      if (k < 3) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
      else if (k < 6) glDisable(GL_BLEND);
      else if (k < 9) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex[rnd(2)]); }
      else if (k < 12) glDisable(GL_TEXTURE_2D);
      else if (k < 13) { glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.5f); }
      else if (k < 14) glDisable(GL_ALPHA_TEST);
      else if (k < 16) { // the game's own client-array draw
        for (int i = 0; i < 8; i++) { gameArr[2 * i] = rf() * W; gameArr[2 * i + 1] = rf() * H; }
        glColor4ub(rnd(256), rnd(256), rnd(256), 255);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, gameArr);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 8);
        glDisableClientState(GL_VERTEX_ARRAY);
      } else if (k < 18) { // a static range
        glColor4ub(rnd(256), rnd(256), rnd(256), 255);
        xmgl::drawStatic(sbuf, NULL, GL_TRIANGLES, 3 * rnd(900), 3 * (1 + rnd(30)));
      } else if (k < 19) { // the game's glBegin/glEnd
        glColor4ub(rnd(256), 0, rnd(256), 255);
        glBegin(GL_TRIANGLES);
        for (int i = 0; i < 3; i++) glad_glVertex2f(rf() * W, rf() * H);
        glEnd();
      } else { // DrawLib primitives
        xmgl::Vtx v[64];
        int n; GLenum mode;
        unsigned m = rnd(10);
        float cx = rf() * W, cy = rf() * H, r = 2 + rf() * 20;
        unsigned char c[4] = { (unsigned char)rnd(256), (unsigned char)rnd(256), (unsigned char)rnd(256), (unsigned char)(128 + rnd(128)) };
        if (m < 6) { mode = GL_POLYGON; n = 8 + rnd(10); }
        else if (m < 8) { mode = GL_QUADS; n = 4 * (1 + rnd(8)); }
        else if (m < 9) { mode = GL_TRIANGLES; n = 3 * (1 + rnd(6)); }
        else { mode = rnd(2) ? GL_LINE_LOOP : GL_LINE_STRIP; n = 2 + rnd(10); }
        for (int i = 0; i < n; i++) {
          float a = 6.2831853f * i / n;
          v[i].x = mode == GL_POLYGON ? cx + r * sinf(a) : cx + rf() * 40 - 20;
          v[i].y = mode == GL_POLYGON ? cy + r * cosf(a) : cy + rf() * 40 - 20;
          v[i].u = rf(); v[i].v = rf();
          memcpy(v[i].c, c, 4);
        }
        xmgl::primBegin();
        xmgl::primEnd(mode, v, n);
      }
    }
    xmgl::frameEnd();
    GLenum e;
    while ((e = glad_glGetError()) != GL_NO_ERROR) { fprintf(stderr, "frame %d: GL error 0x%x\n", f, e); errors++; }
    std::vector<unsigned char> px(W * H * 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned char b : px) h = (h ^ b) * 1099511628211ULL;
    printf("frame %d prims %d hash %016llx\n", f, nprim, h);
  }
  printf("errors %d\n", errors);
  return errors != 0;
}
