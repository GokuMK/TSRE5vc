import os, sys, time
def evict(p):
    fd=os.open(p,os.O_RDONLY); os.fsync(fd) if False else None; os.posix_fadvise(fd,0,0,os.POSIX_FADV_DONTNEED); os.close(fd)
def readall(p):
    t=time.time(); n=0
    with open(p,'rb',buffering=0) as f:
        while True:
            b=f.read(8<<20)
            if not b: break
            n+=len(b)
    return n, time.time()-t
for p in sys.argv[1:]:
    evict(p); n,c=readall(p); n,w=readall(p)
    print(f"{os.path.basename(p)} {n/1e6:.0f}MB cold {c:.2f}s ({n/1e6/c:.0f} MB/s) warm {w:.2f}s ({n/1e6/w:.0f} MB/s)")
