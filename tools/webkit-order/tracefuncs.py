#!/usr/bin/python3 -I
# tracefuncs.py OUT.tsv FUNCTAB_WEBKIT FUNCTAB_JSC LOG...: functions executed in libwebkit2gtk/libjavascriptcoregtk
# from qemu in_asm+strace logs (one per process). Output: lib, first-seen rank, TB count, mangled name.
# How the ordering file in packages/patched/desktop/webkit2gtk/packaging/0002 is made (docs/research/BROWSERS.md
# "Making it fit"): in a ppc64 chroot under qemu-user (binfmt) with the build's debs and Badwolf, start
#   QEMU_LOG=in_asm,strace QEMU_LOG_FILENAME=/out/NAME-q-%d.log badwolf URL
# (each process logs every translated block once, and its openat/mmap calls give the libraries' load addresses),
# load and scroll the page; then functab.py on the build's libraries and their -dbgsym .debug files, this script
# over all the logs, and mkpatch.py over the resulting lists. Function names, not addresses, carry over to the
# next build of the same source; only functions a newer source adds or renames fall back to the default place.
import sys,re,bisect,collections
out,fw,fj=sys.argv[1:4]; logs=sys.argv[4:]
def load(f):
    rows=sorted((int(a,16),int(s),n) for a,s,b,n in (l.rstrip('\n').split('\t') for l in open(f)))
    return [r[0] for r in rows],rows
FT={'webkit':load(fw),'jsc':load(fj)}
LIBN={'webkit':'libwebkit2gtk-4.1.so.0','jsc':'libjavascriptcoregtk-4.1.so.0'}
first={}; count=collections.Counter(); seq=0
ro=re.compile(r'openat\(.*?"([^"]+)".*\) = (\d+)$')
rm=re.compile(r'mmap\((?:NULL|0x[0-9a-f]+),(\d+),PROT_(?:EXEC\|PROT_READ|READ\|PROT_EXEC),[A-Z_|]+,(\d+),0\) = (0x[0-9a-f]+)')
for lg in logs:
    fds={}; bases=[]   # (start,end,lib)
    intb=False
    for line in open(lg,errors='replace'):
        if line.startswith('IN:'): intb=True; continue
        if intb and line.startswith('0x'):
            a=int(line.split(':')[0],16); intb=False
            for s,e,lib in bases:
                if s<=a<e:
                    off=a-s; keys,rows=FT[lib]; i=bisect.bisect_right(keys,off)-1
                    if i>=0 and off<rows[i][0]+max(rows[i][1],4):
                        k=(lib,rows[i][2]); count[k]+=1
                        if k not in first: first[k]=seq; seq+=1
                    break
            continue
        if 'openat(' in line:
            m=ro.search(line.strip())
            if m: fds[m.group(2)]=m.group(1)
        elif 'mmap(' in line and 'PROT_EXEC' in line:
            m=rm.search(line)
            if m:
                path=fds.get(m.group(2),'')
                for lib,n in LIBN.items():
                    if n in path: bases.append((int(m.group(3),16),int(m.group(3),16)+int(m.group(1)),lib))
with open(out,'w') as f:
    for k,r in sorted(first.items(),key=lambda x:x[1]): f.write(f"{k[0]}\t{r}\t{count[k]}\t{k[1]}\n")
print(len(first),'functions', collections.Counter(k[0] for k in first))
