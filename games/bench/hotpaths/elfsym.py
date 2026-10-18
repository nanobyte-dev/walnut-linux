#!/usr/bin/env python3
"""ELF64 (ppc64 ELFv1 aware) function symbol tables for symbolising perf samples."""
import struct, bisect, os


class Elf:
    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            self.buf = b = f.read()
        assert b[:4] == b'\x7fELF' and b[4] == 2
        self.e = e = '>' if b[5] == 2 else '<'
        (self.type, self.machine, _, self.entry, phoff, shoff, _, _, phentsize, phnum, shentsize, shnum,
         shstrndx) = struct.unpack_from(e + 'HHIQQQIHHHHHH', b, 16)
        self.sections = []
        for i in range(shnum):
            name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from(
                e + 'IIQQQQIIQQ', b, shoff + i * shentsize)
            self.sections.append([name, typ, flags, addr, off, size, link, info, entsize])
        if shnum:
            so = self.sections[shstrndx][4]
            for s in self.sections:
                s[0] = self._str(so, s[0])
        self.phdrs = []
        for i in range(phnum):
            typ, flags, off, vaddr, paddr, filesz, memsz, align = struct.unpack_from(
                e + 'IIQQQQQQ', b, phoff + i * phentsize)
            self.phdrs.append((typ, flags, off, vaddr, filesz, memsz))

    def _str(self, off, i):
        b = self.buf
        j = b.index(b'\0', off + i)
        return b[off + i:j].decode('utf-8', 'replace')

    def section(self, name):
        for i, s in enumerate(self.sections):
            if s[0] == name:
                return i, s
        return None, None

    def symbols(self, tabname='.symtab'):
        i, s = self.section(tabname)
        if s is None or s[1] == 8 or self.sections[s[6]][1] == 8:
            return []
        strs = self.sections[s[6]][4]
        out = []
        for off in range(s[4], s[4] + s[5], 24):
            name, info, other, shndx, value, size = struct.unpack_from(self.e + 'IBBHQQ', self.buf, off)
            if info & 0xf in (2, 10):  # FUNC, GNU_IFUNC
                out.append((self._str(strs, name), shndx, value, size))
        return out

    def read_u64_at_vaddr(self, vaddr):
        for typ, flags, off, va, filesz, memsz in self.phdrs:
            if typ == 1 and va <= vaddr < va + filesz:
                return struct.unpack_from(self.e + 'Q', self.buf, off + vaddr - va)[0]
        return None

    def file_off_to_vaddr(self, off):
        for typ, flags, poff, va, filesz, memsz in self.phdrs:
            if typ == 1 and poff <= off < poff + filesz:
                return va + off - poff
        return off


class SymTab:
    """Sorted function ranges: lookup(addr) -> name or None."""

    def __init__(self, entries):
        entries.sort()
        self.starts = [x[0] for x in entries]
        self.entries = entries

    def lookup(self, addr):
        i = bisect.bisect_right(self.starts, addr) - 1
        if i < 0:
            return None
        st, size, name = self.entries[i]
        if size and addr >= st + size:
            # inside a gap: still attribute to the preceding symbol if close (assembly without sizes)
            return name + '+gap' if addr - st < 0x10000 and size == 0 else None
        return name


def user_symtab(runtime, debug):
    """runtime: the installed .so/exe (has .opd contents); debug: the separate .debug or None."""
    rt = Elf(runtime)
    syms = []
    sources = []
    if debug and os.path.exists(debug):
        sources.append(Elf(debug))
    sources.append(rt)
    seen = set()
    for src in sources:
        for tab in ('.symtab', '.dynsym'):
            opd_idx, opd = src.section('.opd')
            for name, shndx, value, size in src.symbols(tab):
                if value == 0:
                    continue
                if opd is not None and shndx == opd_idx:
                    entry = rt.read_u64_at_vaddr(value)
                    if entry is None:
                        continue
                else:
                    entry = value
                key = (entry, name)
                if key in seen:
                    continue
                seen.add(key)
                syms.append((entry, size, name.lstrip('.') if name.startswith('.') else name))
        if syms:
            break
    # dedupe by start: prefer names without '@'/local aliases with larger size
    best = {}
    for st, size, name in syms:
        if st not in best or (size > best[st][0]) or (size == best[st][0] and len(name) < len(best[st][1])):
            best[st] = (size, name)
    return SymTab([(st, sz, nm) for st, (sz, nm) in best.items()]), rt


def module_symtab(ko):
    el = Elf(ko)
    ti, _ = el.section('.text')
    out = []
    for name, shndx, value, size in el.symbols():
        if shndx == ti and name.startswith('.'):
            out.append((value, size, name[1:]))
    return SymTab(out)


def system_map(path):
    out = []
    with open(path) as f:
        for line in f:
            a, t, n = line.split()[:3]
            if t in 'tTwW' and n.startswith('.'):
                out.append((int(a, 16), 0, n[1:]))
    out.sort()
    # sizes = distance to next
    res = []
    for i, (a, _, n) in enumerate(out):
        nxt = out[i + 1][0] if i + 1 < len(out) else a + 0x1000
        res.append((a, nxt - a, n))
    return SymTab(res)
