/* crt0.S: vseal's start at LS 0 (spe.c sets NPC = 0). Stack below 0x3fff0 (vseal_abi.h); main never
 * returns, a return would stop the SPU with code 0x3fff (the kernel takes any stop as an error). */
	.section .text.start,"ax",@progbits
	.global _start
_start:
	ila	$1, 0x3fff0
	il	$2, 0
	stqd	$2, 0($1)
	brsl	$0, main
	stop	0x3fff
	.size	_start, . - _start
