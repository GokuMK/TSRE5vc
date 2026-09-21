#include "TerrainFileTestSuite.h"
#include "TokenTestSupport.h"
#include <tsre/world/TerrainFileData.h>
#include <tsre/world/ScopedBakeTFile.h>
#include <QTemporaryDir>
#include <QFile>
#include <cstring>
namespace TsreTests {
int runTerrainFileSuite(bool verbose) {
    using namespace TokenTest;
    using namespace TerrainFile;
    Suite test{"[tests:terrain-tfile]",verbose};
    auto roundTrip=[&](const QByteArray &bytes,const QString &name) {
        auto input=buffer(bytes);Data data;QString error;
        const bool ok=data.read(*input,error);
        test.check(ok && data.encode(error)==bytes,name+": "+error);
        return data;
    };
    for(int patches:{4,8,16,32}) {
        ScopedBakeTFile legacy;legacy.initNew("fixture",256,8,patches);
        const auto bytes=fields([&](QDataStream &out){legacy.save(out);});
        auto data=roundTrip(bytes,QString("legacy-P%1-exact").arg(patches));
        test.check(data.paired&&data.patchSets.size()==1&&data.activeSet()->patches.size()==size_t(patches*patches),"typed-patch-layout");
    }
    auto shader=[](const QString &name,int slotCount=1) {
        QByteArray textures,uvs;
        for(int i=0;i<slotCount;++i) {
            textures+=block(TS::terrain_texslot,string(QString("source%1.ace").arg(i))+uints({1,quint32(i)}));
            uvs+=block(TS::terrain_uvcalc,uints({1,0,quint32(i)})+floats({32.5f+i}));
        }
        return block(TS::terrain_shader,string(name)
            +block(TS::terrain_texslots,uints({quint32(slotCount)})+textures,"slots")
            +block(TS::terrain_uvcalcs,uints({quint32(slotCount)})+uvs));
    };
    auto patch=[](quint32 index,quint32 flags=0) {
        return block(TS::terrain_patchset_patch,uints({flags})+floats({4,3,-4,8,2,4})
            +uints({index})+floats({0,0,0.25f,0,0,0.25f,1}));
    };
    auto set=[&](quint32 index,float distance=0,quint32 flags=0) {
        return block(TS::terrain_patchset,
            block(TS::terrain_patchset_patches,patch(index,flags)) // Intentionally before P.
            +block(TS::terrain_patchset_distance,floats({distance}))
            +block(TS::terrain_patchset_npatches,uints({1}))
            +block(TS::terrain_patchset_fbuffer,string("patch-flags.raw")));
    };
    auto terrain=[&](const QByteArray &shaders,int count,const QByteArray &sets,int setCount) {
        return file(block(TS::terrain,block(TS::terrain_shaders,uints({quint32(count)})+shaders)
            +block(TS::terrain_patches,block(TS::terrain_patchsets,uints({quint32(setCount)})+sets))));
    };
    for(int count:{1,2,3,9}) {
        QByteArray shaders;for(int i=0;i<count;++i)shaders+=shader("TexDiff");
        auto data=roundTrip(terrain(shaders,count,set(count-1),1),QString("flat-%1").arg(count));
        test.check(!data.paired && data.repairAuxiliaryReferences()==0
            && data.activeSet()->patches[0].shaderIndex==quint32(count-1),"flat-full-index-range");
    }
    const auto pair=shader("DetailTerrain",4)+shader("AlphaTerrain",3);
    auto multiple=roundTrip(terrain(pair,2,set(0,1.5f)+set(1,23.25f,0x01000300),2),"multiple-sets-exact");
    test.check(multiple.shaders[0].textures.size()==4 && multiple.shaders[0].uvCalcs[3].scale==35.5f,"dynamic-slots-and-float-uv");
    test.check(multiple.patchSets.size()==2 && *multiple.activeSet()->distance==23.25f,"last-active-float-distance");
    test.check(multiple.repairAuxiliaryReferences()==1 && multiple.activeSet()->patches[0].shaderIndex==0
        && multiple.activeSet()->patches[0].flags==0x01000100 && multiple.repairAuxiliaryReferences()==0,"auxiliary-repair-idempotent-unrelated-flags");
    for(auto index:{0u,1u,2u,0xffffffffu}) {
        auto data=roundTrip(terrain(pair,2,set(index,0,0x200),1),"exact-uint-index");
        const int repaired=data.repairAuxiliaryReferences();
        test.check(index<2 ? repaired==1&&data.activeSet()->patches[0].shaderIndex==0
                             : repaired==0&&data.activeSet()->patches[0].shaderIndex==index,"repair-bounds");
    }
    const auto unknown=block(0xFFFF1000u,"opaque\0bytes","future");
    const auto rare=file(block(TS::terrain,
        unknown+block(TS::terrain_samples,
            block(TS::terrain_sample_cbuffer,string("colour.ace"),"c")
            +block(TS::terrain_sample_dbuffer,string("d.raw"))
            +block(TS::terrain_sample_usbuffer,QByteArray::fromHex("ff00"),"us")
            +block(TS::terrain_nsamples,uints({3}))
            +block(TS::terrain_sample_asbuffer,QByteArray::fromHex("01ff"))
            +block(TS::TSRETerrainMaterialBuffer,string("map.pmap"))+unknown,"samples")
        +block(TS::terrain_transfers,uints({1})+block(TS::terrain_transfer,shader("AlphaTerrain")+floats({20,-30,10,-2}),"transfer"))
        +block(TS::terrain_shapes,uints({1})+block(TS::terrain_shape,string("surface.s")+uints({0xfffffffe,2,30,40})+floats({0,0.5f,-1}),"shape"))
        +block(TS::terrain_water_height_offset,floats({27}),"water"),"root"));
    auto data=roundTrip(rare,"rare-records-labels-unknowns-exact");
    roundTrip(file(block(TS::terrain,block(TS::terrain_samples)
        +block(TS::terrain_shaders,uints({0}))+block(TS::terrain_transfers,uints({0}))
        +block(TS::terrain_shapes,uints({0}))+block(TS::terrain_patches))),"empty-container-presence");
    roundTrip(terrain(block(TS::terrain_shader,string("Custom")),1,set(0),1),"absent-slot-and-uv-lists");
    const auto exceptionalPatch=block(TS::terrain_patchset_patch,
        patch(0).mid(9)+QByteArray::fromHex("deadbeef"),"patch label");
    roundTrip(terrain(shader("TexDiff"),1,block(TS::terrain_patchset,
        block(TS::terrain_patchset_npatches,uints({1}))
        +block(TS::terrain_patchset_patches,unknown+exceptionalPatch+unknown)),1),"patch-tail-label-and-unknown-siblings");
    test.check(data.samples.c==QString("colour.ace")&&data.samples.d==QString("d.raw")
        && data.transfers[0].x0==20&&data.transfers[0].x1==10&&data.shapes[0].bounds[0]==-2
        && data.shapes[0].rotations[2]==-1&&data.water->single,"rare-typed-values");
    bool truncated=true;
    for(int size=0;size<rare.size();++size) {
        auto input=buffer(rare.left(size));QString error;
        truncated&=!data.read(*input,error)&&data.transfers.size()==1&&data.shapes.size()==1;
    }
    test.check(truncated,"all-truncations-rejected-without-changing-existing-data");
    auto duplicated=buffer(file(block(TS::terrain,block(TS::terrain_samples,
        block(TS::terrain_nsamples,uints({4}))+block(TS::terrain_nsamples,uints({8}))))));
    QString error;
    test.check(data.read(*duplicated,error)&&data.ambiguous&&data.encode(error).isEmpty(),"duplicate-preserved-but-rewrite-refused");
    // The codec does not impose renderer acceptance or normalize numeric bits.
    test.check(sizeof(Patch)==60,"patch-is-60-bytes-without-cold-data");
    for(quint32 bits:{0x80000000u,0x7fc01234u,0x7f800000u})
        roundTrip(file(block(TS::terrain,block(TS::terrain_water_height_offset,uints({bits})))),"single-water-bit-pattern");
    Data created;created.samples.c="colour.ace";
    auto createdBytes=created.encode(error);auto createdInput=buffer(createdBytes);
    test.check(data.read(*createdInput,error)&&data.samples.c==created.samples.c,"new-optional-sample-container-without-count");
    const auto future=file(block(TS::terrain,block(TS::terrain_samples,
        block(TS::TSRETerrainBakedMaterials,uints({9000})+unknown,"future-version"))));
    roundTrip(future,"future-procedural-container-remains-opaque");
    auto patched=multiple.encode(error);auto patchedInput=buffer(patched);
    test.check(data.read(*patchedInput,error)&&data.activeSet()->patches[0].shaderIndex==0
        && data.activeSet()->patches[0].flags==0x01000100,"repair-survives-save-reload");
    Data zeroGrid;zeroGrid.patchSets.emplace_back();zeroGrid.patchSets[0].patchesPerSide=0;
    test.check(zeroGrid.encode(error).isEmpty(),"writer-rejects-zero-grid");
    for(const auto &invalid:{
        terrain(shader("TexDiff"),2,set(0),1),
        terrain(shader("TexDiff"),1,set(0),2),
        file(block(TS::terrain,block(TS::terrain_shaders,uints({0xffffffffu})))),
        file(block(TS::terrain,block(TS::terrain_shapes,uints({1})+block(TS::terrain_shape,uints({0xffffffffu})))))}) {
        auto input=buffer(invalid);test.check(!data.read(*input,error),"reject-invalid-count-or-string");
    }
    auto wrongHeader=rare;wrongHeader[23]='t';auto wrongInput=buffer(wrongHeader);
    test.check(!data.read(*wrongInput,error),"text-header-explicitly-not-supported");
    QTemporaryDir temporary;
    for(bool compress:{false,true}) {
        const auto path=temporary.filePath(compress?"compressed.t":"plain.t");
        QFile f(path);test.check(f.open(QIODevice::WriteOnly),"open-file-fixture");
        f.write(compress?compressed(rare):rare);f.close();
        test.check(data.readFile(path,error)&&data.encode(error)==rare,"compressed-and-plain-file-roundtrip");
    }
    for(const auto &invalid:{QByteArray("SIMISA@F",8),
        QByteArray("SIMISA@F",8)+uints({quint32(Data::MaximumBytes)})+QByteArray("@@@@junk",8),
        QByteArray("SIMISA@F",8)+uints({1})+QByteArray("@@@@",4)+compressed(rare).mid(16)}) {
        const auto path=temporary.filePath("invalid-compressed.t");QFile f(path);
        const bool opened=f.open(QIODevice::WriteOnly);if(opened){f.write(invalid);f.close();}
        test.check(opened&&!data.readFile(path,error),"reject-truncated-oversize-or-wrong-inflate-length");
    }
    const auto savePath=temporary.filePath("saved.t");
    auto original=buffer(rare);data.read(*original,error);
    test.check(data.save(savePath,error)&&created.readFile(savePath,error)&&created.encode(error)==rare,"atomic-file-save-reload");
    auto incomplete=buffer(rare.left(80));
    test.check(!data.read(*incomplete,error)&&data.encode(error)==rare,"failed-reload-keeps-prior-descriptor");
    // Production TFile, not just the preservation codec.
    auto runtimeBytes=[](TFile &f){return fields([&](QDataStream &out){f.save(out);});};
    TFile runtime;
    auto runtimeInput=buffer(rare);
    test.check(runtime.load(runtimeInput.get())&&runtimeBytes(runtime)==rare,
               "runtime-preserves-native-rare-records-and-TSRE-extension");
    test.check(!runtime.canRemapMaterials(error),"opaque-future-fields-block-renumbering-only");
    runtime.sampleMaterialBuffer="renamed.pmap";
    Data retained;auto edited=buffer(runtimeBytes(runtime));
    test.check(retained.read(*edited,error)&&retained.transfers.size()==1&&retained.shapes.size()==1
               &&retained.extras.unknown==runtime.extras.unknown,"extension-edit-retains-unrelated-cold-records");
    runtimeInput=buffer(terrain(pair,2,set(1,0,0x300)+set(0),2));
    test.check(runtime.load(runtimeInput.get())&&runtime.paired&&runtime.materialCount()==1
               &&runtime.patchSets[0].patches[0].shaderIndex==0,"runtime-repairs-auxiliary-before-editing");
    test.check(runtime.newMat()==1&&runtime.materialCount()==2
               &&runtime.material(0).textures.size()==4&&runtime.auxiliary(0).textures.size()==3,
               "paired-insertion-retains-all-slots-and-half-order");
    runtime.patchSets[0].patches[0].shaderIndex=1;
    test.check(runtime.moveMaterialToFront(1,error)&&runtime.patchSets[0].patches[0].shaderIndex==0
               &&runtime.patchSets[1].patches[0].shaderIndex==1,"material-remap-covers-inactive-set");
    runtime.removeMat(1);
    test.check(runtime.materialCount()==1&&runtime.patchSets[1].patches[0].shaderIndex==0,
               "material-delete-covers-inactive-set");
    runtime.patches()[0].shaderIndex=1;
    test.check(!runtime.preflight(error),"runtime-refuses-new-direct-auxiliary-assignment");
    runtimeInput=buffer(terrain(shader("TexDiff")+shader("TexDiff"),2,set(1),1));
    test.check(runtime.load(runtimeInput.get())&&!runtime.paired&&runtime.materialCount()==2
               &&runtime.patches()[0].shaderIndex==1&&runtime.newMat()==2,
               "flat-even-table-retains-full-index-range-and-appends-flat");
    TFile generated;generated.initNew("layout",1024,2,32);
    const auto layoutPath=temporary.filePath("layout.t");
    test.check(generated.save(layoutPath),"runtime-new-tile-save");
    TFile::LayoutInfo info;
    test.check(TFile::readLayoutInfo(layoutPath,info)&&info.samples==1024&&info.spacing==2&&info.patches==32,
               "lightweight-layout-reader");
    auto broken=buffer(QByteArray("invalid"));
    const auto before=runtimeBytes(generated);
    test.check(!generated.load(broken.get())&&runtimeBytes(generated)==before,"runtime-load-is-transactional");
    // An external P*P flags resource wins for viewing, but untouched inline
    // words and unknown sidecar bits must survive an unrelated edit.
    Data flagFixture=generated;
    flagFixture.patchSets.back().flagsBuffer="flags.raw";
    flagFixture.patchSets.back().patches[0].flags=0x010000c0;
    const auto flagsPath=temporary.filePath("flags.raw");
    auto writeFile=[](const QString &path,const QByteArray &bytes){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();};
    auto readFile=[](const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();};
    const QByteArray flags(1024,char(0x14));
    test.check(flagFixture.save(layoutPath,error)&&writeFile(flagsPath,flags)
               &&runtime.readT(layoutPath)&&!runtime.patchFlagsWritable()
               &&runtime.loadPatchFlags(temporary.path(),error)&&runtime.patchFlagsWritable()
               &&runtime.patches()[0].flags==0x14,"external-patch-flags-override-inline");
    runtime.patches()[1].uv.x=0.2f;
    test.check(runtime.save(layoutPath)&&retained.readFile(layoutPath,error)
               &&retained.activeSet()->patches[0].flags==0x010000c0&&readFile(flagsPath)==flags,
               "unmodified-external-and-inline-flags-preserved");
    runtime.patches()[0].flags=0xc3;
    test.check(runtime.save(layoutPath)&&quint8(readFile(flagsPath)[0])==0xc3
               &&quint8(readFile(flagsPath)[1])==0x14,"only-edited-sidecar-byte-masked");
    const auto descriptorBefore=readFile(layoutPath);
    test.check(writeFile(flagsPath,QByteArray(1024,'x')),"external-sidecar-change-fixture");
    runtime.patches()[1].flags=1;
    test.check(!runtime.save(layoutPath)&&readFile(layoutPath)==descriptorBefore,
               "external-sidecar-conflict-does-not-overwrite-descriptor");
    QFile::remove(flagsPath);
    test.check(runtime.readT(layoutPath)&&!runtime.loadPatchFlags(temporary.path(),error)
               &&!runtime.patchFlagsWritable(),"missing-sidecar-is-not-writable");
    runtime.patches()[0].flags^=1;
    test.check(!runtime.preflight(error),"missing-sidecar-flag-edit-refused");
    // Bake metadata also has cold labels/unknown children, even while revised.
    const auto metadata=QByteArray(1,char(0))+fields([](QDataStream &s){s<<quint32(2)<<quint64(42);})+unknown;
    TFile bake;
    test.check(bake.readBakeMetadata(metadata)&&bake.bakeMetadata()==metadata,"bake-unknown-child-preserved");
    bake.materialContentRevision=43;
    test.check(bake.bakeMetadata().endsWith(unknown),"bake-revision-edit-preserves-unknown-child");
    return test.finish();
}
}
