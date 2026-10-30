// Host test of patches/0002 (the hardware cursor's alpha on big-endian). It does what Source/hwcursor.cpp
// does with a cursor at a scaled-up screen (the 8-bit sprite with its colour key, converted to 32 bits,
// DevilutionX's own BilinearScale32, SDL_CreateColorCursor), then reads the cursor back from the X server
// (XFixesGetCursorImage) and counts the pixels inside the sprite that lost their alpha. Built against
// the real Source/utils/sdl_bilinear_scale.cpp; run under Xvfb, on amd64 and on ppc64 (qemu-user):
//   g++ -O2 -I$SRC/Source hwcursor-alpha-test.cpp $SRC/Source/utils/sdl_bilinear_scale.cpp \
//       $(sdl2-config --cflags --libs) -lX11 -lXfixes -o t && ./t argb8888 && ./t bgra32
// argb8888 is 1.5.5 as shipped, bgra32 the patch. Expected: both "0 bad" on amd64; on ppc64 argb8888 loses
// alpha wherever the sprite's blue is low (BilinearScale32 reads byte 3 as alpha: blue on big-endian),
// bgra32 "0 bad". Exit status 1 when a pixel is bad.
#include <cstdio>
#include <cstring>

#include <SDL.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>

#include "utils/sdl_bilinear_scale.hpp"

int main(int argc, char **argv)
{
	const bool patched = argc > 1 && std::strcmp(argv[1], "bgra32") == 0;
	const Uint32 fmt = patched ? SDL_PIXELFORMAT_BGRA32 : SDL_PIXELFORMAT_ARGB8888;
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 2;
	}
	SDL_Window *win = SDL_CreateWindow("hwcursor", 0, 0, 320, 240, SDL_WINDOW_SHOWN);
	if (win == nullptr) {
		std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		return 2;
	}

	// the sprite: 32x32, index 1 transparent (the colour key, as SetHardwareCursorFromSprite fills it), a
	// 4-pixel transparent border, the inside in colours like the hand's: browns with no blue, greys, a blue
	const int W = 32, H = 32, B = 4;
	SDL_Surface *src = SDL_CreateRGBSurfaceWithFormat(0, W, H, 8, SDL_PIXELFORMAT_INDEX8);
	const SDL_Color cols[] = { { 0, 0, 0, 255 }, { 0, 0, 0, 255 }, { 139, 69, 0, 255 }, { 200, 160, 0, 255 },
		{ 90, 90, 90, 255 }, { 220, 200, 180, 255 }, { 40, 20, 4, 255 }, { 30, 60, 250, 255 } };
	SDL_SetPaletteColors(src->format->palette, cols, 0, 8);
	SDL_FillRect(src, nullptr, 1);
	SDL_SetColorKey(src, SDL_TRUE, 1);
	for (int y = B; y < H - B; ++y)
		for (int x = B; x < W - B; ++x)
			static_cast<Uint8 *>(src->pixels)[y * src->pitch + x] = 2 + (x / 3 + y / 5) % 6;

	// hwcursor.cpp's scaled path (the PS3's 1920x1080 against 854x480: 2.25)
	const int SW = W * 9 / 4, SH = H * 9 / 4;
	SDL_Surface *conv = SDL_ConvertSurfaceFormat(src, fmt, 0);
	SDL_Surface *scaled = SDL_CreateRGBSurfaceWithFormat(0, SW, SH, 32, fmt);
	devilution::BilinearScale32(conv, scaled);
	SDL_Cursor *cur = SDL_CreateColorCursor(scaled, 0, 0);
	if (cur == nullptr) {
		std::fprintf(stderr, "SDL_CreateColorCursor: %s\n", SDL_GetError());
		return 2;
	}
	SDL_SetCursor(cur);
	SDL_ShowCursor(SDL_ENABLE);
	SDL_WarpMouseInWindow(win, 100, 100);
	for (int i = 0; i < 20; ++i) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) { }
		SDL_Delay(20);
	}

	// the inside of the sprite, scaled, one source pixel away from the border: opaque, alpha 255
	const int x0 = (B + 1) * 9 / 4, x1 = (W - B - 1) * 9 / 4;
	int badSurface = 0, badX = 0, n = 0;
	for (int y = x0; y < x1; ++y)
		for (int x = x0; x < x1; ++x) {
			Uint8 r, g, b, a;
			SDL_GetRGBA(*reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(scaled->pixels) + y * scaled->pitch + x * 4),
			    scaled->format, &r, &g, &b, &a);
			badSurface += a != 255;
			++n;
		}

	Display *dpy = XOpenDisplay(nullptr);
	int ev, er;
	if (dpy == nullptr || !XFixesQueryExtension(dpy, &ev, &er)) {
		std::fprintf(stderr, "no X display with XFixes\n");
		return 2;
	}
	XFixesCursorImage *img = XFixesGetCursorImage(dpy);
	if (img == nullptr || img->width != SW || img->height != SH) {
		std::fprintf(stderr, "X cursor %dx%d, not %dx%d (not ours)\n", img ? img->width : 0, img ? img->height : 0, SW, SH);
		return 2;
	}
	for (int y = x0; y < x1; ++y)
		for (int x = x0; x < x1; ++x)
			badX += ((img->pixels[y * img->width + x] >> 24) & 0xff) != 255;
	const unsigned long mid = img->pixels[(SH / 2) * img->width + SW / 2];
	std::printf("%s (%s-endian): %d of %d inner pixels not opaque in the scaled surface, %d in the X cursor "
	            "(%dx%d; its middle pixel 0x%08lx)\n",
	    patched ? "bgra32" : "argb8888", SDL_BYTEORDER == SDL_BIG_ENDIAN ? "big" : "little", badSurface, n, badX,
	    img->width, img->height, mid & 0xffffffffUL);
	XFree(img);
	XCloseDisplay(dpy);
	SDL_FreeCursor(cur);
	SDL_Quit();
	return (badSurface || badX) ? 1 : 0;
}
