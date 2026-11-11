/* vseal.ld: the program at LS 0, everything below VSEAL_LS_PROG_MAX (0x10000, vseal_abi.h) */
ENTRY(_start)
SECTIONS
{
	. = 0;
	.text : { *(.text.start) *(.text .text.*) }
	.rodata : { *(.rodata .rodata.*) }
	.data : { *(.data .data.*) }
	.bss : { *(.bss .bss.*) *(COMMON) }
	_end = .;
	ASSERT(_end <= 0x10000, "vseal is over VSEAL_LS_PROG_MAX")
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
