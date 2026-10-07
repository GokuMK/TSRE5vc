// Inflate-only baseline: own blob reader, zlib vs libdeflate, N threads.
#include <cstdio>
#include <cstdint>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <zlib.h>
#include <libdeflate.h>
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
static uint64_t vi(const uint8_t*&p){uint64_t r=0;int s=0;for(;;){uint8_t c=*p++;r|=uint64_t(c&127)<<s;s+=7;if(c<128)return r;}}
struct B{const uint8_t* z; size_t zl; size_t raw;};
int main(int argc,char**argv){
    int fd=open(argv[1],O_RDONLY); struct stat st; fstat(fd,&st);
    const uint8_t* m=(const uint8_t*)mmap(0,st.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    bool useLd=argv[2][0]=='l'; bool useMz=argv[2][0]=='m'; int th=atoi(argv[3]);
    auto t0=std::chrono::steady_clock::now();
    std::vector<B> bl; size_t off=0;
    while(off<(size_t)st.st_size){
        uint32_t hl=__builtin_bswap32(*(uint32_t*)(m+off)); off+=4;
        const uint8_t*p=m+off,*e=p+hl; uint64_t ds=0;
        while(p<e){uint64_t k=vi(p); if((k&7)==0){uint64_t v=vi(p); if((k>>3)==3)ds=v;} else {uint64_t n=vi(p); p+=n;}}
        off+=hl; const uint8_t*q=m+off,*qe=q+ds; B b{0,0,0};
        while(q<qe){uint64_t k=vi(q); if((k&7)==0){uint64_t v=vi(q); if((k>>3)==2)b.raw=v;} else {uint64_t n=vi(q); if((k>>3)==3){b.z=q;b.zl=n;} q+=n;}}
        bl.push_back(b); off+=ds;
    }
    std::atomic<size_t> idx{0}, tot{0};
    std::vector<std::thread> ts;
    for(int t=0;t<th;t++) ts.emplace_back([&]{
        std::vector<uint8_t> out; libdeflate_decompressor* d=libdeflate_alloc_decompressor();
        for(size_t i;(i=idx++)<bl.size();){ auto&b=bl[i]; out.resize(b.raw);
            if(useMz){ mz_ulong a=b.raw; mz_uncompress(out.data(),&a,b.z,b.zl);} else if(useLd){ size_t a; libdeflate_zlib_decompress(d,b.z,b.zl,out.data(),b.raw,&a);}
            else { uLongf a=b.raw; uncompress(out.data(),&a,b.z,b.zl);} tot+=b.raw; }
        libdeflate_free_decompressor(d);});
    for(auto&t:ts)t.join();
    double s=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
    printf("%s threads=%d blobs=%zu raw=%.0fMB %.2fs (%.0f MB/s raw)\n",useMz?"miniz":useLd?"libdeflate":"zlib",th,bl.size(),tot/1e6,s,tot/1e6/s);
}
