// Full decode benchmark: libosmium PBF reader, counts entities.
#include <osmium/io/pbf_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sys/resource.h>
struct H : osmium::handler::Handler {
    uint64_t n=0,w=0,r=0,tags=0,refs=0,mems=0; int64_t maxn=0,maxw=0;
    void node(const osmium::Node& x){ n++; tags+=x.tags().size(); if(x.id()>maxn)maxn=x.id(); }
    void way(const osmium::Way& x){ w++; tags+=x.tags().size(); refs+=x.nodes().size(); if(x.id()>maxw)maxw=x.id(); }
    void relation(const osmium::Relation& x){ r++; tags+=x.tags().size(); mems+=x.members().size(); }
};
int main(int argc,char**argv){
    auto bits=osmium::osm_entity_bits::all;
    if(argc>2){ bits=osmium::osm_entity_bits::nothing;
        if(strchr(argv[2],'n')) bits|=osmium::osm_entity_bits::node;
        if(strchr(argv[2],'w')) bits|=osmium::osm_entity_bits::way;
        if(strchr(argv[2],'r')) bits|=osmium::osm_entity_bits::relation; }
    auto t0=std::chrono::steady_clock::now();
    osmium::io::Reader rd{argv[1], bits, osmium::io::read_meta::no};
    H h; osmium::apply(rd,h); rd.close();
    double s=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
    rusage ru; getrusage(RUSAGE_SELF,&ru);
    printf("%.2fs cpu=%.1fs n=%lu w=%lu r=%lu tags=%lu wayrefs=%lu members=%lu maxnode=%ld maxway=%ld rss=%ldMB\n",
      s, ru.ru_utime.tv_sec+ru.ru_stime.tv_sec+ (ru.ru_utime.tv_usec+ru.ru_stime.tv_usec)/1e6,
      h.n,h.w,h.r,h.tags,h.refs,h.mems,h.maxn,h.maxw,ru.ru_maxrss/1024);
}
