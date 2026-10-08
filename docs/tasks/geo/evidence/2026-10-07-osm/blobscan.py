import sys, struct, zlib, time, os
def varint(b, i):
    r=s=0
    while True:
        c=b[i]; i+=1; r|=(c&0x7f)<<s; s+=7
        if c<0x80: return r,i
def fields(b):
    i=0
    while i<len(b):
        k,i=varint(b,i); f,t=k>>3,k&7
        if t==0: v,i=varint(b,i)
        elif t==2: n,i=varint(b,i); v=b[i:i+n]; i+=n
        elif t==1: v=b[i:i+8]; i+=8
        elif t==5: v=b[i:i+4]; i+=4
        yield f,t,v
def zz(v): return (v>>1)^-(v&1)
for path in sys.argv[1:]:
    t0=time.time(); n=0; raw=0; comp=0; hdr=None; kinds={}
    with open(path,'rb') as fh:
        while True:
            l=fh.read(4)
            if not l: break
            hl=struct.unpack('>I',l)[0]; bh=dict((f,v) for f,t,v in fields(fh.read(hl)))
            data=fh.read(bh[3]); n+=1
            blob={}
            for f,t,v in fields(data): blob[f]=v
            comp+=len(data); raw+=blob.get(2,0)
            if bh[1]==b'OSMHeader':
                hb=zlib.decompress(blob[3]); hdr={}
                for f,t,v in fields(hb):
                    if f==1: hdr['bbox']=[zz(x)/1e9 for _,_,x in fields(v)]
                    elif f in (4,5): hdr.setdefault('feat',[]).append(v.decode())
                    elif f==16: hdr['prog']=v.decode()
                    elif f==32: hdr['ts']=v
            for k in blob:
                if k!=2: kinds[k]=kinds.get(k,0)+1
    print(os.path.basename(path), f"blobs={n} comp={comp/1e6:.0f}MB raw={raw/1e6:.0f}MB ratio={raw/comp:.2f} kinds={kinds} scan={time.time()-t0:.2f}s", hdr)
