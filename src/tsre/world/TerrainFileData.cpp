#include "TerrainFileData.h"
#include <tsre/fileFunctions/ContentPath.h>
#include <QFile>
#include <QSaveFile>
#include <QDataStream>
#include <QSet>
#include <QtEndian>
#include <QVarLengthArray>
#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>
#define MINIZ_HEADER_FILE_ONLY
#include <mzip/miniz/miniz.h>

namespace TerrainFile {
namespace {
quint64 key(TS::TokenId token,int index=0) { return (quint64(token)<<32)|quint32(index); }
bool sameFloatBits(const float &a,const float &b) {return std::memcmp(&a,&b,sizeof(float))==0;}
const Extras &childExtras(const Extras &parent,TS::TokenId token,int index=0) {
    static const Extras empty;
    const auto it=parent.children.constFind(key(token,index));
    return it==parent.children.cend()?empty:it.value();
}
struct Reader {
    FileBuffer &in;
    Data &out;
    int depth=0;
    static constexpr int MaxRecords=1024*1024;
    QByteArray bytes(int start,int end) const {
        return QByteArray(reinterpret_cast<const char*>(in.data+start),end-start);
    }
    template<class Decode>
    Extras payload(const FileBuffer::Block &block,Decode decode) {
        if (++depth>16) throw FileBuffer::ParseError("Terrain nesting limit");
        FileBuffer::ScopedLimit limit(in,block.end);
        Extras meta;
        meta.label=bytes(block.body+1,block.payload);
        in.off=block.payload;
        decode(meta);
        meta.tail=bytes(in.off,block.end);
        in.off=block.end;
        --depth;
        return meta;
    }
    template<class Decode>
    void children(Extras &meta,Decode decode) {
        // A container normally has a handful of distinct child tokens. Avoid a
        // heap/map node per scalar; unusually many unknown IDs can still spill.
        QVarLengthArray<std::pair<TS::TokenId,int>,32> occurrences;
        int records=0;
        while(in.off<in.readEnd()) {
            if(++records>MaxRecords)throw FileBuffer::ParseError("Too many terrain children");
            const int start=in.off;
            const auto block=in.readBlock();
            auto occurrence=std::find_if(occurrences.begin(),occurrences.end(),
                [&](const auto &entry){return entry.first==block.id;});
            int index=0;
            if(occurrence==occurrences.end())occurrences.append({block.id,1});
            else index=occurrence->second++;
            if (decode(block,index,meta)) meta.order.push_back({block.id,index});
            else {
                meta.order.push_back({block.id,-int(meta.unknown.size())-1});
                meta.unknown.push_back(bytes(start,block.end));
            }
            in.off=block.end;
        }
    }
    template<class Decode>
    bool field(const FileBuffer::Block &block,int index,Extras &meta,Decode decode) {
        if(index) {
            out.ambiguous=true;
            out.diagnostics<<QString("Duplicate %1 retained; descriptor rewrite disabled").arg(TS::describe(block.id));
            return false;
        }
        auto extra=payload(block,[&](Extras &){decode();});
        if(!extra.empty())meta.children.insert(key(block.id),std::move(extra));
        return true;
    }
    int count() {
        const quint32 n=in.getUint();
        if(n>MaxRecords || n>quint32((in.readEnd()-in.off)/9))
            throw FileBuffer::ParseError("Invalid terrain collection count");
        return int(n);
    }
    template<class T,class Decode>
    void collection(Extras &meta,TS::TokenId token,std::vector<T> &values,Decode decode,bool counted=true) {
        const int declared=counted?count():-1;
        if(declared>=0)values.reserve(declared);
        children(meta,[&](const auto &block,int,Extras &parent) {
            if(block.id!=token)return false;
            if(values.size()>=MaxRecords || (declared>=0 && int(values.size())>=declared))
                throw FileBuffer::ParseError("Too many terrain records");
            T value{};
            auto extra=payload(block,[&](Extras &m){decode(value,m);});
            if constexpr(std::is_same_v<T,Shader>||std::is_same_v<T,PatchSet>||std::is_same_v<T,Transfer>||std::is_same_v<T,Shape>)
                value.extras=std::move(extra);
            else if(!extra.empty())parent.children.insert(key(token,int(values.size())),std::move(extra));
            values.push_back(std::move(value));
            return true;
        });
        if(declared>=0 && int(values.size())!=declared)
            throw FileBuffer::ParseError("Terrain collection count mismatch");
    }
    void shader(Shader &value,Extras &meta) {
        value.name=in.readString();
        value.texturesPresent=value.uvCalcsPresent=false;
        children(meta,[&](const auto &block,int index,Extras &parent) {
            if(block.id!=TS::terrain_texslots && block.id!=TS::terrain_uvcalcs)return false;
            if(index){out.ambiguous=true;return false;}
            auto list=payload(block,[&](Extras &list) {
                if(block.id==TS::terrain_texslots) {
                    value.texturesPresent=true;
                    collection(list,TS::terrain_texslot,value.textures,[&](TextureSlot &slot,Extras &) {
                        slot.filename=in.readString();slot.arg0=in.getInt();slot.arg1=in.getInt();
                    });
                } else {
                    value.uvCalcsPresent=true;
                    collection(list,TS::terrain_uvcalc,value.uvCalcs,[&](UvCalc &uv,Extras &) {
                    uv.arg0=in.getInt();uv.arg1=in.getInt();uv.arg2=in.getInt();uv.scale=in.getFloat();
                });
                }
            });
            parent.children.insert(key(block.id),std::move(list));return true;
        });
    }
    void patch(Patch &p,Extras &) {
        // One checked fixed-width row, decoded field-by-field. Neither host
        // struct layout nor alignment/endianness is used as the file format.
        in.require(60);
        const unsigned char *cursor=in.data+in.off;
        const auto integer=[&]() {const auto v=qFromLittleEndian<quint32>(cursor);cursor+=4;return v;};
        const auto number=[&]() {const auto bits=integer();float v;std::memcpy(&v,&bits,4);return v;};
        p.flags=integer();p.centerX=number();p.averageY=number();p.centerZ=number();
        p.sphereRadius=number();p.rangeY=number();p.radiusM=number();p.shaderIndex=integer();
        p.uv.x=number();p.uv.y=number();p.uv.w=number();
        p.uv.b=number();p.uv.c=number();p.uv.h=number();p.errorBias=number();
        in.off+=60;
    }
    void patchList(PatchSet &set,Extras &meta) {
        // Packed common case: no occurrence map, order entry or metadata object
        // per ordinary patch. Exceptions retain their exact framing separately.
        set.patches.reserve(std::min(MaxRecords,(in.readEnd()-in.off)/69));
        Extras unused;
        int records=0;
        while(in.off<in.readEnd()) {
            if(++records>MaxRecords)throw FileBuffer::ParseError("Too many terrain patch children");
            const int start=in.off;
            const auto block=in.readBlock();
            if(block.id!=TS::terrain_patchset_patch) {
                if(meta.order.empty())for(size_t i=0;i<set.patches.size();++i)
                    meta.order.push_back({TS::terrain_patchset_patch,int(i)});
                meta.order.push_back({block.id,-int(meta.unknown.size())-1});
                meta.unknown.push_back(bytes(start,block.end));in.off=block.end;continue;
            }
            if(set.patches.size()>=MaxRecords)throw FileBuffer::ParseError("Too many terrain patches");
            FileBuffer::ScopedLimit limit(in,block.end);in.off=block.payload;
            set.patches.emplace_back();patch(set.patches.back(),unused);
            const int index=int(set.patches.size())-1;
            if(block.payload!=block.body+1 || in.off!=block.end) {
                Extras extra;extra.label=bytes(block.body+1,block.payload);extra.tail=bytes(in.off,block.end);
                meta.children.insert(key(block.id,index),std::move(extra));
            }
            if(!meta.order.empty())meta.order.push_back({block.id,index});
            in.off=block.end;
        }
    }
    void patchSet(PatchSet &set,Extras &meta) {
        children(meta,[&](const auto &block,int index,Extras &parent) {
            switch(block.id) {
            case TS::terrain_patchset_distance:return field(block,index,parent,[&]{set.distance=in.getFloat();});
            case TS::terrain_patchset_npatches:return field(block,index,parent,[&]{set.patchesPerSide=in.getUint();});
            case TS::terrain_patchset_fbuffer:return field(block,index,parent,[&]{set.flagsBuffer=in.readString();});
            case TS::terrain_patchset_patches: {
                if(index){out.ambiguous=true;return false;}
                auto extra=payload(block,[&](Extras &m){patchList(set,m);});
                parent.children.insert(key(block.id),std::move(extra));return true;
            }
            default:return false;
            }
        });
        if(!set.patchesPerSide || !*set.patchesPerSide
                || quint64(*set.patchesPerSide)* *set.patchesPerSide!=set.patches.size())
            throw FileBuffer::ParseError("Terrain patch grid/record count mismatch");
    }
    void samples(Samples &s,Extras &meta) {
        children(meta,[&](const auto &block,int index,Extras &parent) {
            switch(block.id) {
            case TS::terrain_nsamples:return field(block,index,parent,[&]{s.count=in.getUint();});
            case TS::terrain_sample_rotation:return field(block,index,parent,[&]{s.rotation=in.getFloat();});
            case TS::terrain_sample_floor:return field(block,index,parent,[&]{s.floor=in.getFloat();});
            case TS::terrain_sample_scale:return field(block,index,parent,[&]{s.scale=in.getFloat();});
            case TS::terrain_sample_size:return field(block,index,parent,[&]{s.spacing=in.getFloat();});
            case TS::terrain_sample_fbuffer:return field(block,index,parent,[&]{s.f=in.readString();});
            case TS::terrain_sample_ybuffer:return field(block,index,parent,[&]{s.y=in.readString();});
            case TS::terrain_sample_ebuffer:return field(block,index,parent,[&]{s.e=in.readString();});
            case TS::terrain_sample_nbuffer:return field(block,index,parent,[&]{s.n=in.readString();});
            case TS::terrain_sample_cbuffer:return field(block,index,parent,[&]{s.c=in.readString();});
            case TS::terrain_sample_dbuffer:return field(block,index,parent,[&]{s.d=in.readString();});
            case TS::terrain_sample_asbuffer:
            case TS::terrain_sample_usbuffer:return field(block,index,parent,[&] {
                auto &v=block.id==TS::terrain_sample_asbuffer?s.alwaysSelect:s.unknownSelect;
                v=bytes(in.off,in.readEnd());in.off=in.readEnd();
            });
            default:return false; // Extensions preserved independently of native semantics.
            }
        });
    }
    void root(Extras &meta) {
        children(meta,[&](const auto &block,int index,Extras &parent) {
            switch(block.id) {
            case TS::terrain_errthreshold_scale:return field(block,index,parent,[&]{out.errorThresholdScale=in.getFloat();});
            case TS::terrain_alwaysselect_maxdist:return field(block,index,parent,[&]{out.alwaysSelectMaxDistance=in.getFloat();});
            case TS::terrain_water_height_offset:return field(block,index,parent,[&] {
                const int size=in.readEnd()-in.off;
                if(size!=4&&size!=16)throw FileBuffer::ParseError("Invalid terrain water record");
                Water w;w.single=size==4;w.sw=in.getFloat();
                w.se=w.single?w.sw:in.getFloat();w.ne=w.single?w.sw:in.getFloat();w.nw=w.single?w.sw:in.getFloat();out.water=w;
            });
            case TS::terrain_samples:
            case TS::terrain_shaders:
            case TS::terrain_patches:
            case TS::terrain_transfers:
            case TS::terrain_shapes:break;
            default:return false;
            }
            if(index){out.ambiguous=true;return false;}
            auto extra=payload(block,[&](Extras &m) {
                switch(block.id) {
                case TS::terrain_samples:samples(out.samples,m);break;
                case TS::terrain_shaders:collection(m,TS::terrain_shader,out.shaders,[&](Shader &s,Extras &e){shader(s,e);});break;
                case TS::terrain_patches:children(m,[&](const auto &sets,int n,Extras &p) {
                    if(sets.id!=TS::terrain_patchsets)return false;
                    if(n){out.ambiguous=true;return false;}
                    auto e=payload(sets,[&](Extras &list){collection(list,TS::terrain_patchset,out.patchSets,
                        [&](PatchSet &s,Extras &x){patchSet(s,x);});});
                    p.children.insert(key(sets.id),std::move(e));return true;
                });break;
                case TS::terrain_transfers:collection(m,TS::terrain_transfer,out.transfers,[&](Transfer &t,Extras &e) {
                    const auto shaderBlock=in.readBlock();
                    if(shaderBlock.id!=TS::terrain_shader)throw FileBuffer::ParseError("Terrain transfer has no shader");
                    auto s=payload(shaderBlock,[&](Extras &x){shader(t.shader,x);});
                    t.shader.extras=std::move(s);
                    t.x0=in.getFloat();t.z0=in.getFloat();t.x1=in.getFloat();t.z1=in.getFloat();
                });break;
                case TS::terrain_shapes:collection(m,TS::terrain_shape,out.shapes,[&](Shape &s,Extras &) {
                    s.filename=in.readString();for(auto &v:s.bounds)v=in.getInt();for(auto &v:s.rotations)v=in.getFloat();
                });break;
                default:break;
                }
            });
            parent.children.insert(key(block.id),std::move(extra));return true;
        });
    }
};

struct Writer {
    QByteArray bytes;
    QDataStream out{&bytes,QIODevice::WriteOnly};
    Writer() { out.setByteOrder(QDataStream::LittleEndian);out.setFloatingPointPrecision(QDataStream::SinglePrecision); }
    void raw(const QByteArray &b) {
        if(b.size()>Data::MaximumBytes-out.device()->pos())
            throw FileBuffer::ParseError("Terrain output exceeds descriptor limit");
        out.writeRawData(b.constData(),b.size());
    }
    void string(const QString &s) {
        if(s.size()>65535)throw FileBuffer::ParseError("Terrain string too long");
        out<<quint16(s.size());for(auto c:s)out<<c.unicode();
    }
    void floating(float value) { quint32 bits;std::memcpy(&bits,&value,4);out<<bits; }
    template<class Emit> void block(TS::TokenId token,const Extras &meta,Emit writeBody) {
        if(meta.label.size()>510 || meta.label.size()%2)throw FileBuffer::ParseError("Terrain label too long");
        const auto start=out.device()->pos();
        out<<quint32(token)<<quint32(0)<<quint8(meta.label.size()/2);raw(meta.label);
        writeBody();raw(meta.tail);
        const auto end=out.device()->pos();
        if(end>Data::MaximumBytes)throw FileBuffer::ParseError("Terrain output exceeds descriptor limit");
        out.device()->seek(start+4);out<<quint32(end-start-8);out.device()->seek(end);
    }
    template<class Emit> void children(const Extras &meta,const std::vector<OrderEntry> &expected,Emit writeChild) {
        QSet<quint64> emitted;
        for(const auto &entry:meta.order) {
            if(entry.index<0) {
                const size_t index=size_t(-qint64(entry.index)-1);
                if(index>=meta.unknown.size())throw FileBuffer::ParseError("Invalid terrain preservation entry");
                raw(meta.unknown[index]);
            } else if(!emitted.contains(key(entry.token,entry.index))) {
                writeChild(entry.token,entry.index);emitted.insert(key(entry.token,entry.index));
            }
        }
        for(const auto &entry:expected)if(!emitted.contains(key(entry.token,entry.index)))writeChild(entry.token,entry.index);
    }
    template<class T,class Emit> void collection(TS::TokenId token,const std::vector<T> &values,const Extras &meta,Emit writeRecord,bool counted=true) {
        if(values.size()>Reader::MaxRecords)throw FileBuffer::ParseError("Too many terrain records on save");
        if(counted)out<<quint32(values.size());
        if(meta.order.empty()) {
            for(size_t i=0;i<values.size();++i) {
                const auto &extra=[&]() -> const Extras & {
                    if constexpr(std::is_same_v<T,Shader>||std::is_same_v<T,PatchSet>||std::is_same_v<T,Transfer>||std::is_same_v<T,Shape>)
                        return values[i].extras;
                    else return childExtras(meta,token,int(i));
                }();
                if constexpr(std::is_same_v<T,Patch>) {
                    if(extra.empty()) {
                        out<<quint32(token)<<quint32(61)<<quint8(0);writeRecord(values[i],extra);continue;
                    }
                }
                block(token,extra,[&]{writeRecord(values[i],extra);});
            }
            return;
        }
        std::vector<OrderEntry> order;order.reserve(values.size());
        for(size_t i=0;i<values.size();++i)order.push_back({token,int(i)});
        children(meta,order,[&](TS::TokenId id,int index) {
            if(id!=token || index<0 || size_t(index)>=values.size())return;
            const auto &extra=[&]() -> const Extras & {
                if constexpr(std::is_same_v<T,Shader>||std::is_same_v<T,PatchSet>||std::is_same_v<T,Transfer>||std::is_same_v<T,Shape>)
                    return values[index].extras;
                else return childExtras(meta,id,index);
            }();
            block(id,extra,[&]{writeRecord(values[index],extra);});
        });
    }
    void shader(const Shader &s,const Extras &meta) {
        string(s.name);
        children(meta,{{TS::terrain_texslots,0},{TS::terrain_uvcalcs,0}},[&](TS::TokenId id,int) {
            const auto &m=childExtras(meta,id);
            if(id==TS::terrain_texslots&&(s.texturesPresent||!s.textures.empty()))block(id,m,[&]{collection(TS::terrain_texslot,s.textures,m,[&](const TextureSlot &v,const Extras &){string(v.filename);out<<v.arg0<<v.arg1;});});
            if(id==TS::terrain_uvcalcs&&(s.uvCalcsPresent||!s.uvCalcs.empty()))block(id,m,[&]{collection(TS::terrain_uvcalc,s.uvCalcs,m,[&](const UvCalc &v,const Extras &){out<<v.arg0<<v.arg1<<v.arg2;floating(v.scale);});});
        });
    }
    void patch(const Patch &p) {
        out<<p.flags;floating(p.centerX);floating(p.averageY);floating(p.centerZ);
        floating(p.sphereRadius);floating(p.rangeY);floating(p.radiusM);out<<p.shaderIndex;
        for(float v:{p.uv.x,p.uv.y,p.uv.w,p.uv.b,p.uv.c,p.uv.h,p.errorBias})floating(v);
    }
    void patchSet(const PatchSet &s,const Extras &meta) {
        if(!s.patchesPerSide || !*s.patchesPerSide || quint64(*s.patchesPerSide)* *s.patchesPerSide!=s.patches.size())
            throw FileBuffer::ParseError("Invalid terrain patch grid on save");
        children(meta,{{TS::terrain_patchset_distance,0},{TS::terrain_patchset_npatches,0},
            {TS::terrain_patchset_fbuffer,0},{TS::terrain_patchset_patches,0}},[&](TS::TokenId id,int) {
            const auto &m=childExtras(meta,id);
            switch(id) {
            case TS::terrain_patchset_distance:if(s.distance)block(id,m,[&]{floating(*s.distance);});break;
            case TS::terrain_patchset_npatches:if(s.patchesPerSide)block(id,m,[&]{out<<*s.patchesPerSide;});break;
            case TS::terrain_patchset_fbuffer:if(s.flagsBuffer)block(id,m,[&]{string(*s.flagsBuffer);});break;
            case TS::terrain_patchset_patches:block(id,m,[&]{collection(TS::terrain_patchset_patch,s.patches,m,[&](const Patch &p,const Extras &){patch(p);},false);});break;
            default:break;
            }
        });
    }
    void samples(const Samples &s,const Extras &meta) {
        std::vector<OrderEntry> fields;
        for(auto token:{TS::terrain_nsamples,TS::terrain_sample_rotation,TS::terrain_sample_floor,
            TS::terrain_sample_scale,TS::terrain_sample_size,TS::terrain_sample_asbuffer,TS::terrain_sample_usbuffer,
            TS::terrain_sample_fbuffer,TS::terrain_sample_ybuffer,TS::terrain_sample_ebuffer,
            TS::terrain_sample_nbuffer,TS::terrain_sample_cbuffer,TS::terrain_sample_dbuffer})fields.push_back({token,0});
        children(meta,fields,[&](TS::TokenId id,int) {
            const auto &m=childExtras(meta,id);
            const std::optional<float> *number=nullptr;
            const std::optional<QString> *text=nullptr;
            switch(id) {
            case TS::terrain_nsamples:if(s.count)block(id,m,[&]{out<<*s.count;});return;
            case TS::terrain_sample_rotation:number=&s.rotation;break;
            case TS::terrain_sample_floor:number=&s.floor;break;
            case TS::terrain_sample_scale:number=&s.scale;break;
            case TS::terrain_sample_size:number=&s.spacing;break;
            case TS::terrain_sample_fbuffer:text=&s.f;break;
            case TS::terrain_sample_ybuffer:text=&s.y;break;
            case TS::terrain_sample_ebuffer:text=&s.e;break;
            case TS::terrain_sample_nbuffer:text=&s.n;break;
            case TS::terrain_sample_cbuffer:text=&s.c;break;
            case TS::terrain_sample_dbuffer:text=&s.d;break;
            case TS::terrain_sample_asbuffer:if(s.alwaysSelect)block(id,m,[&]{raw(*s.alwaysSelect);});return;
            case TS::terrain_sample_usbuffer:if(s.unknownSelect)block(id,m,[&]{raw(*s.unknownSelect);});return;
            default:return;
            }
            if(number&&*number)block(id,m,[&]{floating(**number);});
            if(text&&*text)block(id,m,[&]{string(**text);});
        });
    }
    void root(const Data &d) {
        const auto &meta=d.extras;
        children(meta,{{TS::terrain_errthreshold_scale,0},{TS::terrain_water_height_offset,0},
            {TS::terrain_alwaysselect_maxdist,0},{TS::terrain_samples,0},{TS::terrain_shaders,0},
            {TS::terrain_patches,0},{TS::terrain_transfers,0},{TS::terrain_shapes,0}},[&](TS::TokenId id,int) {
            const auto &m=childExtras(meta,id);
            switch(id) {
            case TS::terrain_errthreshold_scale:if(d.errorThresholdScale)block(id,m,[&]{floating(*d.errorThresholdScale);});break;
            case TS::terrain_alwaysselect_maxdist:if(d.alwaysSelectMaxDistance)block(id,m,[&]{floating(*d.alwaysSelectMaxDistance);});break;
            case TS::terrain_water_height_offset:if(d.water)block(id,m,[&] {
                const auto &v=*d.water;floating(v.sw);
                if(!v.single||!sameFloatBits(v.sw,v.se)||!sameFloatBits(v.sw,v.ne)||!sameFloatBits(v.sw,v.nw)) {
                    floating(v.se);floating(v.ne);floating(v.nw);
                }
            });break;
            case TS::terrain_samples: {
                const auto &s=d.samples;
                if(s.count||s.rotation||s.floor||s.scale||s.spacing||s.y||s.f||s.e||s.n||s.c||s.d
                    ||s.alwaysSelect||s.unknownSelect||meta.children.contains(key(id)))
                    block(id,m,[&]{samples(s,m);});
                break;
            }
            case TS::terrain_shaders:if(!d.shaders.empty()||meta.children.contains(key(id)))block(id,m,[&]{collection(TS::terrain_shader,d.shaders,m,[&](const Shader &v,const Extras &e){shader(v,e);});});break;
            case TS::terrain_patches:if(!d.patchSets.empty()||meta.children.contains(key(id)))block(id,m,[&] {
                children(m,{{TS::terrain_patchsets,0}},[&](TS::TokenId token,int) {
                    if(token!=TS::terrain_patchsets)return;
                    if(d.patchSets.empty()&&!m.children.contains(key(token)))return;
                    const auto &e=childExtras(m,token);
                    block(token,e,[&]{collection(TS::terrain_patchset,d.patchSets,e,[&](const PatchSet &v,const Extras &x){patchSet(v,x);});});
                });
            });break;
            case TS::terrain_transfers:if(!d.transfers.empty()||meta.children.contains(key(id)))block(id,m,[&]{collection(TS::terrain_transfer,d.transfers,m,[&](const Transfer &v,const Extras &e){
                const auto &s=v.shader.extras;block(TS::terrain_shader,s,[&]{shader(v.shader,s);});
                floating(v.x0);floating(v.z0);floating(v.x1);floating(v.z1);
            });});break;
            case TS::terrain_shapes:if(!d.shapes.empty()||meta.children.contains(key(id)))block(id,m,[&]{collection(TS::terrain_shape,d.shapes,m,[&](const Shape &v,const Extras &){
                string(v.filename);for(auto i:v.bounds)out<<i;for(auto f:v.rotations)floating(f);
            });});break;
            default:break;
            }
        });
    }
};
}
bool Extras::empty() const {return label.isEmpty()&&tail.isEmpty()&&order.empty()&&unknown.empty()&&children.isEmpty();}
bool Data::read(FileBuffer &input,QString &error) {
    const int start=input.off;
    try {
        FileBuffer::ScopedLimit bounds(input,input.readEnd());
        input.require(32);
        if(std::memcmp(input.data+start,"SIMISA",6) || std::memcmp(input.data+start+16,"JINX0t",6)
                || input.data[start+23]!='b')throw FileBuffer::ParseError("Expected binary terrain SIMIS header");
        if(input.readEnd()-start>MaximumBytes)throw FileBuffer::ParseError("Terrain descriptor exceeds size limit");
        input.off+=32;
        const auto root=input.readBlock();
        if(root.id!=TS::terrain)throw FileBuffer::ParseError("Expected terrain root");
        Data next;Reader reader{input,next};
        next.extras=reader.payload(root,[&](Extras &m){reader.root(m);});
        if(input.off!=input.readEnd())throw FileBuffer::ParseError("Unexpected bytes after terrain root");
        if(next.samples.count)for(const auto *mask:{&next.samples.alwaysSelect,&next.samples.unknownSelect}) {
            const quint64 n=*next.samples.count;
            if(*mask && (n>=MaximumBytes || quint64((*mask)->size())!=((n+1)*(n+1)+7)/8))
                next.diagnostics<<"Terrain AS/US dimensions mismatch; opaque bytes retained";
        }
        next.detectShaderLayout();*this=std::move(next);error.clear();return true;
    } catch(const FileBuffer::ParseError &e) {
        error=QString("Terrain descriptor at byte %1: %2").arg(input.off-start).arg(e.what());return false;
    }
}
bool readDescriptorBytes(const QString &path,QByteArray &bytes,QString &error) {
    QFile file(ContentPath::normalize(path));
    if(!file.open(QIODevice::ReadOnly)){error=file.errorString();return false;}
    if(file.size()>Data::MaximumBytes){error="Terrain input exceeds size limit";return false;}
    bytes=file.readAll();
    if(file.error()!=QFile::NoError || bytes.size()!=file.size()) {
        error="Could not read complete terrain descriptor: "+file.errorString();return false;
    }
    if(bytes.startsWith("SIMISA@F")) {
        if(bytes.size()<20){error="Truncated compressed terrain";return false;}
        const quint32 expected=qFromLittleEndian<quint32>(bytes.constData()+8);
        if(expected>Data::MaximumBytes-16){error="Inflated terrain exceeds size limit";return false;}
        QByteArray inflated(expected,Qt::Uninitialized);
        mz_ulong actual=expected;
        const int status=mz_uncompress(reinterpret_cast<unsigned char*>(inflated.data()),&actual,
            reinterpret_cast<const unsigned char*>(bytes.constData()+16),bytes.size()-16);
        if(status!=MZ_OK || actual!=expected){error="Invalid compressed terrain stream";return false;}
        bytes=QByteArray("SIMISA@@@@@@@@@@",16)+inflated;
    }
    return true;
}
bool Data::readFile(const QString &path,QString &error) {
    QByteArray bytes;if(!readDescriptorBytes(path,bytes,error))return false;
    auto memory=new unsigned char[bytes.size()];std::memcpy(memory,bytes.constData(),bytes.size());
    FileBuffer input(memory,bytes.size());return read(input,error);
}
QByteArray Data::encode(QString &error) const {
    if(ambiguous){error="Ambiguous terrain fields must be resolved before rewriting";return {};}
    try {
        Writer writer;writer.raw(QByteArray("SIMISA@@@@@@@@@@JINX0t6b______\r\n",32));
        writer.block(TS::terrain,extras,[&]{writer.root(*this);});
        if(writer.out.status()!=QDataStream::Ok){error="Terrain serialization failed";return {};}
        error.clear();return writer.bytes;
    }catch(const FileBuffer::ParseError &e){error=e.what();return {};}
}
bool Data::save(const QString &path,QString &error) const {
    const auto bytes=encode(error);if(bytes.isEmpty())return false;
    QSaveFile file(ContentPath::normalize(path));
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){error=file.errorString();return false;}
    return true;
}
void Data::detectShaderLayout() {
    paired=!shaders.empty()&&shaders.size()%2==0;
    const auto half=shaders.size()/2;
    for(size_t i=0;paired&&i<half;++i)
        paired=shaders[i].name.compare("DetailTerrain",Qt::CaseInsensitive)==0
                && shaders[i+half].name.compare("AlphaTerrain",Qt::CaseInsensitive)==0;
}
int Data::repairAuxiliaryReferences() {
    // Explicit edit/load-time operation, never called in drawing/sample loops.
    detectShaderLayout();
    if(!paired)return 0;
    const quint32 half=quint32(shaders.size()/2);int changed=0;
    for(auto &set:patchSets)for(auto &patch:set.patches) {
        if(patch.shaderIndex>=shaders.size())continue;
        const bool auxiliary=patch.shaderIndex>=half;
        if(auxiliary)patch.shaderIndex-=half;
        if(auxiliary||(patch.flags&0x200)) {
            if(patch.flags&0x200)patch.flags=(patch.flags&~quint32(0x200))|0x100;
            ++changed;
        }
    }
    if(changed)diagnostics<<QString("Repaired %1 stale auxiliary terrain references").arg(changed);
    return changed;
}
}
