/* Userspace stand-ins for the kernel bits shadow.c uses (equivalence test only) */
#ifndef SHIM_H
#define SHIM_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stddef.h>
#define BUILD_BUG_ON(c) _Static_assert(!(c), #c)

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;

#undef __always_inline
#define __always_inline inline __attribute__((always_inline))
#define noinline __attribute__((noinline))
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define READ_ONCE(x) (*(volatile typeof(x) *)&(x))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define min_t(t, a, b) ((t)(a) < (t)(b) ? (t)(a) : (t)(b))

#define BITS_PER_LONG		(8 * (int)sizeof(unsigned long))
#define BIT_WORD(n)		((n) / BITS_PER_LONG)
#define BITS_TO_LONGS(n)	(((n) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define DECLARE_BITMAP(name, bits) unsigned long name[BITS_TO_LONGS(bits)]

/* Reference semantics, deliberately bit by bit */
static inline void __set_bit(unsigned long nr, unsigned long *map)
{
	map[nr / BITS_PER_LONG] |= 1UL << (nr % BITS_PER_LONG);
}
static inline bool test_bit(unsigned long nr, const unsigned long *map)
{
	return (map[nr / BITS_PER_LONG] >> (nr % BITS_PER_LONG)) & 1;
}
static inline void bitmap_set(unsigned long *map, unsigned int start, unsigned int len)
{
	while (len--)
		__set_bit(start++, map);
}
static inline void bitmap_zero(unsigned long *map, unsigned int nbits)
{
	memset(map, 0, BITS_TO_LONGS(nbits) * sizeof(long));
}
static inline unsigned int bitmap_weight(const unsigned long *map, unsigned int nbits)
{
	unsigned int i, w = 0;

	for (i = 0; i < nbits; i++)
		w += test_bit(i, map);
	return w;
}
/* As lib/find_bit.c (out of line, a word at a time), so instruction counts are the kernel's */
static noinline unsigned long find_next_bit(const unsigned long *map,
					    unsigned long size, unsigned long off)
{
	unsigned long idx, val;

	if (off >= size)
		return size;
	idx = off / BITS_PER_LONG;
	val = map[idx] & (~0UL << (off % BITS_PER_LONG));
	while (!val) {
		idx++;
		if (idx * BITS_PER_LONG >= size)
			return size;
		val = map[idx];
	}
	off = idx * BITS_PER_LONG + __builtin_ctzl(val);
	return off < size ? off : size;
}
#define for_each_set_bit(bit, map, size) \
	for ((bit) = find_next_bit(map, size, 0); (bit) < (size); \
	     (bit) = find_next_bit(map, size, (bit) + 1))
#define __ffs(w) ((unsigned long)__builtin_ctzl(w))

#define GFP_KERNEL 0
static inline void *kvzalloc(size_t n, int gfp) { (void)gfp; return calloc(1, n); }
static inline void kvfree(void *p) { free(p); }

struct seq_file { int dummy; };
#define seq_printf(m, ...) do { (void)(m); } while (0)

/* the test reaches the param through test_param_<name> */
#define module_param(name, type, perm) type *const test_param_##name = &name
#define MODULE_PARM_DESC(name, desc)
#endif
