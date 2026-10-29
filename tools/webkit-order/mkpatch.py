#!/usr/bin/python3 -I
# mkpatch.py SRC_TREE OUT.patch TSV...: (SRC_TREE: build/src/webkit2gtk/webkit2gtk-<ver>, with packaging 0001 applied) packaging patch 0002: debian/walnut-text-order.ld (the executed functions of the
# traces, first-seen order, libwebkit2gtk then libjavascriptcoregtk) and the rules lines that use it
import sys,subprocess,os,tempfile,shutil
T,out=sys.argv[1:3]; tsvs=sys.argv[3:]
names={'webkit':[], 'jsc':[]}; seen=set()
for t in tsvs:
    for l in open(t):
        lib,r,c,n=l.rstrip('\n').split('\t')
        if (lib,n) not in seen: seen.add((lib,n)); names[lib].append(n)
d=tempfile.mkdtemp()
os.makedirs(f'{d}/a/debian'); os.makedirs(f'{d}/b/debian')
shutil.copy(f'{T}/debian/rules',f'{d}/a/debian/rules')
r=open(f'{T}/debian/rules').read()
old="""ifeq ($(DEB_HOST_ARCH),ppc64)
	CFLAGS += -mtune=cell
	CXXFLAGS += -mtune=cell
endif
"""
new="""ifeq ($(DEB_HOST_ARCH),ppc64)
	CFLAGS += -mtune=cell
	CXXFLAGS += -mtune=cell
	# The code a page load and a scroll run first in .text, the rest after it: debian/walnut-text-order.ld
	# lists the functions that ran in traces of Badwolf under qemu-user (GameDB and Wikipedia pages,
	# docs/research/BROWSERS.md "Making it fit"), so the pages of the libraries a browsing session reads
	# from disk are about half as many. Relative relocations packed (DT_RELR, glibc 2.36): 13 MB less
	# for the dynamic loader to read at every process start.
	LDFLAGS += -Wl,-z,pack-relative-relocs -Wl,--section-ordering-file,$(CURDIR)/debian/walnut-text-order.ld
endif
"""
assert old in r
open(f'{d}/b/debian/rules','w').write(r.replace(old,new))
with open(f'{d}/b/debian/walnut-text-order.ld','w') as f:
    f.write('/* Walnut: functions that ran while Badwolf loaded and scrolled pages under qemu-user, in the order they\n'
            '   first ran (libwebkit2gtk, then libjavascriptcoregtk); placed first in .text. Regenerated from traces,\n'
            '   see docs/research/BROWSERS.md "Making it fit" in the ps3 project. */\n.text : {\n')
    for lib in ('webkit','jsc'):
        for n in names[lib]: f.write(f'  *(.text.{n})\n')
    f.write('}\n')
hdr="""From 0000000000000000000000000000000000000000 Mon Sep 17 00:00:00 2001
From: Tiberiu Chibici <chibicitiberiu@gmail.com>
Date: Thu, 29 Oct 2026 23:30:00 +0200
Subject: Walnut: the code browsing runs first in .text, packed relative
 relocations

On ppc64 the libraries are linked with a section ordering file (debian/walnut-text-order.ld): the %d functions of libwebkit2gtk and %d of libjavascriptcoregtk that ran while Badwolf loaded and scrolled pages under qemu-user come first in .text, the rest after them. With 128 KB of readahead per page fault, a browsing session read most of the 71 MB of libwebkit2gtk's code before; now it reads about half. And DT_RELR (-z pack-relative-relocs): .rela.dyn 13.2 MB to 1.3 MB. The code itself is the same.

""" % (len(names['webkit']),len(names['jsc']))
p=subprocess.run(['diff','-ruN','a','b'],cwd=d,capture_output=True,text=True).stdout
p=p.replace('diff -ruN a/debian/rules b/debian/rules','diff --git a/debian/rules b/debian/rules')
p=p.replace('diff -ruN a/debian/walnut-text-order.ld b/debian/walnut-text-order.ld','diff --git a/debian/walnut-text-order.ld b/debian/walnut-text-order.ld\nnew file mode 100644')
import re
p=re.sub(r'^(---|\+\+\+) (\S+)\t.*$',r'\1 \2',p,flags=re.M)
p=p.replace('--- a/debian/walnut-text-order.ld','--- /dev/null')
open(out,'w').write(hdr+p)
shutil.rmtree(d)
print(len(names['webkit']),len(names['jsc']),os.path.getsize(out))
