// Cold-friendly block table read: pread each BlobHeader only, no readahead.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
static uint64_t vi(const uint8_t*& p){uint64_t r=0;int s=0;for(;;){uint8_t c=*p++;r|=uint64_t(c&127)<<s;s+=7;if(c<128)return r;}}
int main(int, char** argv){
    auto t=std::chrono::steady_clock::now();
    int fd=open(argv[1],O_RDONLY); posix_fadvise(fd,0,0,POSIX_FADV_RANDOM);
    off_t size=lseek(fd,0,SEEK_END); off_t off=0; size_t n=0, idx=0; uint8_t buf[512];
    while(off<size){
        pread(fd,buf,sizeof buf,off); uint32_t hl=__builtin_bswap32(*(uint32_t*)buf);
        const uint8_t*p=buf+4,*e=p+hl; uint64_t ds=0;
        while(p<e){uint64_t k=vi(p); if((k&7)==0){uint64_t v=vi(p); if((k>>3)==3)ds=v;} else {uint64_t l=vi(p); if((k>>3)==2&&l==17)idx++; p+=l;}}
        off+=4+hl+ds; n++;
    }
    printf("pread scan: %zu blocks (%zu with bbox) in %.3fs\n",n,idx,std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count());
}
