// Every way-node location in the converted file must equal the original node's location.
#include <osmium/io/pbf_input.hpp>
#include <osmium/index/map/sparse_mem_array.hpp>
#include <cstdio>
using Index=osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type, osmium::Location>;
int main(int, char** argv){
    Index idx;
    { osmium::io::Reader r{argv[1], osmium::osm_entity_bits::node, osmium::io::read_meta::no};
      while(auto b=r.read()) for(auto& n: b.select<osmium::Node>()) idx.set(n.positive_id(), n.location()); }
    idx.sort();
    uint64_t ways=0, refs=0, bad=0, missing=0;
    osmium::io::Reader r{argv[2], osmium::osm_entity_bits::way, osmium::io::read_meta::no};
    while(auto b=r.read()) for(auto& w: b.select<osmium::Way>()){ ways++;
        for(auto& n: w.nodes()){ refs++; auto l=idx.get_noexcept(n.positive_ref()); if(!l.valid()) missing++; else if(!(l==n.location())) bad++; } }
    printf("verify: ways %lu refs %lu mismatched %lu missing-in-original %lu\n", ways, refs, bad, missing);
}
