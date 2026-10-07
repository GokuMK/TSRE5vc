// Per-blob spatial locality: one osmium buffer == one PBF blob.
#include <osmium/io/pbf_input.hpp>
#include <osmium/index/map/sparse_mem_array.hpp>
#include <osmium/osm/box.hpp>
#include <cstdio>
#include <vector>
#include <string>
#include <algorithm>
using Index=osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type, osmium::Location>;
static bool isect(const osmium::Box&a,const osmium::Box&b){return !(a.top_right().lon()<b.bottom_left().lon()||b.top_right().lon()<a.bottom_left().lon()||a.top_right().lat()<b.bottom_left().lat()||b.top_right().lat()<a.bottom_left().lat());}
struct Q{const char* name; osmium::Box b;};
int main(int argc,char**argv){
    std::vector<Q> qs={
      {"tile Tczew 2km",   osmium::Box{18.7807,54.0833,18.8121,54.1017}},
      {"tile Gdansk 2km",  osmium::Box{18.6283,54.3465,18.6597,54.3649}},
      {"tile Warsaw 2km",  osmium::Box{20.9965,52.2205,21.0279,52.2389}},
      {"tile rural 2km",   osmium::Box{17.8843,54.2408,17.9157,54.2592}},
      {"route 60x60km",    osmium::Box{18.3,53.9,19.2,54.45}},
    };
    Index idx; std::vector<osmium::Box> nb, wb, rb; size_t emptyWayBoxes=0;
    osmium::io::Reader rd{argv[1], osmium::osm_entity_bits::node|osmium::osm_entity_bits::way, osmium::io::read_meta::no};
    while(osmium::memory::Buffer buf=rd.read()){
        osmium::Box b; int kind=-1;
        for(auto& e: buf.select<osmium::OSMObject>()){
            if(e.type()==osmium::item_type::node){ auto& n=static_cast<osmium::Node&>(e); idx.set(n.positive_id(), n.location()); b.extend(n.location()); kind=0; }
            else if(e.type()==osmium::item_type::way){ kind=1; auto& w=static_cast<osmium::Way&>(e);
                for(auto& r: w.nodes()){ auto l=idx.get_noexcept(r.positive_ref()); if(l.valid()) b.extend(l);} }
        }
        if(kind==0) { nb.push_back(b); if(nb.size()==1) {} }
        else if(kind==1) { if(!b.valid()) emptyWayBoxes++; wb.push_back(b); }
        if(kind==1 && wb.size()==1) idx.sort();
    }
    rd.close();
    auto area=[](const osmium::Box& b){ return b.valid()? (b.top_right().lon()-b.bottom_left().lon())*(b.top_right().lat()-b.bottom_left().lat()):0.0;};
    auto med=[&](std::vector<osmium::Box> v){ std::vector<double> a; for(auto&b:v) a.push_back(area(b)); std::sort(a.begin(),a.end()); return a.empty()?0:a[a.size()/2]; };
    printf("node blobs=%zu (median bbox %.4f deg2), way blobs=%zu (median %.4f deg2), way blobs w/o geometry=%zu\n", nb.size(), med(nb), wb.size(), med(wb), emptyWayBoxes);
    for(auto& q: qs){
        size_t n=0,w=0; for(auto&b:nb) if(b.valid()&&isect(b,q.b)) n++; for(auto&b:wb) if(b.valid()&&isect(b,q.b)) w++;
        printf("  %-18s node blobs %5zu/%zu (%4.1f%%)  way blobs %5zu/%zu (%4.1f%%)\n", q.name, n, nb.size(), 100.0*n/nb.size(), w, wb.size(), 100.0*w/wb.size());
    }
}
