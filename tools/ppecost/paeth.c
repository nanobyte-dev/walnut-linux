/* paeth MODE BPP: libpng 1.6's png_read_filter_row_paeth_multibyte_pixel (MODE lib) against a
 * version that keeps the left and upper-left pixels in registers and selects without branches
 * (MODE reg), over 64 rows of 4096 bytes x 200 passes; prints a checksum (must match) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8;
static void paeth_lib(unsigned bpp, size_t rowbytes, u8 *row, const u8 *prev_row)
{
	u8 *rp_end = row + bpp;
	while (row < rp_end) { int a = *row + *prev_row++; *row++ = (u8)a; }
	rp_end = rp_end + (rowbytes - bpp);
	while (row < rp_end) {
		int a, b, c, pa, pb, pc, p;
		c = *(prev_row - bpp); a = *(row - bpp); b = *prev_row++;
		p = b - c; pc = a - c;
		pa = p < 0 ? -p : p; pb = pc < 0 ? -pc : pc; pc = (p + pc) < 0 ? -(p + pc) : p + pc;
		if (pb < pa) pa = pb, a = b;
		if (pc < pa) a = c;
		a += *row; *row++ = (u8)a;
	}
}
static inline int pred(int a, int b, int c)
{	/* the same choice as libpng's, without branches */
	int p = b - c, q = a - c;
	int pa = abs(p), pb = abs(q), pc = abs(p + q);
	int useb = pb < pa; int m = useb ? pb : pa;
	int r = useb ? b : a;
	return pc < m ? c : r;
}
static void paeth_reg(unsigned bpp, size_t rowbytes, u8 *row, const u8 *prev)
{
	if (bpp == 4) {
		int a0 = 0, a1 = 0, a2 = 0, a3 = 0, c0 = 0, c1 = 0, c2 = 0, c3 = 0;
		for (size_t i = 0; i < rowbytes; i += 4) {
			int b0 = prev[i], b1 = prev[i + 1], b2 = prev[i + 2], b3 = prev[i + 3];
			a0 = (u8)(row[i] + pred(a0, b0, c0)); a1 = (u8)(row[i + 1] + pred(a1, b1, c1));
			a2 = (u8)(row[i + 2] + pred(a2, b2, c2)); a3 = (u8)(row[i + 3] + pred(a3, b3, c3));
			row[i] = a0; row[i + 1] = a1; row[i + 2] = a2; row[i + 3] = a3;
			c0 = b0; c1 = b1; c2 = b2; c3 = b3;
		}
	} else {	/* bpp 3 */
		int a0 = 0, a1 = 0, a2 = 0, c0 = 0, c1 = 0, c2 = 0;
		for (size_t i = 0; i < rowbytes; i += 3) {
			int b0 = prev[i], b1 = prev[i + 1], b2 = prev[i + 2];
			a0 = (u8)(row[i] + pred(a0, b0, c0)); a1 = (u8)(row[i + 1] + pred(a1, b1, c1));
			a2 = (u8)(row[i + 2] + pred(a2, b2, c2));
			row[i] = a0; row[i + 1] = a1; row[i + 2] = a2;
			c0 = b0; c1 = b1; c2 = b2;
		}
	}
}
int main(int argc, char **argv)
{
	int reg = !strcmp(argv[1], "reg"); unsigned bpp = atoi(argv[2]);
	size_t rb = 4092 / bpp * bpp; int rows = 64;
	u8 *img = malloc(rb * rows), *src = malloc(rb * rows), *zero = calloc(rb, 1);
	unsigned r = 7; for (size_t k = 0; k < rb * rows; k++) { r = r * 1103515245u + 12345u; src[k] = r >> 24; }
	unsigned long sum = 0;
	for (int pass = 0; pass < 200; pass++) {
		memcpy(img, src, rb * rows);
		for (int y = 0; y < rows; y++) {
			const u8 *prev = y ? img + (y - 1) * rb : zero;
			if (reg) paeth_reg(bpp, rb, img + y * rb, prev); else paeth_lib(bpp, rb, img + y * rb, prev);
		}
		sum += img[rb * rows - 1] + img[rb * rows / 2];
	}
	for (size_t k = 0; k < rb * rows; k++) sum += img[k] * (k & 7);
	printf("%s bpp=%u checksum=%lu bytes=%zu\n", argv[1], bpp, sum, rb * rows * 200);
	return 0;
}
