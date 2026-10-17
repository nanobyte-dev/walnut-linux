import sys
names=[l.strip() for l in open('cnt/names.txt') if l.strip()]
o=open('cnt/stubs.S','w')
o.write('.text\n')
for i,n in enumerate(names):
    o.write(f""".globl {n}
.type {n},@function
{n}:
 mov (%rsp),%r11
 movabs ${i<<48},%r10
 or %r10,%r11
 push %rax
 mov glc_idx(%rip),%rax
 lea glc_log(%rip),%r10
 mov %r11,(%r10,%rax,8)
 inc %rax
 and $0xffffff,%rax
 mov %rax,glc_idx(%rip)
 pop %rax
 mov glc_real+{i*8}(%rip),%r10
 jmp *%r10
""")
o.write(f""".globl glc_nstubs
glc_nstubs: .long {len(names)}
.data
.globl glc_idx
glc_idx: .quad 0
.bss
.globl glc_real
.comm glc_real,{len(names)*8},16
.comm glc_log,{(1<<24)*8},16
.section .rodata
.globl glc_names
glc_names:
""")
for n in names: o.write(f' .quad .Ln{names.index(n)}\n') if False else None
o.close()
# names table in C
c=open('cnt/names.h','w')
c.write(''.join('extern void %s(void);\n'%n for n in names))
c.write('static const char *const glc_names[]={\n'+',\n'.join('"%s"'%n for n in names)+'};\n')
c.write('static void *const glc_stubs[]={\n'+',\n'.join('(void*)%s'%n for n in names)+'};\n')
c.write(''.join('') )
