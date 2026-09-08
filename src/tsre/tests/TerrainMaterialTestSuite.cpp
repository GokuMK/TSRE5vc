#include "TerrainMaterialTestSuite.h"
#include <tsre/world/TerrainMaterialMap.h>
#include <tsre/world/TerrainMaterialSource.h>
#include <tsre/world/Terrain.h>
#include <tsre/world/TerrainLib.h>
#include <tsre/world/TerrainMeshBackend.h>
#include <tsre/texture/Texture.h>
#include <tsre/texture/AceLib.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Brush.h>
#include <tsre/Game.h>
#include <tsre/renderer/RenderItem.h>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QElapsedTimer>
#include <QScopedValueRollback>
#include <QtEndian>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLExtraFunctions>
#include <routeEditor/TerrainTools.h>
#include <QPushButton>
#include <QThread>
#include <algorithm>
#include <cstring>

namespace {
class TestTerrain : public Terrain {
public:
    TFile &descriptor() { return *tfile; }
    QByteArray staticDescriptorBytes() {
        QScopedValueRollback<QString> reference(tfile->sampleMaterialBuffer,QString());
        QByteArray bytes; QDataStream out(&bytes,QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::LittleEndian);
        out.setFloatingPointPrecision(QDataStream::SinglePrecision);
        tfile->save(out); return bytes;
    }
    using Terrain::proceduralTexture;
    using Terrain::proceduralDetailTexture;
    using Terrain::loadProceduralMaterial;
    using Terrain::PatchVisibility;
    using Terrain::proceduralRequestOrder;
    using Terrain::prepareVisibleProceduralTextures;
    using Terrain::proceduralNearCamera;
    using Terrain::proceduralTextureRemap;
    using Terrain::proceduralFallbackTexture;
    void setup(const QString &directory, int patches, const QString &tileName, int samples=256, int sampleSpacing=8) {
        name=tileName;
        tfile=new TFile(); tfile->initNew(tileName,samples,sampleSpacing,patches);
        QString error;
        TerrainGridLayout::tryCreate(samples,sampleSpacing,patches,0,gridLayout,error);
        tfile->materials[0].tex[0]=new QString("red.png");
        tfile->materials[0].count153=1;
        tfile->newMat();
        tfile->materials[1].tex[0]=new QString("blue.png");
        tfile->materials[1].count153=1;
        texturepath=directory;
        for (int i=0; i<gridLayout.patchRecordCount(); ++i) {
            texModified[i]=texLocked[i]=false; texid[i]=texid2[i]=-1;
            hidden[i]=false;
        }
        terrainDataRows=samples+1;
        terrainData=new float*[samples+1];
        for (int i=0;i<=samples;++i) terrainData[i]=new float[samples+1]();
        loaded=editable=true;
        initializePatchBounds();
    }
    void lock(int patch, bool value=true) { texLocked[patch]=value; }
    void hide(int patch, bool value) { hidden[patch]=value; }
    void spacing(float value) {
        *tfile->sampleSize=value; QString error;
        TerrainGridLayout::tryCreate(*tfile->nsamples,value,gridLayout.patchesPerSide,0,gridLayout,error);
        initializePatchBounds();
    }
};
class TestMaterialTerrainLib : public TerrainLib {
public:
    Terrain *first=nullptr, *second=nullptr;
    int lookups=0;
    Terrain *getTerrainByXY(int x,int z,bool = false) override {
        ++lookups;
        return z==0 ? (x==0 ? first : (x==1 ? second : nullptr)) : nullptr;
    }
};
bool finishMaterialJobs() {
    QElapsedTimer timer; timer.start();
    do {
        Terrain::beginProceduralFrame();
        if (Terrain::proceduralWorkStats().outstanding==0) return true;
        QThread::msleep(1);
    } while(timer.elapsed()<10000);
    return false;
}
void variedMaterialMap(TerrainMaterialMap &map) {
    map.initialize();
    const int side=TerrainMaterialMap::Side/16;
    for(int p=0;p<8;++p)
        for(int z=0;z<side;++z)
            for(int x=0;x<(p+1)*side/10;++x)
                map.ids[z*TerrainMaterialMap::Side+p*side+x]=1;
}
}

