/* Host test of patches/0003 (gcc -O2 -o /tmp/t blit-swizzle-test.c && /tmp/t: "0 differ"); the loops copied from SDL_blit.h/SDL_blit_A.c. */
/* blit-test.c: SDL3's Blit8888to8888PixelAlphaSwizzle as shipped (the ALPHA_BLEND_SWIZZLE_8888 macro)
 * against the libsdl3 patch's loop (shifts in locals, alpha 0 and 255 without the blend), on random
 * pixels and every pair of 8888 layouts the patch can meet; both FACTOR_BLEND_8888 variants. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef struct { Uint32 Rshift, Gshift, Bshift, Ashift, Amask; } Fmt;

#define BLEND64(src, dst, factor) do { Uint64 s64 = src; s64 = (s64 | (s64 << 24)) & 0x00FF00FF00FF00FFull; \
    Uint64 d64 = dst; d64 = (d64 | (d64 << 24)) & 0x00FF00FF00FF00FFull; \
    d64 = ((s64 - d64) * factor) + (d64 << 8) - d64; d64 += 0x0001000100010001ull; \
    d64 += (d64 >> 8) & 0x00FF00FF00FF00FFull; d64 &= 0xFF00FF00FF00FF00ull; dst = (Uint32)((d64 >> 8) | (d64 >> 32)); } while (0)
#define BLEND32(src, dst, factor) do { Uint32 s02 = src & 0x00FF00FF, d02 = dst & 0x00FF00FF; \
    Uint32 s13 = (src >> 8) & 0x00FF00FF, d13 = (dst >> 8) & 0x00FF00FF; \
    Uint32 r02 = ((s02 - d02) * factor) + (d02 << 8) - d02; r02 += 0x00010001; r02 += (r02 >> 8) & 0x00FF00FF; r02 = (r02 >> 8) & 0x00FF00FF; \
    Uint32 r13 = ((s13 - d13) * factor) + (d13 << 8) - d13; r13 += 0x00010001; r13 += (r13 >> 8) & 0x00FF00FF; r13 &= 0xFF00FF00; \
    dst = r02 | r13; } while (0)

static Uint32 old_px(int v64, Uint32 src, Uint32 dst, const Fmt *sf, const Fmt *df, bool fill, Uint32 dAm)
{
    Uint32 srcA = (src >> sf->Ashift) & 0xFF;
    Uint32 tmp = (((src >> sf->Rshift) & 0xFF) << df->Rshift) | (((src >> sf->Gshift) & 0xFF) << df->Gshift) |
                 (((src >> sf->Bshift) & 0xFF) << df->Bshift) | df->Amask;
    if (v64) BLEND64(tmp, dst, srcA); else BLEND32(tmp, dst, srcA);
    if (fill) dst |= dAm;
    return dst;
}

static Uint32 new_px(int v64, Uint32 src32, Uint32 dst, const Fmt *sf, const Fmt *df, bool fill, Uint32 dstAmask)
{
    const Uint32 sA = sf->Ashift, sR = sf->Rshift, sG = sf->Gshift, sB = sf->Bshift;
    const Uint32 dR = df->Rshift, dG = df->Gshift, dB = df->Bshift, dAmask = df->Amask;
    const bool same_rgb = (sR == dR && sG == dG && sB == dB);
    const Uint32 rgbmask = (0xFFu << sR) | (0xFFu << sG) | (0xFFu << sB);
    const Uint32 srcA = (src32 >> sA) & 0xFF;
    if (srcA == 0)
        return fill ? dst | dstAmask : dst;
    Uint32 tmp = same_rgb ? (src32 & rgbmask) | dAmask
                          : (((src32 >> sR) & 0xFF) << dR) | (((src32 >> sG) & 0xFF) << dG) | (((src32 >> sB) & 0xFF) << dB) | dAmask;
    Uint32 d;
    if (srcA == 0xFF) d = tmp; else { d = dst; if (v64) BLEND64(tmp, d, srcA); else BLEND32(tmp, d, srcA); }
    if (fill) d |= dstAmask;
    return d;
}

int main(void)
{
    /* the four byte orders with alpha (src) and with or without alpha (dst) */
    const Fmt src[] = { {16, 8, 0, 24, 0xFF000000}, {0, 8, 16, 24, 0xFF000000}, {24, 16, 8, 0, 0xFF}, {8, 16, 24, 0, 0xFF} };
    const Fmt dst[] = { {16, 8, 0, 24, 0xFF000000}, {16, 8, 0, 24, 0}, {0, 8, 16, 24, 0}, {24, 16, 8, 0, 0}, {8, 16, 24, 0, 0},
                        {24, 16, 8, 0, 0xFF}, {8, 16, 24, 0, 0xFF} };
    unsigned long n = 0, bad = 0;
    srand(1);
    for (int v64 = 0; v64 < 2; v64++)
        for (unsigned i = 0; i < sizeof src / sizeof *src; i++)
            for (unsigned j = 0; j < sizeof dst / sizeof *dst; j++) {
                const Fmt *df = &dst[j];
                bool fill = !df->Amask;
                Uint32 xmask = fill ? ~((0xFFu << df->Rshift) | (0xFFu << df->Gshift) | (0xFFu << df->Bshift)) : df->Amask;
                for (int k = 0; k < 200000; k++) {
                    Uint32 s = (Uint32)rand() << 16 ^ (Uint32)rand(), d = (Uint32)rand() << 16 ^ (Uint32)rand();
                    int pick = k % 4;   /* alpha 0, 255 and in between, often */
                    if (pick == 0) s &= ~(0xFFu << src[i].Ashift);
                    if (pick == 1) s |= 0xFFu << src[i].Ashift;
                    n++;
                    if (old_px(v64, s, d, &src[i], df, fill, xmask) != new_px(v64, s, d, &src[i], df, fill, xmask)) {
                        if (bad++ < 5) printf("diff v64=%d src=%u dst=%u s=%08x d=%08x\n", v64, i, j, s, d);
                    }
                }
            }
    printf("%lu pixels, %lu differ\n", n, bad);
    return bad != 0;
}
