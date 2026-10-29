#!/usr/bin/python3 -I
# functab.py <stripped lib> <debug file> > tsv: code_addr size bind name (ELFv1: FUNC symbols point into .opd)
import struct,subprocess,sys
L,D=sys.argv[1:3]
def sections(f):
    out=subprocess.run(['powerpc64-linux-gnu-readelf','-SW',f],capture_output=True,text=True).stdout
    s={}
    for l in out.splitlines():
        l=l.replace('[ ','[')
        p=l.split()
        if len(p)>6 and p[0].startswith('[') and p[0]!='[Nr]':
            s[p[1]]=(int(p[3],16),int(p[4],16),int(p[5],16))
    return s
S=sections(L)
oa,oo,osz=S['.opd']; ta,to,tsz=S['.text']
data=open(L,'rb').read()
opd=data[oo:oo+osz]
out=subprocess.run(['powerpc64-linux-gnu-readelf','-sW','--wide',D],capture_output=True,text=True).stdout
seen=set()
for l in out.splitlines():
    p=l.split()
    if len(p)<8 or p[3]!='FUNC': continue
    v=int(p[1],16); size=int(p[2],0); name=p[7]
    if oa<=v<oa+osz:
        code=struct.unpack('>Q',opd[v-oa:v-oa+8])[0]
    elif ta<=v<ta+tsz: code=v
    else: continue
    if (code,name) in seen: continue
    seen.add((code,name))
    print(f"{code:x}\t{size}\t{p[4]}\t{name}")