int TsreTests::runTerrainMaterialSuite(bool verbose, bool benchmark) {
    int passed=0,failed=0;
    auto check=[&](bool ok,const char *name) {
        if (ok) ++passed; else ++failed;
        if (!ok || verbose) qInfo() << "[tests:terrain-material]" << (ok?"PASS":"FAIL") << name;
    };
    TerrainMaterialMap map;
    map.initialize();
    check(map.valid() && map.usedIds()==QSet<int>{0},"default-zero-plane");
    map.ids[42]=char(255);
    QString error;
    QByteArray decoded;
    const auto encoded=map.encode();
    check(TerrainMaterialMap::decode(encoded,decoded,error) && decoded==map.ids,"compressed-round-trip-including-255");
    QByteArray bad=encoded; bad[8]=2;
    check(!TerrainMaterialMap::decode(bad,decoded,error),"reject-version");
    bad=encoded; bad[12]=1;
    check(!TerrainMaterialMap::decode(bad,decoded,error),"reject-dimensions");
    check(!TerrainMaterialMap::decode(encoded.chopped(4),decoded,error),"reject-truncated-stream");
    check(!TerrainMaterialMap::decode(encoded+"garbage",decoded,error),"reject-trailing-stream-data");
    bad=encoded.left(20)+qCompress(QByteArray(TerrainMaterialMap::Side*TerrainMaterialMap::Side+1,'x')).mid(4);
    check(!TerrainMaterialMap::decode(bad,decoded,error),"bounded-inflate-rejects-extra-output-byte");
    QImage mask(9,9,QImage::Format_Grayscale8); mask.fill(255);
    map.initialize();
    check(map.paint(1024,1024,300,1,32,mask).isEmpty(),"white-mask-no-op");
    mask.fill(254);
    const auto changed=map.paint(1024,1024,300,255,32,mask,{},false,3);
    const int touchedSide=1323/(TerrainMaterialMap::Side/32)-724/(TerrainMaterialMap::Side/32)+1;
    check(changed.size()==touchedSide*touchedSide && map.at(1024,1024)==255,"nonwhite-hard-replacement-many-patches");
    check(map.paint(1024,1024,300,255,32,mask).isEmpty(),"same-id-no-op");
    check(map.paint(1024,1024,300,256,32,mask).isEmpty(),"reject-id-256");
    check(map.paint(1024,1024,300,-1,32,mask).isEmpty(),"reject-negative-id");
    map.initialize();
    check(map.paint(32,32,8,1,32,mask,{0}).isEmpty(),"locked-patch-no-op");
    check(map.paint(-100,-100,1,1,16,mask).isEmpty(),"out-of-tile-no-op");
    QImage red(256,256,QImage::Format_RGB888); red.fill(Qt::red);
    QImage blue(256,256,QImage::Format_RGB888); blue.fill(Qt::blue);
    QHash<int,QImage> sources{{0,red},{1,blue},{255,blue}};
    for (int p : {4,8,16,32}) {
        map.initialize();
        const int k=TerrainMaterialMap::Side/p;
        for (int z=0;z<k;++z) for(int x=k/2;x<k;++x) map.ids[z*TerrainMaterialMap::Side+x]=1;
        const auto rgb=map.generate(0,p,sources,3);
        const int mid = TerrainMaterialMap::OutputSide/2;
        check(rgb.pixelColor(mid-1,mid)==QColor(Qt::red) && rgb.pixelColor(mid,mid)==QColor(Qt::blue),"categorical-input-output-mapping");
        check(map.patchKey(1,p,3)==map.patchKey(2,p,3) && map.patchKey(0,p,3)!=map.patchKey(1,p,3),"local-recipe-key");
    }
    map.initialize();
    map.ids[11*TerrainMaterialMap::Side+11]=1;
    check(map.sampleId(11,11,1)==0,"strongest-sums-support-before-applying-priority");
    map.ids[11*TerrainMaterialMap::Side+11]=3;
    check(map.sampleId(11,11,1)==3,"higher-id-actively-outweighs-lower-support");
    map.ids[11*TerrainMaterialMap::Side+11]=2;
    check(map.sampleId(11,11,1)==2,"higher-id-breaks-equal-weighted-score-tie");
    check(map.sampleId(10.5,10.5,1)==0,"zero-support-high-id-does-not-win");
    check(map.sampleId(11,11,4)==0,"support-only-sums-same-material-neighbours");
    map.ids[11*TerrainMaterialMap::Side+10]=2;
    check(map.sampleId(11,11,4)==2,"support-only-higher-id-wins-exact-tie");
    check(map.sampleId(11,10.99,4)==0,"support-only-higher-id-cannot-win-near-tie");
    map.ids[11*TerrainMaterialMap::Side+10]=0;
    map.ids[11*TerrainMaterialMap::Side+11]=255;
    check(map.sampleId(11,11,4)==0,"support-only-ignores-id-magnitude");
    map.ids[11*TerrainMaterialMap::Side+11]=3;
    int scattered=0; bool deterministic=true,existingOnly=true;
    for (quint32 seed=0;seed<10000;++seed) {
        const int selected=map.sampleId(11,11,2,seed);
        scattered+=selected==3;
        existingOnly &= selected==0 || selected==3;
        deterministic &= selected==map.sampleId(11,11,2,seed);
    }
    check(deterministic && existingOnly,"scattering-is-deterministic-and-never-invents-ids");
    check(scattered>2300 && scattered<2700,"scattering-follows-distance-probability-not-id-priority");
    check(map.sampleId(11,11,3)==3 && map.sampleId(10.99,10.99,3)==0,"nearest-mode-preserves-original-cell-boundary");
    for (int mode : {1,2,3,4})
        check(map.sampleId(0,0,mode)==0 && map.sampleId(TerrainMaterialMap::Side,TerrainMaterialMap::Side,mode)==0,
              "sampling-clamps-tile-boundaries");
    const int patchSide=TerrainMaterialMap::Side/32;
    for (int mode : {1,2,3,4}) {
        map.initialize();
        const QByteArray before=map.patchKey(1,32,mode);
        const auto touched=map.paint(patchSide-.5,patchSide-.5,.4,1,32,QImage(),{1},false,mode);
        check(touched==(mode==3 ? QSet<int>{0} : QSet<int>{0,1,32,33}),"filter-halo-invalidates-edge-and-corner-neighbours");
        check(map.at(patchSide,patchSide-1)==0,"halo-refresh-does-not-edit-locked-neighbour");
        check((map.patchKey(1,32,mode)!=before)==(mode!=3),"recipe-key-includes-filter-halo");
    }
    map.initialize();
    check(map.patchKey(0,32,1)!=map.patchKey(0,32,2)
          && map.patchKey(0,32,2)!=map.patchKey(0,32,3)
          && map.patchKey(0,32,4)!=map.patchKey(0,32,1)
          && map.patchKey(0,32,4)!=map.patchKey(0,32,2)
          && map.patchKey(0,32,4)!=map.patchKey(0,32,3),"recipe-keys-separate-sampling-modes");
    check(map.patchKey(0,32,1)==map.patchKey(33,32,1),"uniform-halo-clamp-retains-recipe-sharing");
    // Diagonal two-material border: generated RGB must still come from exactly
    // one source. Record a small, comparable synthesis-only timing for each mode.
    for (int z=0;z<patchSide+1;++z) for(int x=0;x<patchSide+1;++x)
        map.ids[z*TerrainMaterialMap::Side+x]=char(x>z);
    const QByteArray storedIds=map.ids;
    for (int mode : {1,2,3,4}) {
        const QImage generated=map.generate(0,32,sources,mode);
        generated.save(QString("build/terrain-material-sampling-%1.png").arg(mode));
        bool unblended=true;
        for (int z=0;z<generated.height();++z) for(int x=0;x<generated.width();++x) {
            const QRgb color=generated.pixel(x,z);
            unblended &= color==qRgb(255,0,0) || color==qRgb(0,0,255);
        }
        check(unblended && generated==map.generate(0,32,sources,mode) && map.ids==storedIds,
              "generation-is-unblended-repeatable-and-preserves-id-map");
        double elapsed=0;
        for (int run=0;run<5;++run) {
            QElapsedTimer timer; timer.start();
            const QImage measured=map.generate(0,32,sources,mode);
            elapsed+=timer.nsecsElapsed()/1e6;
            if (measured.isNull()) ++failed;
        }
        qInfo() << "[tests:terrain-material] sampling mode" << mode << "P32 output" << TerrainMaterialMap::OutputSide
                << "mean generation ms (5 warm runs)" << elapsed/5;
    }
    // Patch fill replaces mixed IDs; flood fill only follows the clicked ID.
    map.initialize();
    map.ids[2*TerrainMaterialMap::Side+2]=1;
    map.ids[3*TerrainMaterialMap::Side+3]=1;
    check(map.fill(2,2,2,32,false,{},false,3)==QSet<int>{0}
          && map.at(2,2)==2 && map.at(3,3)==1,"flood-fill-is-four-connected-not-diagonal");
    const QByteArray beforeFill=map.ids;
    check(!map.fill(4,4,7,32,true,{},true).isEmpty() && map.ids==beforeFill,"patch-fill-dry-run-preserves-data");
    check(!map.fill(4,4,7,32,false,{},true).isEmpty() && map.ids==beforeFill,"flood-fill-dry-run-preserves-data");
    check(map.fill(2,2,2,32,false).isEmpty(),"flood-fill-same-id-no-op");
    check(map.fill(4,4,7,32,true,{0}).isEmpty() && map.ids==beforeFill,"locked-patch-fill-no-op");
    check(map.fill(-1,0,7,32,false).isEmpty() && map.fill(TerrainMaterialMap::Side,0,7,32,true).isEmpty()
          && map.fill(0,0,256,32,true).isEmpty(),"fill-rejects-outside-tile-and-invalid-id");
    check(map.fill(4,4,7,32,true,{},false,3)==QSet<int>{0},"patch-fill-changes-only-clicked-patch");
    bool fullPatch=true;
    for (int z=0;z<patchSide;++z) for(int x=0;x<patchSide;++x) fullPatch &= map.at(x,z)==7;
    check(fullPatch && map.at(patchSide,0)==0 && map.fill(4,4,7,32,true).isEmpty(),"patch-fill-replaces-all-original-ids-and-is-idempotent");
    for (int mode : {1,2,3,4}) {
        map.initialize();
        check(map.fill(0,0,1,32,true,{},false,mode)==(mode==3 ? QSet<int>{0} : QSet<int>{0,1,32,33}),
              "patch-fill-includes-filter-halo");
    }
    map.initialize();
    QElapsedTimer fillTimer; fillTimer.start();
    const auto wholeTile=map.fill(0,0,255,32,false);
    qInfo() << "[tests:terrain-material] whole-tile flood ID edit ms" << fillTimer.nsecsElapsed()/1e6;
    check(wholeTile.size()==1024 && map.usedIds()==QSet<int>{255},"flood-fill-reaches-all-tile-borders-without-recursion");
    map.initialize();
    QSet<int> barrier;
    for(int pz=0;pz<32;++pz) barrier.insert(pz*32+1);
    map.fill(0,0,1,32,false,barrier);
    check(map.at(patchSide-1,TerrainMaterialMap::Side-1)==1
          && map.at(patchSide,0)==0 && map.at(2*patchSide,0)==0,"locked-patches-block-flood-propagation");
    // Compare the scanline algorithm with a simple reference traversal on small
    // randomly shaped regions enclosed by a different ID.
    quint32 fillRng=12345;
    for (int trial=0;trial<8;++trial) {
        map.initialize(255);
        QByteArray expected(32*32,'\0');
        for(int z=0;z<32;++z) for(int x=0;x<32;++x) {
            fillRng=fillRng*1664525u+1013904223u;
            expected[z*32+x]=char((fillRng>>29)%2);
            map.ids[z*TerrainMaterialMap::Side+x]=expected[z*32+x];
        }
        const char target=expected[16*32+16];
        QVector<int> pending{16*32+16}; expected[16*32+16]=2;
        while(!pending.isEmpty()) {
            const int p=pending.takeLast(),x=p%32,z=p/32;
            for(int next : {x>0?p-1:-1,x<31?p+1:-1,z>0?p-32:-1,z<31?p+32:-1})
                if(next>=0 && expected[next]==target) { expected[next]=2; pending.push_back(next); }
        }
        map.fill(16,16,2,32,false);
        bool same=true;
        for(int z=0;z<32;++z) for(int x=0;x<32;++x) same &= map.at(x,z)==quint8(expected[z*32+x]);
        check(same && map.at(32,0)==255,"scanline-fill-matches-reference-connected-region");
    }
    const auto bc1=TerrainMaterialMap::encodeBC1(red);
    check(bc1.size()==32768,"bc1-patch-size");
    Texture decodedTexture;
    decodedTexture.width=decodedTexture.height=256;
    decodedTexture.type=GL_RGB;
    decodedTexture.compressedData=bc1; decodedTexture.compressedGLFormat=0x83F0;
    check(decodedTexture.decodeToCpu() && decodedTexture.imageData[0]>245
          && decodedTexture.imageData[1]<10 && decodedTexture.imageData[2]<10,"bc1-existing-decoder-no-gl");
    delete[] decodedTexture.imageData;
    check(TerrainMaterialMap::textureKey(red,true)!=TerrainMaterialMap::textureKey(red,false)
          && TerrainMaterialMap::textureKey(red,true)!=TerrainMaterialMap::textureKey(blue,true),"global-output-key-includes-encoding-and-color");
    QTemporaryDir temp;
    const int worldPoint32=32*TerrainMaterialMap::Side/2048;
    check(temp.isValid(),"temporary-fixture");
    QScopedValueRollback<QString> root(Game::root,temp.path());
    QScopedValueRollback<QString> route(Game::route,QString("proc-test"));
    QScopedValueRollback<bool> write(Game::writeEnabled,true);
    QScopedValueRollback<bool> seasonal(Game::seasonalEditing,false);
    // QTemporaryDir names can contain uppercase letters on case-sensitive hosts.
    QScopedValueRollback<bool> caseFolding(Game::caseInsensitiveFS,false);
    const QString tileDir=temp.path()+"/routes/proc-test/tiles";
    QDir().mkpath(tileDir);
    red.save(temp.path()+"/red.png"); blue.save(temp.path()+"/blue.png");
    {
        // Independent ACE row-table reader: the permissive legacy loader ignores
        // those offsets, so its successful round trip alone cannot verify them.
        QImage rgb(8,4,QImage::Format_RGB888);
        for (int y=0;y<4;++y) for (int x=0;x<8;++x) rgb.setPixelColor(x,y,QColor(x*27,y*53,(x+y)*19));
        const QString path=temp.path()+"/checked.ace";
        check(AceLib::save(path,rgb,AceWriteOptions{},error),"checked-rgb-ace-write");
        QFile file(path); check(file.open(QIODevice::ReadOnly),"checked-ace-open-for-independent-read"); const auto bytes=file.readAll(); file.close();
        bool correct=bytes.size()==216+4*4+8*4*3 && bytes.startsWith("SIMISA@@@@@@@@@@");
        for (int y=0;y<4 && correct;++y) {
            const quint32 row=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData()+216+4*y));
            correct &= row==200+4*4+y*8*3;
            for (int x=0;x<8 && correct;++x) for (int c=0;c<3;++c)
                correct &= quint8(bytes[16+row+c*8+x])==rgb.constScanLine(y)[3*x+c];
        }
        check(correct,"independent-ace-offset-table-and-planar-rgb-payload");
        Texture texture(path); texture.editable=true; AceLib loader; loader.texture=&texture; loader.run();
        check(texture.loaded && texture.width==8 && texture.height==4 && texture.imageData
              && !memcmp(texture.imageData,rgb.constBits(),96),"checked-ace-new-reader-round-trip");
        delete[] texture.imageData; texture.imageData=nullptr;
        check(!AceLib::save(temp.path(),rgb,AceWriteOptions{},error),"checked-ace-reports-directory-write-failure");
        check(!AceLib::save(path,QImage(),AceWriteOptions{},error),"checked-ace-rejects-empty-image-without-overwriting");
        check(file.open(QIODevice::ReadOnly) && file.readAll()==bytes,"checked-ace-failure-preserves-existing-file");
    }
    for (int p : {4,8,16,32}) {
        const int n=p==4?128:(p==8?256:(p==16?1024:2048));
        TestTerrain baked; baked.setup(temp.path(),p,QString("baked%1").arg(p),n,2048/n);
        const auto source0=TerrainMaterialSource::capture(baked.descriptor(),0);
        baked.descriptor().flags[0]=0xc3;
        baked.descriptor().errorBias[0]=0.73f;
        check(baked.setProceduralMaterial(true,error),"bake-enable-reserves-material-zero");
        check(TerrainMaterialSource::capture(baked.descriptor(),1)->key()==source0->key()
              && baked.descriptor().materialsCount==3,"bake-conversion-preserves-complete-source-pair");
        check(!baked.setProceduralMaterial(false,error),"first-disable-requires-successful-bake");
        QElapsedTimer timer; timer.start();
        check(baked.save(),"bake-save-with-no-resident-near-textures");
        qInfo() << "[tests:terrain-material] bake/save P" << p << "ms" << timer.nsecsElapsed()/1e6;
        TFile disk; disk.readT(tileDir+"/"+baked.name+".t");
        bool mapping=disk.bakedMaterialInfo.startsWith("v1:") && disk.bakedMaterialInfo!="v1:pending";
        for(int patch=0;patch<p*p;++patch) {
            const float *d=disk.tdata+13*patch;
            mapping &= d[6]==0 && d[7]==float(patch%p)/p && d[8]==float(patch/p)/p
                    && d[9]==1.0f/n && d[12]==1.0f/n && d[10]==0 && d[11]==0;
        }
        float detail; memcpy(&detail,&disk.materials[0].itex[1][3],4);
        check(mapping && detail==32*p,"baked-descriptor-uv-and-microtex-density-all-patches");
        check(disk.flags[0]==0xc3 && disk.errorBias[0]==0.73f,"bake-preserves-hidden-hole-water-and-error-bias");
        check(baked.proceduralResidentPatchCount()==0,"baking-does-not-populate-near-output-cache");
        const QString ace=temp.path()+"/"+baked.name+"_procedural.ace";
        QFile savedAce(ace); check(savedAce.open(QIODevice::ReadOnly),"open-saved-tile-bake");
        const auto aceBytes=savedAce.readAll(); savedAce.close();
        const int bakeSide=TerrainMaterialMap::BakedSide;
        check(aceBytes.size()==216+bakeSide*4+bakeSide*bakeSide*3,"bake-uncompressed-rgb-file-size");
        check(baked.save() && !QFileInfo::exists(ace+".bk"),"unchanged-save-reuses-bake-without-backup-rotation");
        if (p==4) {
            // More tiles than workers: rejected initial requests must retry
            // without any visibility/preparation/fallback call from a renderer.
            std::vector<std::unique_ptr<TestTerrain>> waiting;
            for (int i=0;i<6;++i) {
                auto tile=std::make_unique<TestTerrain>();
                tile->setup(temp.path(),p,baked.name,n,2048/n);
                tile->descriptor().readT(tileDir+"/"+baked.name+".t");
                tile->loadProceduralMaterial(tileDir);
                waiting.push_back(std::move(tile));
            }
            check(Terrain::proceduralWorkStats().outstanding==4,
                  "tile-load-bake-prefetch-respects-four-job-limit");
            check(finishMaterialJobs(),"tile-load-bake-prefetch-retries-without-rendering");
            bool allReady=true;
            for (const auto &tile : waiting)
                allReady &= tile->proceduralFallbackTexture()>=0 && tile->proceduralResidentPatchCount()==0;
            check(allReady && Terrain::proceduralWorkStats().outstanding==0,
                  "all-loaded-tiles-have-bake-before-first-patch-request");
            for (const auto &tile : waiting) tile->releaseProceduralTextures();
            Terrain::beginProceduralFrame();
            check(Terrain::proceduralWorkStats().outstanding==0,
                  "released-bake-prefetch-is-not-restarted-by-frame-pump");
            QImage oldBake(2048,2048,QImage::Format_RGB888); oldBake.fill(Qt::red);
            check(AceLib::save(ace,oldBake,AceWriteOptions{},error),"write-earlier-2048-bake-fixture");
            baked.loadProceduralMaterial(tileDir);
            check(finishMaterialJobs(),"earlier-bake-validation-worker-completes");
            check(baked.proceduralFallbackTexture()<0 && !baked.hasProceduralBake(),
                  "earlier-2048-bake-is-rejected-without-resampling");
            baked.releaseProceduralTextures();
            check(baked.save(),"save-regenerates-rejected-bake-at-current-size");
        }
        TestTerrain::PatchVisibility view; view.valid=true; view.maximumDistance=100000;
        view.cameraLocalX=view.cameraLocalZ=1024;
        check(baked.proceduralNearCamera(view),"bake-near-camera-cell-uses-procedural");
        view.cameraLocalX=4096; view.cameraLocalZ=1024;
        check(!baked.proceduralNearCamera(view),"bake-outside-three-by-three-uses-static");
        Terrain::beginProceduralFrame(); baked.prepareVisibleProceduralTextures(view);
        check(baked.proceduralTexture(0,true)<0 && baked.proceduralResidentPatchCount()==0
              && Terrain::proceduralWorkStats().outstanding<=1,
              "far-bake-does-not-queue-near-generation");
        check(finishMaterialJobs() && baked.proceduralFallbackTexture()>=0,
              "far-bake-loads-through-bounded-owned-file-job");
        const int bakeId=baked.proceduralFallbackTexture();
        baked.releaseProceduralTextures();
        check(!TexLib::mtex.count(bakeId),"far-bake-cache-releases-with-tile-residency");
        if (p==4) {
            QFile::rename(ace,ace+".missing-test");
            baked.proceduralFallbackTexture();
            check(finishMaterialJobs() && baked.proceduralFallbackTexture()<0
                  && baked.proceduralNearCamera(view),"missing-bake-file-job-falls-back-to-procedural");
            check(baked.save() && QFileInfo::exists(ace),"save-regenerates-missing-or-failed-bake");
        }
        const auto remap=baked.proceduralTextureRemap(p*p-1,-1);
        check(remap==QVector3D(1.0f/p-1.0f,float(p-1)/p,float(p-1)/p)
              && baked.proceduralTextureRemap(0,42).isNull(),"fallback-uv-remap-only-when-bake-drawn");
        baked.spacing(4096.0f/n); view.cameraLocalX=5000; view.cameraLocalZ=1024;
        check(baked.proceduralNearCamera(view),"large-terrain-overlapping-near-region-is-procedural");
        baked.spacing(2048.0f/n);
        check(baked.setProceduralMaterial(false,error) && baked.save(),"disable-saves-baked-appearance");
        check(baked.setProceduralMaterial(true,error) && baked.descriptor().materialsCount==3,
              "reenable-does-not-reserve-another-bake-slot");
        check(baked.proceduralNearCamera(view),"unsaved-procedural-edits-do-not-use-old-far-bake");
    }
    const int texturesBefore=TexLib::mtex.size();
    {
        const QString sourcePath=temp.path()+"/source-refresh.png";
        red.save(sourcePath);
        TestTerrain t; t.setup(temp.path(),16,"source-refresh");
        *t.descriptor().materials[0].tex[0]="source-refresh.png";
        check(t.setProceduralMaterial(true,error) && t.save(),"save-initial-source-stamp-bake");
        t.proceduralTexture(0); // Retain an old decoded/generated source deliberately.
        blue.save(sourcePath);
        check(t.save(),"changed-source-file-triggers-rebake");
        Texture baked(temp.path()+"/source-refresh_procedural.ace");
        AceLib loader; loader.texture=&baked; loader.run();
        const int nearId=t.proceduralTexture(0);
        auto *near=TexLib::mtex.at(nearId); near->decodeToCpu();
        check(baked.loaded && baked.imageData && baked.imageData[2]==255 && baked.imageData[0]==0
              && near->imageData && near->imageData[2]>245 && near->imageData[0]<10,
              "rebake-and-near-cache-use-fresh-source-not-new-stamp-with-old-pixels");
        delete[] baked.imageData; baked.imageData=nullptr;
    }
    {
        TestTerrain full; full.setup(temp.path(),16,"full-conversion");
        full.descriptor().materialsCount=256;
        check(!full.setProceduralMaterial(true,error) && full.descriptor().bakedMaterialInfo.isEmpty()
              && full.descriptor().materialsCount==256 && !full.usesProceduralMaterial(),
              "full-old-palette-conversion-refused-before-mutation");
    }
    {
        TerrainMaterialMap old; variedMaterialMap(old);
        const auto before=old.ids;
        old.write(tileDir+"/old-demo.pmap",error);
        TestTerrain migrated; migrated.setup(temp.path(),16,"old-demo");
        migrated.descriptor().sampleMaterialBuffer="old-demo.pmap";
        migrated.loadProceduralMaterial(tileDir);
        TerrainMaterialMap onDisk; onDisk.read(tileDir+"/old-demo.pmap",error);
        check(migrated.rendersProceduralMaterial() && migrated.descriptor().materialsCount==3
              && onDisk.ids==before && migrated.isModified(),"old-demo-migration-is-memory-only-until-save");
        check(migrated.save() && onDisk.read(tileDir+"/old-demo_materials.pmap",error),"old-demo-migration-saves");
        bool shifted=onDisk.ids.size()==before.size();
        for (int i=0;i<before.size() && shifted;++i) shifted &= quint8(onDisk.ids[i])==quint8(before[i])+1;
        check(shifted,"old-demo-migration-shifts-every-id-exactly-once");
        const auto count=migrated.descriptor().materialsCount;
        migrated.loadProceduralMaterial(tileDir);
        check(migrated.rendersProceduralMaterial() && migrated.descriptor().materialsCount==count,
              "saved-marker-prevents-second-migration");
    }
    {
        TerrainMaterialMap plane; plane.initialize();
        plane.fill(TerrainMaterialMap::Side-1,TerrainMaterialMap::Side-1,1,16,true);
        const auto bake=plane.bake(16,sources);
        check(!bake.isNull() && bake.pixelColor(0,0)==QColor(Qt::red)
              && bake.pixelColor(TerrainMaterialMap::BakedSide-1,TerrainMaterialMap::BakedSide-1)==QColor(Qt::blue)
              && bake.pixelColor(TerrainMaterialMap::BakedSide-1,0)==QColor(Qt::red),"bake-patch-row-orientation-and-complete-coverage");
        QImage checker(512,512,QImage::Format_RGB888);
        for(int y=0;y<512;++y) for(int x=0;x<512;++x) checker.setPixelColor(x,y,((x+y)&1)?Qt::white:Qt::black);
        plane.initialize();
        const auto reduced=plane.bake(16,{{0,checker}});
        check(!reduced.isNull() && abs(reduced.pixelColor(32,32).red()-128)<=1,
              "bake-minifies-near-rgb-instead-of-selecting-one-id-or-colour");
    }
    {
        TestTerrain filled,neighbour;
        filled.setup(temp.path(),16,"filled"); neighbour.setup(temp.path(),16,"fill-neighbour");
        neighbour.mojex=1;
        TestMaterialTerrainLib lib; lib.first=&filled; lib.second=&neighbour;
        QString error;
        filled.setProceduralMaterial(true,error); neighbour.setProceduralMaterial(true,error);
        Texture source(temp.path()+"/blue.png");
        Brush brush; brush.useTexture=true; brush.tex=&source;
        QImage white(3,3,QImage::Format_Grayscale8); white.fill(255); brush.brushshape=&white;
        const QByteArray staticBytes=filled.staticDescriptorBytes();
        const int original=filled.proceduralTexture(0), other=neighbour.proceduralTexture(0);
        filled.setModified(false);
        filled.paintTexture(&brush,0,0,-992,-992);
        brush.useTexture=false; filled.paintTexture(&brush,0,0,-992,-992); brush.useTexture=true;
        filled.setTexture(&brush,0,0,-992,-992);
        check(!filled.isModified() && filled.proceduralTexture(0)==original
              && filled.staticDescriptorBytes()==staticBytes,"static-texture-colour-and-put-silently-ignore-procedural-tiles");
        float point[]={-992,0,-992};
        filled.lock(0); lib.paintProceduralTexture(&brush,0,0,point,TerrainMaterialMap::FillPatch);
        check(!filled.isModified(),"procedural-fill-respects-shared-patch-lock");
        filled.lock(0,false);
        Game::writeEnabled=false;
        lib.paintProceduralTexture(&brush,0,0,point,TerrainMaterialMap::FillPatch);
        check(!filled.isModified(),"procedural-fill-respects-global-write-protection");
        Game::writeEnabled=true;
        lib.paintProceduralTexture(&brush,0,0,point,TerrainMaterialMap::FillPatch);
        check(filled.isModified() && filled.proceduralTexture(0)!=original,
              "procedural-fill-patch-ignores-white-mask");
        filled.save(); TerrainMaterialMap saved;
        check(saved.read(tileDir+"/"+filled.descriptor().sampleMaterialBuffer,error)
              && saved.at(0,0)==2 && saved.at(TerrainMaterialMap::Side/16-1,TerrainMaterialMap::Side/16-1)==2
              && saved.at(TerrainMaterialMap::Side/16,0)==1,"runtime-patch-fill-saves-full-patch-only");
        // Fill the remaining zero region from the next patch, not the blue patch.
        point[0]=-864; lib.lookups=0;
        lib.paintProceduralTexture(&brush,0,0,point,TerrainMaterialMap::FloodFill);
        check(lib.lookups==1 && neighbour.proceduralTexture(0)==other,
              "runtime-flood-fill-does-not-cross-tile-boundary");
        check(filled.proceduralTexture(17)==filled.proceduralTexture(255),"fill-reuses-identical-private-output-recipes");
        check(filled.save() && saved.read(tileDir+"/"+filled.descriptor().sampleMaterialBuffer,error)
              && saved.usedIds()==QSet<int>{2},"runtime-flood-fill-save-reload");
        filled.setProceduralMaterial(false,error); filled.setModified(false);
        lib.paintProceduralTexture(&brush,0,0,point,TerrainMaterialMap::FloodFill);
        check(!filled.isModified(),"procedural-tools-ignore-static-tiles");
        filled.setProceduralMaterial(true,error);
        neighbour.descriptor().materials[1].atex[0][0]=19; // Distinct complete source shader, picked from ID plane.
        brush.texId=73;
        neighbour.rememberProceduralSource(&brush,1,0,-992,-992);
        const int paletteBefore=filled.descriptor().materialsCount;
        lib.paintProceduralTexture(&brush,0,0,point,TerrainMaterialMap::FillPatch);
        check(filled.descriptor().materialsCount==paletteBefore+1
              && filled.descriptor().materials[paletteBefore].atex[0][0]==19,
              "fill-patch-imports-picked-shader-with-white-brush-mask");
        lib.paintProceduralTexture(&brush,0,0,point,TerrainMaterialMap::FillPatch);
        check(filled.descriptor().materialsCount==paletteBefore+1,"repeated-fill-reuses-imported-shader");
    }
    {
        TestTerrain a,b;
        a.setup(temp.path(),16,"testa"); b.setup(temp.path(),32,"testb");
        a.descriptor().setPatchValue(0,TFile::PatchField::TextureX,0.123f);
        a.descriptor().setPatchValue(0,TFile::PatchField::TextureB,0.037f);
        a.descriptor().sampleASbuffer.present=true;
        a.descriptor().sampleASbuffer.payload="preserved AS payload";
        const int count=a.descriptor().materialsCount+1; // Reserved bake plus existing source palette.
        const float uv=a.descriptor().patchValue(0,TFile::PatchField::TextureW);
        check(a.setProceduralMaterial(true,error) && b.setProceduralMaterial(true,error),"enable-existing-shader-zero");
        const QByteArray originalStatic=a.staticDescriptorBytes();
        check(a.proceduralResidentPatchCount()==0 && b.proceduralResidentPatchCount()==0,
              "enable-does-not-generate-unseen-patches");
        const int texture=a.proceduralTexture(0);
        check(a.proceduralResidentPatchCount()==1 && b.proceduralResidentPatchCount()==0,
              "first-request-generates-only-requested-patch");
        check(texture==a.proceduralTexture(255) && texture==b.proceduralTexture(1023),"whole-tile-and-cross-tile-output-sharing");
        check(a.setProceduralMaterial(true,error) && a.proceduralTexture(0)==texture,"enable-idempotent");
        const auto params=TerrainMeshPaged::terrainParams(a,0);
        check(params.uvAndOriginX[0]==1.0f/16 && params.uvAndOriginX[2]==0,"runtime-default-uv");
        Texture source(temp.path()+"/blue.png");
        Brush brush; brush.useTexture=true; brush.tex=&source; brush.brushshape=&mask;
        // World local(-992,-992) is tile ID-plane(32,32).
        a.setModified(false);
        a.paintProceduralMaterial(&brush,0,0,-992,-992,12);
        check(a.proceduralResidentPatchCount()==2,"painting-replaces-changed-output-synchronously");
        const int privateTexture=a.proceduralTexture(0);
        check(privateTexture!=texture && a.isModified() && b.proceduralTexture(0)==texture,"copy-on-write-does-not-change-shared-neighbour");
        a.setModified(false);
        a.paintProceduralMaterial(&brush,0,0,-992,-992,12);
        check(!a.isModified() && a.proceduralTexture(0)==privateTexture,"runtime-no-op-retains-material");
        check(a.descriptor().materialsCount==count && a.descriptor().patchValue(0,TFile::PatchField::TextureW)==uv
              && a.descriptor().patchValue(0,TFile::PatchField::ShaderIndex)==1,"paint-preserves-converted-descriptor-until-save");
        check(a.staticDescriptorBytes()==originalStatic,"all-static-fields-shaders-uv-and-opaque-buffers-byte-preserved");
        {
            TestTerrain other; other.setup(temp.path(),16,"reordered");
            other.descriptor().materials[0].tex[0]=new QString("blue.png");
            other.descriptor().materials[1].tex[0]=new QString("red.png");
            check(other.setProceduralMaterial(true,error),"enable-reordered-target-shaders");
            Texture redSource(temp.path()+"/red.png");
            Brush picked; picked.useTexture=true;picked.tex=&redSource;picked.texId=42;picked.brushshape=&mask;
            a.rememberProceduralSource(&picked,0,0,-700,-700);
            other.paintProceduralMaterial(&picked,0,0,-992,-992,12);
            check(other.save(),"save-cross-tile-remapped-paint");
            TerrainMaterialMap remapped;
            check(remapped.read(tileDir+"/"+other.descriptor().sampleMaterialBuffer,error) && remapped.at(worldPoint32,worldPoint32)==2,
                  "picked-source-resolves-different-id-on-another-tile");
        }
        a.descriptor().removeMat(1);
        {
            Texture pickedTexture(temp.path()+"/blue.png");
            Brush picked; picked.useTexture=true; picked.tex=&pickedTexture; picked.texId=89;
            QImage importMask(9,9,QImage::Format_Grayscale8); importMask.fill(255); picked.brushshape=&importMask;
            QString pickedKey;
            {
                TestTerrain palette; palette.setup(temp.path(),16,"palette");
                // Source IDs may exceed 255; only the destination bitmap ID is 8-bit.
                palette.descriptor().materials[300]=palette.descriptor().materials[1];
                palette.descriptor().amaterials[300]=palette.descriptor().amaterials[1];
                palette.descriptor().materialsCount=301;
                palette.descriptor().materials[300].count153=2;
                palette.descriptor().materials[300].atex[1][1]=7;
                palette.descriptor().amaterials[300].itex[0][3]=12345;
                palette.descriptor().setPatchValue(0,TFile::PatchField::ShaderIndex,300);
                palette.rememberProceduralSource(&picked,0,0,-992,-992);
                check(bool(picked.terrainShaderSource),"picked-complete-shader-snapshot");
                if (picked.terrainShaderSource) pickedKey=picked.terrainShaderSource->key();
                *palette.descriptor().materials[300].tex[0]="red.png";
            }
            check(picked.terrainShaderSource && picked.terrainShaderSource->normal.textures[0]=="blue.png",
                  "snapshot-survives-palette-mutation-and-unload");
            TestTerrain target; target.setup(temp.path(),32,"imported");
            *target.descriptor().materials[1].tex[0]="red.png";
            check(target.setProceduralMaterial(true,error),"enable-import-target");
            target.setModified(false);
            target.paintProceduralMaterial(&picked,0,0,-992,-992,12);
            check(target.descriptor().materialsCount==3 && !target.isModified(),"white-stroke-does-not-import");
            importMask.fill(0); target.lock(0);
            target.paintProceduralMaterial(&picked,0,0,-992,-992,12);
            check(target.descriptor().materialsCount==3 && !target.isModified(),"locked-stroke-does-not-import");
            target.lock(0,false); Game::writeEnabled=false;
            target.paintProceduralMaterial(&picked,0,0,-992,-992,12);
            check(target.descriptor().materialsCount==3 && !target.isModified(),"read-only-stroke-does-not-import");
            Game::writeEnabled=true;
            target.paintProceduralMaterial(&picked,0,0,-992,-992,12);
            const auto imported=TerrainMaterialSource::capture(target.descriptor(),3);
            check(target.descriptor().materialsCount==4 && imported && imported->key()==pickedKey && target.isModified(),
                  "paint-appends-full-normal-and-auxiliary-shader-pair");
            check(target.descriptor().patchValue(0,TFile::PatchField::ShaderIndex)==1
                  && *target.descriptor().materials[1].tex[0]=="red.png", "import-preserves-converted-source-indices-and-shaders");
            target.paintProceduralMaterial(&picked,0,0,-960,-992,12);
            check(target.descriptor().materialsCount==4,"repeated-strokes-reuse-imported-shader");
            check(target.save(),"save-imported-shader-palette");
            TFile importedFile;
            check(importedFile.readT(tileDir+"/imported.t"),"reload-imported-descriptor");
            const auto restored=TerrainMaterialSource::capture(importedFile,3);
            check(restored && restored->key()==pickedKey && importedFile.materialsCount==4,"imported-shader-pair-round-trip");
            TerrainMaterialMap importedMap;
            check(importedMap.read(tileDir+"/"+importedFile.sampleMaterialBuffer,error) && importedMap.at(worldPoint32,worldPoint32)==3,
                  "imported-local-id-round-trip");
            target.loadProceduralMaterial(tileDir);
            check(target.rendersProceduralMaterial(),"imported-material-generates-after-reload");
            TFile capacity; capacity.initNew("capacity",256,8,16); capacity.materialsCount=255;
            if (picked.terrainShaderSource) {
                check(picked.terrainShaderSource->appendTo(capacity,error)==255,"last-8-bit-shader-slot-is-usable");
                check(picked.terrainShaderSource->appendTo(capacity,error)==-1 && capacity.materialsCount==256,
                      "full-palette-refused-without-replacement");
            }
        }
        check(a.descriptor().materialsCount==count,"shader-renumbering-gated");
        check(a.save(),"save-sidecar-and-tfile");
        check(a.proceduralTexture(0)!=privateTexture,"save-finalizes-private-material");
        TFile roundtrip;
        check(roundtrip.readT(tileDir+"/testa.t") && !roundtrip.sampleMaterialBuffer.isEmpty()
              && roundtrip.materialsCount==count && roundtrip.patchValue(0,TFile::PatchField::TextureW)==1.0f/256
              && roundtrip.bakedMaterialInfo.startsWith("v1:"),"tfile-extensions-and-baked-uv-round-trip");
        TerrainMaterialMap saved;
        check(saved.read(tileDir+"/"+roundtrip.sampleMaterialBuffer,error) && saved.at(worldPoint32,worldPoint32)==2,"sidecar-reloads-painted-ids");
        check(QFileInfo::exists(temp.path()+"/testa_procedural.ace"),"single-baked-ace-in-texture-directory");
        const QString stable = tileDir+"/testa_materials.pmap";
        const QString backup = stable+".bk";
        check(a.descriptor().sampleMaterialBuffer=="testa_materials.pmap", "stable-map-filename");
        const QByteArray firstIds = saved.ids;
        auto *oldFileTexture=new Texture(temp.path()+"/testa_procedural.ace");
        const QString oldFilePath=oldFileTexture->pathid;
        const int oldFileId=TexLib::addTex(oldFileTexture);
        a.paintProceduralMaterial(&brush,0,0,-960,-992,12);
        check(a.save() && saved.read(backup,error) && saved.ids==firstIds,
              "save-keeps-previous-map-in-one-backup");
        check(TexLib::getTex(oldFilePath)==-1 && oldFileTexture->pathid==oldFilePath
              && !oldFileTexture->loaded,"rebake-retires-file-alias-without-mutating-loader-state");
        TexLib::mtex.erase(oldFileId); delete oldFileTexture; // This mock has no worker or GL storage.
        check(saved.read(stable,error) && saved.ids!=firstIds,"save-replaces-current-map");
        const QByteArray secondIds = saved.ids;
        a.paintProceduralMaterial(&brush,0,0,-928,-992,12);
        check(a.save() && saved.read(backup,error) && saved.ids==secondIds,
              "subsequent-save-replaces-backup");
        check(QDir(tileDir).entryList({"testa_materials*"},QDir::Files).size()==2,
              "repeated-saves-keep-only-map-and-bk");
        saved.read(stable,error);
        const QByteArray lastSavedIds = saved.ids;
        QFile lastBake(temp.path()+"/testa_procedural.ace"); check(lastBake.open(QIODevice::ReadOnly),"open-bake-before-failure-test");
        const auto lastBakeBytes=lastBake.readAll(); lastBake.close();
        const QString lastBakeInfo=a.descriptor().bakedMaterialInfo;
        // Fail the descriptor replacement after a successful map replacement.
        QFile::rename(tileDir+"/testa.t",tileDir+"/testa-test-original.t");
        QDir().mkdir(tileDir+"/testa.t");
        a.paintProceduralMaterial(&brush,0,0,-896,-992,12);
        check(!a.save() && saved.read(stable,error) && saved.ids==lastSavedIds
              && saved.read(backup,error) && saved.ids==lastSavedIds,
              "descriptor-failure-restores-current-map-and-retains-backup");
        check(lastBake.open(QIODevice::ReadOnly),"open-restored-bake");
        check(lastBake.readAll()==lastBakeBytes && a.descriptor().bakedMaterialInfo==lastBakeInfo,
              "descriptor-failure-also-restores-ace-and-bake-signature"); lastBake.close();
        QDir().rmdir(tileDir+"/testa.t");
        QFile::rename(tileDir+"/testa-test-original.t",tileDir+"/testa.t");
        // Fail map creation itself after rotating the old map to the backup.
        a.name="mapfailure";
        QDir().mkdir(tileDir+"/mapfailure_materials.pmap");
        check(!a.save() && saved.read(stable,error) && saved.ids==lastSavedIds
              && saved.read(tileDir+"/mapfailure_materials.pmap.bk",error) && saved.ids==lastSavedIds,
              "map-write-failure-immediately-restores-old-map");
        a.name="testa";
        // Refuse a blocked backup without touching the current map.
        QFile::rename(backup,backup+"-test-original"); QDir().mkdir(backup);
        check(!a.save() && saved.read(stable,error) && saved.ids==lastSavedIds,
              "backup-rotation-failure-preserves-current-map");
        QDir().rmdir(backup); QFile::rename(backup+"-test-original",backup);
        // Block ACE backup rotation: the map/descriptor must not be touched.
        const QString aceBackup=temp.path()+"/testa_procedural.ace.bk";
        QFile::rename(aceBackup,aceBackup+"-test-original"); QDir().mkdir(aceBackup);
        check(!a.save() && saved.read(stable,error) && saved.ids==lastSavedIds,
              "ace-backup-failure-does-not-change-authoritative-map");
        QDir().rmdir(aceBackup); QFile::rename(aceBackup+"-test-original",aceBackup);
        check(a.save(),"retry-after-failed-save-succeeds");
        saved.read(stable,error);
        const QByteArray beforeMigrationIds=saved.ids;
        // A referenced legacy hash filename is migrated; unrelated old versions
        // are not swept from the user's route directory.
        const QString legacyRef="testa_materials_legacyhash.pmap";
        QFile::rename(stable,tileDir+"/"+legacyRef);
        a.descriptor().sampleMaterialBuffer=legacyRef;
        check(a.save() && a.descriptor().sampleMaterialBuffer=="testa_materials.pmap"
              && !QFile::exists(tileDir+"/"+legacyRef)
              && saved.read(stable,error) && saved.ids==beforeMigrationIds
              && saved.read(backup,error) && saved.ids==beforeMigrationIds,
              "legacy-hash-reference-migrates-to-stable-name-on-save");
        a.paintProceduralMaterial(&brush,0,0,-864,-992,12);
        const QString previous=a.descriptor().sampleMaterialBuffer;
        QDir().mkdir(tileDir+"/blocked.t"); a.name="blocked";
        check(!a.save() && a.isModified() && a.descriptor().sampleMaterialBuffer==previous,"failed-descriptor-save-keeps-reference-and-dirty-state");
        a.name="testa";
        check(!a.setProceduralMaterial(false,error) && a.usesProceduralMaterial(),"disable-refuses-unsaved-paint");
        check(a.save(),"save-current-bake-before-disable");
        check(a.setProceduralMaterial(false,error) && !a.usesProceduralMaterial()
              && a.descriptor().patchValue(0,TFile::PatchField::TextureW)==1.0f/256,"disable-retains-baked-static-uv");
        check(a.save() && roundtrip.readT(tileDir+"/testa.t") && roundtrip.sampleMaterialBuffer.isEmpty(),"disabled-reference-omitted");
        Game::writeEnabled=false;
        check(!a.setProceduralMaterial(true,error),"global-write-protection");
        Game::writeEnabled=true;
        a.descriptor().sampleMaterialBuffer="missing.pmap";
        a.loadProceduralMaterial(tileDir);
        check(a.usesProceduralMaterial() && !a.rendersProceduralMaterial() && !a.save(),"missing-sidecar-not-replaced-by-zero");
    }
    check(int(TexLib::mtex.size())==texturesBefore,"generated-material-reference-cleanup");
    {
        TestTerrain a,b;
        a.setup(temp.path(),16,"lazy-a"); b.setup(temp.path(),32,"lazy-b");
        a.setProceduralMaterial(true,error); b.setProceduralMaterial(true,error);
        check(int(TexLib::mtex.size())==texturesBefore,"lazy-enable-allocates-no-output-textures");
        const int shared=a.proceduralTexture(0);
        check(b.proceduralTexture(0)==shared && TexLib::mtex.at(shared)->ref==2,
              "two-material-owners-acquire-two-library-references");
        a.proceduralTexture(1);
        check(TexLib::mtex.at(shared)->ref==2,"same-tile-recipe-sharing-does-not-add-library-references");
        a.releaseProceduralTextures();
        check(a.proceduralResidentPatchCount()==0 && TexLib::mtex.at(shared)->ref==1
              && b.proceduralTexture(0)==shared,"releasing-one-tile-preserves-shared-neighbour");
        b.releaseProceduralTextures();
        check(int(TexLib::mtex.size())==texturesBefore,"last-owner-reclaims-cpu-only-textures-and-cache");
        bool bounded=true;
        for (int cycle=0;cycle<5;++cycle) {
            const int id=a.proceduralTexture(7);
            bounded &= id>=0 && a.proceduralResidentPatchCount()==1;
            a.releaseProceduralTextures();
            a.releaseProceduralTextures();
            bounded &= int(TexLib::mtex.size())==texturesBefore;
        }
        check(bounded,"repeated-residency-cycles-reclaim-generated-textures");
        Texture source(temp.path()+"/blue.png");
        QImage solid(9,9,QImage::Format_Grayscale8); solid.fill(0);
        Brush brush; brush.useTexture=true; brush.tex=&source; brush.brushshape=&solid;
        a.setModified(false);
        a.paintProceduralMaterial(&brush,0,0,-992,-992,12);
        check(a.isModified() && a.proceduralResidentPatchCount()==1
              && int(TexLib::mtex.size())>texturesBefore,"painting-generates-changed-patches-even-before-first-render");
        const int edited=a.proceduralTexture(0);
        const QByteArray pixels=TexLib::mtex.at(edited)->compressedData;
        a.releaseProceduralTextures();
        check(a.isModified() && a.usesProceduralMaterial() && int(TexLib::mtex.size())==texturesBefore,
              "dirty-tile-release-preserves-edit-state");
        const int regenerated=a.proceduralTexture(0);
        check(regenerated>=0 && TexLib::mtex.at(regenerated)->compressedData==pixels,
              "dirty-tile-regeneration-preserves-painted-pixels");
        a.releaseProceduralTextures();
        check(a.save() && a.proceduralResidentPatchCount()==0,
              "save-with-no-resident-patches-does-not-generate-output");
        b.descriptor().sampleMaterialBuffer=a.descriptor().sampleMaterialBuffer;
        b.loadProceduralMaterial(tileDir);
        check(b.rendersProceduralMaterial() && b.proceduralResidentPatchCount()==0,
              "reload-does-not-generate-output");
        // P16 reload keeps the same recipe dimensions for a direct pixel comparison.
        TestTerrain reloaded; reloaded.setup(temp.path(),16,"lazy-reload");
        reloaded.setProceduralMaterial(true,error); // Same reserved palette as the saved plane.
        reloaded.descriptor().sampleMaterialBuffer=a.descriptor().sampleMaterialBuffer;
        reloaded.loadProceduralMaterial(tileDir);
        const int loaded=reloaded.proceduralTexture(0);
        check(loaded>=0 && TexLib::mtex.at(loaded)->compressedData==pixels,
              "save-after-release-round-trips-edited-pixels");
        check(a.proceduralTexture(-1)==-1 && a.proceduralTexture(256)==-1,
              "invalid-patch-request-does-not-access-slot");
    }
    check(int(TexLib::mtex.size())==texturesBefore,"lazy-lifecycle-leaves-no-generated-textures");
    {
        TerrainMaterialMap varied; variedMaterialMap(varied);
        check(varied.write(tileDir+"/async.pmap",error),"async-fixture-write");
        TestTerrain a,b;
        a.setup(temp.path(),16,"async-a"); b.setup(temp.path(),16,"async-b");
        a.descriptor().sampleMaterialBuffer=b.descriptor().sampleMaterialBuffer="async.pmap";
        a.loadProceduralMaterial(tileDir); b.loadProceduralMaterial(tileDir);
        Terrain::beginProceduralFrame();
        for(int p=0;p<256;++p) { a.proceduralTexture(p,true); b.proceduralTexture(p,true); }
        check(Terrain::proceduralWorkStats().outstanding>0 && Terrain::proceduralWorkStats().outstanding<=4
              && a.proceduralResidentPatchCount()==0,"background-request-queues-bounded-work-without-publishing-inline");
        a.releaseProceduralTextures(); b.releaseProceduralTextures();
        check(finishMaterialJobs() && a.proceduralResidentPatchCount()==0
              && int(TexLib::mtex.size())==texturesBefore,"cancelled-jobs-cannot-resurrect-released-tile-caches");
        bool bounded=true, complete=false;
        QElapsedTimer timer; timer.start();
        while(timer.elapsed()<10000 && !complete) {
            Terrain::beginProceduralFrame(); complete=true;
            for(int p=0;p<256;++p) {
                if (a.proceduralTexture(p,true)<0) complete=false;
                if (b.proceduralTexture(p,true)<0) complete=false;
            }
            const auto stats=Terrain::proceduralWorkStats();
            bounded &= stats.active<=4 && stats.outstanding<=4;
            QThread::msleep(1);
        }
        check(complete && bounded && Terrain::proceduralWorkStats().peakActive<=4,
              "background-four-worker-four-job-limits-and-eventual-completion");
        check(a.proceduralTexture(100,true)==b.proceduralTexture(100,true),"background-results-share-across-tiles");
        TestTerrain reference; reference.setup(temp.path(),16,"async-reference");
        reference.descriptor().sampleMaterialBuffer="async.pmap";
        reference.loadProceduralMaterial(tileDir);
        bool same=true;
        for(int p : {0,1,7,16,100,255})
            same &= a.proceduralTexture(p,true)==reference.proceduralTexture(p);
        check(same,"background-output-identical-to-synchronous-reference");
        const int old=a.proceduralTexture(0,true);
        Texture source(temp.path()+"/blue.png");
        QImage solid(9,9,QImage::Format_Grayscale8); solid.fill(0);
        Brush brush; brush.useTexture=true; brush.tex=&source; brush.brushshape=&solid;
        a.releaseProceduralTextures();
        Terrain::beginProceduralFrame();
        for(int p=0;p<4;++p) a.proceduralTexture(p,true);
        a.paintProceduralMaterial(&brush,0,0,-992,-992,12);
        Terrain::beginProceduralFrame();
        const int painted=a.proceduralTexture(0,true);
        check(painted>=0 && painted!=old,"painting-replaces-background-preview-synchronously");
        check(finishMaterialJobs() && a.proceduralTexture(0,true)==painted,
              "cancelled-background-results-cannot-overwrite-synchronous-paint");
        // A second stamp is also synchronous; release/save preserves both edits.
        a.paintProceduralMaterial(&brush,0,0,-960,-992,12);
        a.proceduralTexture(0,true);
        a.releaseProceduralTextures();
        check(a.save() && finishMaterialJobs() && a.proceduralResidentPatchCount()==0,
              "save-after-inflight-edits-and-release-does-not-publish-stale-results");
        TerrainMaterialMap saved; saved.read(tileDir+"/"+a.descriptor().sampleMaterialBuffer,error);
        check(saved.at(worldPoint32,worldPoint32)==2 && saved.at(worldPoint32*2,worldPoint32)==2,
              "inflight-edits-persist-authoritative-latest-ids");
    }
    {
        TestTerrain dying; dying.setup(temp.path(),16,"async-dying");
        TerrainMaterialMap fixture; variedMaterialMap(fixture);
        fixture.write(tileDir+"/async-dying.pmap",error);
        dying.descriptor().sampleMaterialBuffer="async-dying.pmap";
        dying.loadProceduralMaterial(tileDir);
        Terrain::beginProceduralFrame();
        for(int p=0;p<8;++p) dying.proceduralTexture(p,true);
        check(dying.rendersProceduralMaterial() && Terrain::proceduralWorkStats().outstanding>0,
              "destruction-fixture-really-has-outstanding-workers");
    }
    check(finishMaterialJobs() && int(TexLib::mtex.size())==texturesBefore,
          "tile-destruction-with-outstanding-workers-is-safe-and-reclaims-output");
    {
        TestTerrain t; t.setup(temp.path(),16,"nearest");
        t.setProceduralMaterial(true,error);
        TestTerrain::PatchVisibility view;
        view.valid=true; view.maximumDistance=10000;
        view.cameraLocalX=view.cameraLocalZ=1984;
        auto order=t.proceduralRequestOrder(view);
        check(order.size()==256 && order[0]==255 && order[1]==239 && order[2]==254,
              "nearest-requests-use-camera-distance-and-deterministic-ties-not-row-order");
        view.cameraLocalX=view.cameraLocalZ=64;
        check(t.proceduralRequestOrder(view).first()==0,"nearest-request-order-follows-camera-movement");
        view.cameraLocalX=view.cameraLocalZ=1984;
        t.descriptor().flags[255]|=1; t.hide(239,true);
        check(t.proceduralRequestOrder(view).first()==254,"nearest-requests-skip-hidden-and-do-not-draw-patches");
        view.maximumDistance=0;
        check(t.proceduralRequestOrder(view).isEmpty(),"nearest-requests-respect-distance-culling");
        view.maximumDistance=10000; t.descriptor().flags[255]&=~1; t.hide(239,false);
        view.planes[0].x=-1; view.planes[0].w=1024;
        order=t.proceduralRequestOrder(view);
        bool clipped=!order.isEmpty();
        for(int p : order) clipped &= p%16<9;
        check(clipped,"nearest-requests-respect-frustum-culling");
        view.planes[0]={};
        t.spacing(16);
        check(t.proceduralRequestOrder(view).first()==119,"nearest-requests-use-physical-metres-on-larger-tiles");
        view.cameraLocalX=view.cameraLocalZ=3968;
        check(t.proceduralRequestOrder(view).first()==255,"nearest-requests-handle-larger-tile-far-corner");
        t.spacing(8); view.cameraLocalX=view.cameraLocalZ=1984;
        Terrain::beginProceduralFrame();
        t.prepareVisibleProceduralTextures(view);
        check(Terrain::proceduralWorkStats().outstanding==1,"nearest-requests-coalesce-repeated-recipes");
        check(finishMaterialJobs(),"nearest-request-jobs-complete");
        Terrain::beginProceduralFrame(); t.prepareVisibleProceduralTextures(view);
        order=t.proceduralRequestOrder(view);
        check(!order.contains(255) && order.contains(0),"nearest-patches-become-ready-before-distant-row-zero");
        t.releaseProceduralTextures();
        TestTerrain p32; p32.setup(temp.path(),32,"nearest-p32");
        p32.setProceduralMaterial(true,error);
        view.cameraLocalX=view.cameraLocalZ=2016;
        check(p32.proceduralRequestOrder(view).first()==1023,"nearest-request-order-supports-p32");
        QElapsedTimer sortTimer; sortTimer.start();
        for(int repeat=0;repeat<200;++repeat) p32.proceduralRequestOrder(view);
        qInfo() << "[tests:terrain-material] nearest request sort P32 mean ms" << sortTimer.nsecsElapsed()/200e6;
    }
    check(finishMaterialJobs() && int(TexLib::mtex.size())==texturesBefore,"nearest-prepass-retains-release-safety");
    if (benchmark) {
        for (int p : {16,32}) {
            map.initialize();
            for (int z=0;z<TerrainMaterialMap::Side;++z) for(int x=0;x<TerrainMaterialMap::Side;++x)
                map.ids[z*TerrainMaterialMap::Side+x]=char(((x/7)^(z/11))&1);
            double generation=0,compression=0,hashing=0;
            QVector<double> genTimes,hashTimes,bcTimes;
            constexpr int Runs=100;
            QElapsedTimer timer;
            for(int i=-5;i<Runs;++i) {
                timer.start();
                const auto rgb=map.generate(std::max(0,i)%(p*p),p,sources);
                const double g=timer.nsecsElapsed()/1e6;
                timer.restart(); const auto key=TerrainMaterialMap::textureKey(rgb,true);
                const double h=timer.nsecsElapsed()/1e6;
                timer.restart(); const auto blocks=TerrainMaterialMap::encodeBC1(rgb);
                const double c=timer.nsecsElapsed()/1e6;
                if(i>=0) {generation+=g;hashing+=h;compression+=c; genTimes.push_back(g);hashTimes.push_back(h);bcTimes.push_back(c);}
                if(key.isEmpty() || blocks.isEmpty()) ++failed;
            }
            qInfo() << "[bench:terrain-material] P" << p << "runs" << Runs << "mean patch ms generate" << generation/Runs
                    << "SHA256" << hashing/Runs << "BC1" << compression/Runs;
            for (auto times : {genTimes,hashTimes,bcTimes}) {
                std::sort(times.begin(),times.end());
                qInfo() << "[bench:terrain-material] median / P95 ms (generate, hash, BC1 order)" << times[Runs/2] << times[(Runs*95)/100];
            }
            timer.start();
            QHash<QByteArray,QImage> recipes;
            for(int i=0;i<p*p;++i) { const auto key=map.patchKey(i,p); if(!recipes.contains(key)) recipes.insert(key,map.generate(i,p,sources)); }
            qInfo() << "[bench:terrain-material] load recipes P" << p << "distinct" << recipes.size() << "CPU ms" << timer.nsecsElapsed()/1e6;
            // Worst-case categorical noise: every patch differs. Exercise production
            // preparation twice concurrently, so the second tile hits global TexLib.
            quint32 rng=0x73518;
            for (char &id : map.ids) { rng=rng*1664525u+1013904223u; id=char(rng>>31); }
            map.write(temp.path()+"/noise.pmap",error);
            {
                TestTerrain first,second;
                first.setup(temp.path(),p,"noise1"); second.setup(temp.path(),p,"noise2");
                first.descriptor().sampleMaterialBuffer="noise.pmap";
                second.descriptor().sampleMaterialBuffer="noise.pmap";
                timer.start(); first.loadProceduralMaterial(temp.path());
                for (int i=0;i<p*p;++i) first.proceduralTexture(i);
                const double firstMs=timer.nsecsElapsed()/1e6;
                const auto firstCount=TexLib::mtex.size();
                timer.restart(); second.loadProceduralMaterial(temp.path());
                for (int i=0;i<p*p;++i) second.proceduralTexture(i);
                const double secondMs=timer.nsecsElapsed()/1e6;
                qint64 payload=0;
                for (const auto &entry : TexLib::mtex) if(entry.second) payload+=entry.second->estimatedCpuBytes();
                check(TexLib::mtex.size()==firstCount,"benchmark-cross-tile-unique-output-sharing");
                qInfo() << "[bench:terrain-material] all unique P" << p << "load+request-all ms" << firstMs << "shared second tile ms" << secondMs
                        << "generated texture CPU payload bytes" << payload << "ID plane bytes per tile" << map.ids.size();
            }
        }
    }
    qInfo() << "[tests:terrain-material] passed" << passed << "failed" << failed;
    return failed ? 1 : 0;
}

