/* clip-probe-check: the interface between its two halves (the probe's cases, the validator) */
#include <stdbool.h>
#include <stdint.h>
struct vbo { bool vram; uint64_t lo, size; };
void vinit(const struct vbo *bos, int n);
int vcheck(const uint32_t *w, unsigned n, const char **why);
