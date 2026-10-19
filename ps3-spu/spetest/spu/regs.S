/* reg_roundtrip: load all 128 registers from regsrc, store them to regdst. Every register is
 * written and read through the load/store path, so a stuck or shorted register bit shows as a
 * difference. $0 (link) .. $127 are saved in regsave first and restored last, then bi $0. */
	.text
	.altmacro
	.global reg_roundtrip
	.type reg_roundtrip, @function
	.macro ST n, base
	stqa $\n, \base+16*\n
	.endm
	.macro LD n, base
	lqa $\n, \base+16*\n
	.endm
reg_roundtrip:
	.set i, 0
	.rept 128
	ST %i, regsave
	.set i, i+1
	.endr
	.set i, 0
	.rept 128
	LD %i, regsrc
	.set i, i+1
	.endr
	.set i, 0
	.rept 128
	ST %i, regdst
	.set i, i+1
	.endr
	.set i, 0
	.rept 128
	LD %i, regsave
	.set i, i+1
	.endr
	bi $0
	.size reg_roundtrip, .-reg_roundtrip