int TsreTests::runTerrainMaterialGlSuite() {
    QOpenGLContext context;
    QSurfaceFormat format; format.setVersion(3,3); format.setProfile(QSurfaceFormat::CompatibilityProfile);
    context.setFormat(format);
    if (!context.create()) { qWarning() << "[tests:terrain-material-gl] context unavailable"; return 2; }
    QOffscreenSurface surface; surface.setFormat(context.format()); surface.create();
    if (!context.makeCurrent(&surface)) { qWarning() << "[tests:terrain-material-gl] cannot make context current"; return 2; }
    auto *f=context.functions();
    qInfo() << "[tests:terrain-material-gl] GPU" << reinterpret_cast<const char*>(f->glGetString(GL_RENDERER));
    QTemporaryDir temp;
    QScopedValueRollback<bool> write(Game::writeEnabled,true);
    QScopedValueRollback<bool> seasonal(Game::seasonalEditing,false);
    QScopedValueRollback<bool> caseFolding(Game::caseInsensitiveFS,false);
    QScopedValueRollback<QString> root(Game::root,temp.path());
    QScopedValueRollback<QString> route(Game::route,QStringLiteral("proc-gl"));
    QDir().mkpath(temp.path()+"/routes/proc-gl/tiles");
    QImage red(256,256,QImage::Format_RGB888);red.fill(Qt::red);red.save(temp.path()+"/red.png");
    QImage blue(256,256,QImage::Format_RGB888);blue.fill(Qt::blue);blue.save(temp.path()+"/blue.png");
    int failed=0;
    for (const QString &directory : {QStringLiteral("shaders"),QStringLiteral("shaders330")}) {
        for (const QString &shaderName : {QStringLiteral("StandardFog"),QStringLiteral("StandardFogStoredCoords"),QStringLiteral("StandardBloom")}) {
            const QString base="appdata/0.697/"+directory+"/"+shaderName;
            QOpenGLShaderProgram program;
            if (!program.addShaderFromSourceFile(QOpenGLShader::Vertex,base+".vs")
                    || !program.addShaderFromSourceFile(QOpenGLShader::Fragment,base+".fs") || !program.link()) {
                qWarning() << "Baked terrain shader compile/link failed" << base << program.log(); ++failed;
            }
        }
    }
    {
        // Render actual vertex-shader UV remapping. The same post-transform is
        // used for precomputed attributes and paged UBO-derived coordinates.
        QOpenGLShaderProgram program;
        bool ok=program.addShaderFromSourceFile(QOpenGLShader::Vertex,"appdata/0.697/shaders330/StandardFog.vs")
                && program.addShaderFromSourceCode(QOpenGLShader::Fragment,
                    "#version 330 core\nin vec2 vTextureCoord; out vec4 fragColor; void main(){fragColor=vec4(vTextureCoord,0,1);}")
                && program.link() && program.bind();
        if (ok) {
            QOpenGLFramebufferObject target(16,16); target.bind(); f->glViewport(0,0,16,16);
            QOpenGLVertexArrayObject vao; vao.create(); QOpenGLVertexArrayObject::Binder binding(&vao);
            QOpenGLBuffer vbo; vbo.create(); vbo.bind();
            const float vertices[]={-1,-1,0,0,0, 3,-1,0,0,0, -1,3,0,0,0};
            vbo.allocate(vertices,sizeof(vertices));
            program.enableAttributeArray("vertex"); program.setAttributeBuffer("vertex",GL_FLOAT,0,3,5*sizeof(float));
            program.enableAttributeArray("aTextureCoord"); program.setAttributeBuffer("aTextureCoord",GL_FLOAT,3*sizeof(float),2,5*sizeof(float));
            program.setUniformValue("uPMatrix",QMatrix4x4()); program.setUniformValue("uMVMatrix",QMatrix4x4());
            program.setUniformValue("uMSMatrix",QMatrix4x4()); program.setUniformValue("terrainPaged",0);
            // Active uniform block still needs valid storage even on the nonpaged branch.
            GLuint ubo; auto *extra=context.extraFunctions(); extra->glGenBuffers(1,&ubo);
            extra->glBindBuffer(GL_UNIFORM_BUFFER,ubo); extra->glBufferData(GL_UNIFORM_BUFFER,8192,nullptr,GL_STATIC_DRAW);
            const GLuint block=extra->glGetUniformBlockIndex(program.programId(),"TerrainPatchBlock");
            if (block!=GL_INVALID_INDEX) extra->glUniformBlockBinding(program.programId(),block,0);
            extra->glBindBufferBase(GL_UNIFORM_BUFFER,0,ubo);
            program.setUniformValue("terrainTextureRemap",QVector3D(-0.9375f,0.5f,0.75f));
            f->glDrawArrays(GL_TRIANGLES,0,3);
            const auto baked=target.toImage().pixelColor(8,8);
            program.setUniformValue("terrainTextureRemap",QVector3D());
            f->glDrawArrays(GL_TRIANGLES,0,3);
            const auto plain=target.toImage().pixelColor(8,8);
            ok=abs(baked.red()-128)<=1 && abs(baked.green()-191)<=1 && plain.red()==0 && plain.green()==0;
            extra->glDeleteBuffers(1,&ubo); target.release(); program.release();
        }
        if (!ok) ++failed;
        qInfo() << "[tests:terrain-material-gl] baked UV remap and identity reset" << ok;
    }
    {
        TestTerrain a,b; a.setup(temp.path(),16,"gla");b.setup(temp.path(),32,"glb");
        float defaultDetailScale;
        std::memcpy(&defaultDetailScale, &a.descriptor().materials[0].itex[1][3], sizeof(float));
        if (defaultDetailScale != Terrain::ProceduralDetailScale
                || *a.descriptor().materials[0].tex[1] != "microtex.ace") ++failed;
        QString error;
        if (!a.setProceduralMaterial(true,error) || !b.setProceduralMaterial(true,error)) ++failed;
        const int id=a.proceduralTexture(0);
        const int other=b.proceduralTexture(10);
        auto *texture=TexLib::mtex.at(id);
        if(id!=other || !texture->glLoaded || !f->glIsTexture(texture->tex[0])) ++failed;
        // A deterministic ordinary TexLib source, initially pending: detail must
        // not use stale GPU state while it is unavailable, nor replace primary.
        QString detailPath = temp.path()+"/microtex.ace";
        if (Game::caseInsensitiveFS) detailPath = detailPath.toLower();
        auto *detail = new Texture(detailPath);
        const int detailId = TexLib::addTex(detail);
        if (a.proceduralDetailTexture() != -1) ++failed;
        detail->width=detail->height=4; detail->bpp=24; detail->bytesPerPixel=3;
        detail->type=GL_RGB; detail->imageSize=48;
        detail->imageData=new unsigned char[48]; std::memset(detail->imageData,128,48);
        detail->loaded=true;
        f->glActiveTexture(GL_TEXTURE0); f->glBindTexture(GL_TEXTURE_2D,texture->tex[0]);
        if (a.proceduralDetailTexture()!=detailId || b.proceduralDetailTexture()!=detailId) ++failed;
        GLint activeUnit=0,primaryBinding=0,filter=0,wrap=0;
        f->glGetIntegerv(GL_ACTIVE_TEXTURE,&activeUnit);
        f->glGetIntegerv(GL_TEXTURE_BINDING_2D,&primaryBinding);
        if (activeUnit!=GL_TEXTURE0 || GLuint(primaryBinding)!=texture->tex[0]) ++failed;
        f->glActiveTexture(GL_TEXTURE1); f->glBindTexture(GL_TEXTURE_2D,detail->tex[0]);
        f->glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&filter);
        f->glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,&wrap);
        if (filter!=GL_LINEAR_MIPMAP_LINEAR || wrap!=GL_REPEAT) ++failed;
        f->glActiveTexture(GL_TEXTURE0);
        RenderItem item;
        if (item.secondTexScale!=0.0f) ++failed;
        item.secondTexAddr=detail->tex[0]; item.secondTexScale=Terrain::ProceduralDetailScale;
        RenderItem copied(item);
        if (copied.secondTexAddr!=detail->tex[0] || copied.secondTexScale!=32.0f) ++failed;
        const int detailRefs=detail->ref;
        a.proceduralDetailTexture();
        if (detail->ref!=detailRefs) ++failed;
        qInfo() << "[tests:terrain-material-gl] fixed microtex shared, scale" << defaultDetailScale
                << "mipmapped, repeating; primary binding preserved";
        texture->setEditable();
        if (!texture->imageData || texture->imageData[0]<245 || texture->imageData[1]>10) ++failed;
        qInfo() << "[tests:terrain-material-gl] shared output compressed" << texture->gpuIsCompressed() << "GPU bytes" << texture->estimatedVramBytes();
        Texture source(temp.path()+"/blue.png");
        Brush brush;brush.useTexture=true;brush.tex=&source;
        QImage mask(9,9,QImage::Format_Grayscale8);mask.fill(0);brush.brushshape=&mask;
        a.paintProceduralMaterial(&brush,0,0,-992,-992,12);
        const int edited=a.proceduralTexture(0);
        if(edited==id || b.proceduralTexture(0)!=id) ++failed;
        const unsigned int obsolete=TexLib::mtex.at(edited)->tex[0];
        a.paintProceduralMaterial(&brush,0,0,-960,-992,12);
        if(f->glIsTexture(obsolete)) ++failed;
        a.proceduralTexture(0);
        a.releaseProceduralTextures();
        if (detail->ref!=detailRefs-1) ++failed;
        b.releaseProceduralTextures();
        if (detail->ref!=detailRefs-2) ++failed;
        const GLuint detailGlId=detail->tex[0];
        TexLib::delRef(detailId);
        f->glDeleteTextures(1,&detailGlId);
        delete[] detail->tex;
        delete detail;
        if(f->glGetError()!=GL_NO_ERROR) ++failed;
    }
    {
        TestTerrain a,b; a.setup(temp.path(),16,"bake-gl-a"); b.setup(temp.path(),32,"bake-gl-b");
        QString error;
        bool ok=a.setProceduralMaterial(true,error) && b.setProceduralMaterial(true,error) && a.save() && b.save();
        a.loadProceduralMaterial(Game::root+"/routes/"+Game::route+"/tiles");
        b.loadProceduralMaterial(Game::root+"/routes/"+Game::route+"/tiles");
        TestTerrain::PatchVisibility far; far.valid=true; far.maximumDistance=100000;
        far.cameraLocalX=6000; far.cameraLocalZ=1000;
        int first=-1,second=-1; QElapsedTimer timer; timer.start();
        while(ok && (first<0 || second<0) && timer.elapsed()<10000) {
            GLint unitBefore=0,bindingBefore=0,unitAfter=0,bindingAfter=0;
            f->glGetIntegerv(GL_ACTIVE_TEXTURE,&unitBefore);
            f->glGetIntegerv(GL_TEXTURE_BINDING_2D,&bindingBefore);
            Terrain::beginProceduralFrame();
            f->glGetIntegerv(GL_ACTIVE_TEXTURE,&unitAfter);
            f->glGetIntegerv(GL_TEXTURE_BINDING_2D,&bindingAfter);
            ok &= unitBefore==unitAfter && bindingBefore==bindingAfter;
            // No visible-patch preparation: begin-frame must upload preloaded
            // bakes even when the camera has never faced these tiles.
            for (const auto &entry : TexLib::mtex) if (entry.second && entry.second->glLoaded
                    && entry.second->width==TerrainMaterialMap::BakedSide && entry.second->ref==2) {
                first=a.proceduralFallbackTexture(); second=b.proceduralFallbackTexture();
                break;
            }
            ok &= Terrain::proceduralWorkStats().outstanding<=4 && Terrain::proceduralWorkStats().uploads<=2;
            QThread::msleep(1);
        }
        ok &= first>=0 && first==second && a.proceduralResidentPatchCount()==0 && b.proceduralResidentPatchCount()==0;
        ok &= a.proceduralTextureRemap(1,-1)==QVector3D(1.0f/16-1,1.0f/16,0);
        if (ok) {
            auto *texture=TexLib::mtex.at(first); const GLuint gpu=texture->tex[0];
            f->glActiveTexture(GL_TEXTURE0); f->glBindTexture(GL_TEXTURE_2D,gpu);
            GLint filter=0,wrap=0; f->glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&filter);
            f->glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,&wrap);
            ok &= filter==GL_LINEAR && wrap==GL_CLAMP_TO_EDGE && texture->ref==2;
            a.releaseProceduralTextures(); ok &= f->glIsTexture(gpu) && b.proceduralFallbackTexture()==second;
            b.releaseProceduralTextures(); ok &= !f->glIsTexture(gpu) && !TexLib::mtex.count(first);
        }
        if (!ok) ++failed;
        qInfo() << "[tests:terrain-material-gl] bounded base-level bake uploads, linear filtering, cross-tile sharing and final-owner release" << ok;
    }
    {
        TestTerrain a,b; a.setup(temp.path(),16,"release-a"); b.setup(temp.path(),32,"release-b");
        QString error;
        a.setProceduralMaterial(true,error); b.setProceduralMaterial(true,error);
        const int id=a.proceduralTexture(0);
        const GLuint gpu=TexLib::mtex.at(id)->tex[0];
        if (b.proceduralTexture(0)!=id) ++failed;
        a.releaseProceduralTextures();
        if (!f->glIsTexture(gpu) || b.proceduralTexture(0)!=id) ++failed;
        b.releaseProceduralTextures();
        if (f->glIsTexture(gpu) || TexLib::mtex.count(id)!=0) ++failed;
        const int next=a.proceduralTexture(0);
        const GLuint deferred=TexLib::mtex.at(next)->tex[0];
        context.doneCurrent();
        a.releaseProceduralTextures();
        context.makeCurrent(&surface);
        // Explicit release also flushes a pending deletion without generating a patch.
        a.releaseProceduralTextures();
        if (f->glIsTexture(deferred) || TexLib::mtex.count(next)!=0) ++failed;
        qInfo() << "[tests:terrain-material-gl] shared-owner and deferred-context release checked";
    }
    {
        TerrainMaterialMap map; variedMaterialMap(map); QString error;
        if (!map.write(temp.path()+"/upload-budget.pmap",error)) ++failed;
        TestTerrain t; t.setup(temp.path(),16,"upload-budget");
        t.descriptor().sampleMaterialBuffer="upload-budget.pmap";
        t.loadProceduralMaterial(temp.path());
        context.doneCurrent();
        bool ready=false; QElapsedTimer timer; timer.start();
        while(!ready && timer.elapsed()<10000) {
            Terrain::beginProceduralFrame(); ready=true;
            for(int p=0;p<256;++p) if(t.proceduralTexture(p,true)<0) ready=false;
            QThread::msleep(1);
        }
        if (!ready || !context.makeCurrent(&surface)) ++failed;
        QSet<int> unique;
        for(int p=0;p<256;++p) unique.insert(t.proceduralTexture(p,true));
        // The above calls are also budgeted; restart residency to inspect a full run.
        t.releaseProceduralTextures();
        bool bounded=true; ready=false; timer.restart(); int uploadFrames=0;
        while(!ready && timer.elapsed()<10000) {
            Terrain::beginProceduralFrame(); ready=true;
            for(int p=0;p<256;++p) if(t.proceduralTexture(p,true)<0) ready=false;
            const auto stats=Terrain::proceduralWorkStats();
            bounded &= stats.uploads<=2 && stats.active<=4 && stats.outstanding<=4;
            if (stats.uploads) ++uploadFrames;
            QThread::msleep(1);
        }
        if (!ready || !bounded || uploadFrames<2) ++failed;
        qInfo() << "[tests:terrain-material-gl] bounded background upload frames" << uploadFrames
                << "peak workers" << Terrain::proceduralWorkStats().peakActive;
        QVector<int> before;
        for(int p=0;p<256;++p) before.push_back(t.proceduralTexture(p));
        Texture source(temp.path()+"/blue.png");
        QImage solid(9,9,QImage::Format_Grayscale8); solid.fill(0);
        Brush brush; brush.useTexture=true; brush.tex=&source; brush.brushshape=&solid;
        context.doneCurrent();
        t.paintProceduralMaterial(&brush,0,0,-768,-900,220);
        QSet<int> paintedIds; QVector<int> paintedPatches;
        for(int p=0;p<256;++p) {
            const int id=t.proceduralTexture(p);
            if(id!=before[p]) { paintedPatches.push_back(p); paintedIds.insert(id); }
        }
        if(paintedIds.size()<3 || !context.makeCurrent(&surface)) ++failed;
        Terrain::beginProceduralFrame();
        for(int p : paintedPatches) {
            const int id=t.proceduralTexture(p,true);
            if(id<0 || !TexLib::mtex.at(id)->glLoaded) ++failed;
        }
        if(Terrain::proceduralWorkStats().uploads!=0) ++failed;
        qInfo() << "[tests:terrain-material-gl] synchronous paint uploads bypass streaming budget, unique outputs"
                << paintedIds.size();
        t.releaseProceduralTextures();
        if (!finishMaterialJobs()) ++failed;
    }
    {
        TestTerrain t; t.setup(temp.path(),16,"nearest-upload");
        t.descriptor().sampleMaterialBuffer="upload-budget.pmap";
        t.loadProceduralMaterial(temp.path());
        context.doneCurrent();
        for(int p=0;p<256;++p) t.proceduralTexture(p);
        if(!context.makeCurrent(&surface)) ++failed;
        GLuint bindings[2]; f->glGenTextures(2,bindings);
        f->glActiveTexture(GL_TEXTURE0); f->glBindTexture(GL_TEXTURE_2D,bindings[0]);
        f->glActiveTexture(GL_TEXTURE1); f->glBindTexture(GL_TEXTURE_2D,bindings[1]);
        TestTerrain::PatchVisibility view;
        view.valid=true; view.maximumDistance=10000;
        view.cameraLocalX=960; view.cameraLocalZ=64;
        const auto order=t.proceduralRequestOrder(view);
        if(order.isEmpty() || order.first()!=7) ++failed;
        Terrain::beginProceduralFrame(); t.prepareVisibleProceduralTextures(view);
        GLint active=0,binding=0;
        f->glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        f->glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
        if(active!=GL_TEXTURE1 || GLuint(binding)!=bindings[1]) ++failed;
        f->glActiveTexture(GL_TEXTURE0); f->glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
        if(GLuint(binding)!=bindings[0]) ++failed;
        if(t.proceduralRequestOrder(view).contains(7) || !t.proceduralRequestOrder(view).contains(0)
                || Terrain::proceduralWorkStats().uploads>2) ++failed;
        f->glDeleteTextures(2,bindings);
        t.releaseProceduralTextures();
        qInfo() << "[tests:terrain-material-gl] nearest-first uploads and prepass texture binding restoration checked";
    }
    // Render F2 into an image for layout review without opening an editor window.
    TerrainTools tools("Terrain");
    QString selectedTool;
    Brush *selectedBrush=nullptr;
    QObject::connect(&tools,&TerrainTools::enableTool,[&](const QString &tool) { selectedTool=tool; });
    QObject::connect(&tools,&TerrainTools::setPaintBrush,[&](Brush *brush) { selectedBrush=brush; });
    for(const QString &name : {QStringLiteral("proceduralPaintTextureTool"),QStringLiteral("proceduralFillPatchTool"),QStringLiteral("proceduralFillTool")}) {
        auto *button=tools.findChild<QPushButton*>(name);
        if(!button) { ++failed; continue; }
        button->click();
        if(selectedTool!=name || !selectedBrush || !selectedBrush->useTexture) ++failed;
        tools.msg("toolEnabled",name);
        if(!button->isChecked()) ++failed;
        button->click();
        if(!selectedTool.isEmpty()) ++failed;
    }
    if(tools.findChildren<QPushButton*>("lockTexTool").size()!=1) ++failed;
    tools.ensurePolished(); tools.resize(tools.sizeHint());
    QImage screenshot(tools.size(),QImage::Format_ARGB32);screenshot.fill(Qt::white);tools.render(&screenshot);
    screenshot.save("build/terrain-procedural-tools.png");
    for(auto *button : tools.findChildren<QPushButton*>())
        if((button->text().startsWith("Make tile use") || button->objectName().startsWith("procedural"))
                && button->fontMetrics().horizontalAdvance(button->text())>button->width()-8) ++failed;
    qInfo() << "[tests:terrain-material-gl] failed" << failed;
    context.doneCurrent();
    return failed ? 1 : 0;
}
