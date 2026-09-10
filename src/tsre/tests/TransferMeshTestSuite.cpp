#include "TransferMeshTestSuite.h"
#include <tsre/world/objects/TransferMesh.h>
#include <tsre/world/objects/TransferObj.h>
#include <tsre/world/Terrain.h>
#include <tsre/world/TerrainLib.h>
#include <tsre/world/TerrainMeshBackend.h>
#include <tsre/world/TerrainAdjacentEdge.h>
#include <tsre/Game.h>
#include <QDebug>
#include <QElapsedTimer>
#include <QScopedValueRollback>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>
#include <tsre/renderer/OpenGL3Renderer.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/texture/Texture.h>
#include <tsre/texture/TexLib.h>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOffscreenSurface>
#include <QOpenGLFramebufferObject>

namespace {
class TestTerrain : public Terrain {
public:
    TestTerrain(int wx, int wz, int n=256, int spacing=8, int patches=16) {
        mojex=wx; mojez=wz;
        QString error;
        TerrainGridLayout::tryCreate(n,spacing,patches,0,gridLayout,error);
        tfile=new TFile; tfile->initNew("transfer-test",n,spacing,patches);
        terrainDataRows=n+1; terrainData=new float*[n+1];
        for (int z=0;z<=n;++z) {
            terrainData[z]=new float[n+1];
            for (int x=0;x<=n;++x) terrainData[z][x]=20;
        }
        for (int i=0;i<gridLayout.patchRecordCount();++i) hidden[i]=false;
        loaded=true;
        initializePatchBounds();
    }
    void lod(int step,quint8 mask=0) { renderedSurfaceLod.fill({step,step*gridLayout.sampleSpacing,mask},gridLayout.patchRecordCount()); }
    void hide(bool value) { for (int i=0;i<gridLayout.patchRecordCount();++i) tfile->flags[i]=value?1:0; }
    void gap(int x,int z,bool enabled=true) { if (!jestF) newF(); fData[z][x]=enabled?4:0; invalidatePatch(0,TerrainDirtyGaps); }
};
class Library : public TerrainLib {
public:
    QVector<Terrain*> tiles;
    int lookups=0;
    Terrain *getTerrainByXY(int x,int z,bool) override {
        ++lookups;
        for (auto *terrain:tiles) {
            const auto b=TerrainPhysicalBounds::of(*terrain);
            if (qint64(x)*2048>=b.minX && qint64(x)*2048<b.maxX
                    && qint64(z)*2048>=b.minZ && qint64(z)*2048<b.maxZ) return terrain;
        }
        return nullptr;
    }
};
// Preserve the existing whole-carpet coverage/plane oracles while exposing the
// actual disjoint production outputs for partition tests. Joining is test-only.
class TestMeshCache : public TransferMesh {
public:
    QVector<float> ground, holes;
    bool update(TerrainLib *lib, int wx, int wz, const float *pos, double width,
                double height, const float *q, float alpha, QVector<float> &combined,
                bool respectHoles=false) {
        if (!TransferMesh::update(lib,wx,wz,pos,width,height,q,alpha,ground,holes,respectHoles)) return false;
        combined=ground+holes;
        return true;
    }
    bool coversTerrainHoles() const { return !holes.isEmpty(); }
};
double area(const QVector<float> &mesh) {
    double result=0;
    for (int i=0;i+26<mesh.size();i+=27)
        result+=((double(mesh[i+11])-mesh[i+2])*(double(mesh[i+18])-mesh[i])
                -(double(mesh[i+9])-mesh[i])*(double(mesh[i+20])-mesh[i+2]))/2;
    return result;
}
bool validUv(const QVector<float> &mesh,const TransferMesh::Rectangle &rect) {
    for (int i=0;i+8<mesh.size();i+=9) {
        for (int j=0;j<9;++j) if (!std::isfinite(mesh[i+j])) return false;
        TransferMesh::Vertex p{mesh[i],mesh[i+1],mesh[i+2]}; rect.uv(p);
        if (mesh[i+6]<0 || mesh[i+6]>1 || mesh[i+7]<0 || mesh[i+7]>1
                || std::abs(mesh[i+6]-p.u)>2e-5 || std::abs(mesh[i+7]-p.v)>2e-5) return false;
    }
    return true;
}
}
int TsreTests::runTransferMeshSuite(bool verbose) {
    int passed=0,failed=0;
    auto check=[&](bool ok,const char *name) {
        if (ok) ++passed; else ++failed;
        if (!ok || verbose) qInfo() << "[tests:transfer-mesh]" << (ok?"PASS":"FAIL") << name;
    };
    QScopedValueRollback<Game::TerrainMeshMode> mode(Game::terrainMeshMode,Game::TERRAIN_MESH_PAGED);
    using V=TransferMesh::Vertex;
    QVector<float> mesh,outline;
    for (double yaw : {0.0,0.1,0.7,1.5707963267948966,3.0,-1.2}) {
        float q[]={0,float(std::sin(yaw/2)),0,float(std::cos(yaw/2))};
        const auto rect=TransferMesh::Rectangle::fromQuaternion(31,17,q);
        const auto point=[](double x,double z) { return V{x,10+x*0.25-z*0.75,z}; };
        mesh.clear(); outline.clear();
        TransferMesh::clipTriangle(rect,{point(-100,-100),point(-100,100),point(100,100)},-0.3,mesh,outline);
        TransferMesh::clipTriangle(rect,{point(-100,-100),point(100,100),point(100,-100)},-0.3,mesh,outline);
        bool plane=true;
        for(int i=0;i<mesh.size();i+=9) plane &= std::abs(mesh[i+1]-(10+mesh[i]*0.25-mesh[i+2]*0.75+0.05))<1e-5;
        check(std::abs(area(mesh)-31*17)<0.001 && validUv(mesh,rect) && plane,"rotated-clipping-area-plane-uv-winding");
        double perimeter=0;
        for(int i=0;i+5<outline.size();i+=6) perimeter+=std::hypot(outline[i+3]-outline[i],outline[i+5]-outline[i+2]);
        check(std::abs(perimeter-96)<0.001,"outline-only-rectangle-perimeter");
        for(float &component:q) component=-component;
        const auto opposite=TransferMesh::Rectangle::fromQuaternion(31,17,q);
        check(std::abs(opposite.cosine-rect.cosine)<1e-7 && std::abs(opposite.sine-rect.sine)<1e-7,
              "quaternion-sign-does-not-mirror-uv");
    }
    Library lib;
    TestTerrain t(0,0); lib.tiles={&t};
    QScopedValueRollback<TerrainLib*> terrainLib(Game::terrainLib,&lib);
    TestMeshCache cache;
    const float q[]={0,0,0,1}; float pos[]={0,0,0};
    check(cache.update(&lib,0,0,pos,100,100,q,-0.3,mesh) && std::abs(area(mesh)-10000)<0.01,
          "native-grid-carpet-area");
    check(mesh.size()/9 < 100*100*6/10,"ordinary-8m-terrain-over-tenfold-fewer-vertices-than-1m-carpet");
    const int nativeVertices=mesh.size()/9;
    qInfo() << "[tests:transfer-mesh] 100x100m / 8m terrain vertices" << nativeVertices
            << "old independent 2m carpet vertices" << 50*50*6;
    check(!cache.update(&lib,0,0,pos,100,100,q,-0.3,mesh),"unchanged-no-regeneration");
    check(!cache.coversTerrainHoles(),"solid-ground-disables-transfer-depth-writes");
    auto epoch=t.surfaceRevision(); t.invalidateAll(TerrainDirtyUvParams);
    check(t.surfaceRevision()==epoch && !cache.update(&lib,0,0,pos,100,100,q,-0.3,mesh),"terrain-texture-edit-does-not-rebuild-transfer");
    t.terrainData[128][128]=120; t.invalidateSamples(128,128,128,128,TerrainDirtyHeight);
    check(cache.update(&lib,0,0,pos,100,100,q,-0.3,mesh),"height-edit-invalidates");
    // Independent piecewise-planar oracle for the alternating native diagonals.
    auto terrainPlane=[&](double wx,double wz) {
        const double gx=(wx+1024)/8,gz=(wz+1024)/8;
        const int x=int(std::floor(gx)),z=int(std::floor(gz)); const double u=gx-x,v=gz-z;
        const double a=t.terrainData[z][x],b=t.terrainData[z][x+1],c=t.terrainData[z+1][x],d=t.terrainData[z+1][x+1];
        if (((x+z)&1)==0) return v>=u ? a*(1-v)+c*(v-u)+d*u : a*(1-u)+d*v+b*(u-v);
        return u+v<=1 ? a*(1-u-v)+b*u+c*v : c*(1-u)+d*(u+v-1)+b*(1-v);
    };
    bool exact=true;
    for(int i=0;i+26<mesh.size();i+=27) {
        const double x=(mesh[i]+mesh[i+9]+mesh[i+18])/3.0,z=(mesh[i+2]+mesh[i+11]+mesh[i+20])/3.0;
        const double y=(mesh[i+1]+mesh[i+10]+mesh[i+19])/3.0;
        exact &= std::abs(y-terrainPlane(x,z)-0.05)<2e-5;
    }
    check(exact,"triangle-interiors-fit-spike-not-bilinear-or-resampled-carpet");
    t.lod(2);
    check(cache.update(&lib,0,0,pos,100,100,q,-0.3,mesh) && mesh.size()/9<nativeVertices,
          "current-lod-changes-transfer-topology");
    t.lod(1,TerrainLod::LocalX0);
    check(cache.update(&lib,0,0,pos,100,100,q,-0.3,mesh) && std::abs(area(mesh)-10000)<0.01,
          "stitched-edge-template-keeps-coverage");
    t.hide(true);
    cache.update(&lib,0,0,pos,100,100,q,-0.3,mesh);
    check(std::abs(area(mesh)-10000)<0.01,"default-covers-hidden-terrain-patches");
    check(cache.coversTerrainHoles(),"hidden-ground-retains-transfer-depth-writes");
    check(cache.ground.isEmpty() && std::abs(area(cache.holes)-10000)<0.01,
          "fully-hidden-terrain-emits-only-hole-cover");
    check(cache.update(&lib,0,0,pos,100,100,q,-0.3,mesh,true) && mesh.isEmpty(),"optional-hidden-terrain-clipping");
    t.hide(false); t.lod(1); t.gap(10,10);
    check(cache.update(&lib,0,0,pos,100,100,q,-0.3,mesh) && !cache.coversTerrainHoles(),
          "hole-outside-transfer-does-not-force-depth-writes");
    t.gap(128,128);
    check(cache.update(&lib,0,0,pos,100,100,q,-0.3,mesh) && std::abs(area(mesh)-10000)<0.01,
          "default-covers-terrain-holes");
    check(cache.coversTerrainHoles(),"hole-covering-retains-transfer-depth-writes");
    const auto groundBeforeClip=cache.ground, holesBeforeClip=cache.holes;
    check(!cache.ground.isEmpty() && !cache.holes.isEmpty()
          && std::abs(area(cache.ground)+area(cache.holes)-10000)<0.01,
          "hole-and-ground-partition-preserves-full-area");
    check(validUv(cache.ground,TransferMesh::Rectangle::fromQuaternion(100,100,q))
          && validUv(cache.holes,TransferMesh::Rectangle::fromQuaternion(100,100,q)),
          "both-partitions-use-the-same-uv-transform");
    check(!cache.update(&lib,0,0,pos,100,100,q,-0.3,mesh) && cache.coversTerrainHoles(),
          "cached-hole-coverage-preserved");
    check(cache.ground==groundBeforeClip && cache.holes==holesBeforeClip,"cache-hit-preserves-both-outputs");
    check(cache.update(&lib,0,0,pos,100,100,q,-0.3,mesh,true) && area(mesh)<10000 && area(mesh)>9000,
          "optional-gap-triangles-omitted");
    check(!cache.coversTerrainHoles(),"clipped-holes-do-not-require-depth-writes");
    check(cache.ground==groundBeforeClip && cache.holes.isEmpty(),
          "disabling-hole-cover-keeps-identical-ground-mesh");
    t.loaded=false;
    check(cache.update(&lib,0,0,pos,100,100,q,-0.3,mesh) && mesh.isEmpty(),"unloaded-terrain-omitted");
    t.loaded=true;
    check(cache.update(&lib,0,0,pos,100,100,q,-0.3,mesh) && !mesh.isEmpty(),"terrain-reappearance-rebuilds");
    for (double width : {0.0,-1.0,std::numeric_limits<double>::infinity(),1e100})
        check(cache.update(&lib,0,0,pos,width,10,q,-0.3,mesh) && mesh.isEmpty(),"invalid-or-enormous-dimensions-safe");
    TestTerrain right(1,0,512,4), bottom(0,1,1024,2), diagonal(1,1,256,8);
    lib.tiles={&t,&right,&bottom,&diagonal}; pos[0]=pos[2]=1024;
    check(cache.update(&lib,0,0,pos,100,100,q,-0.3,mesh) && std::abs(area(mesh)-10000)<0.01
          && validUv(mesh,TransferMesh::Rectangle::fromQuaternion(100,100,q)),"four-tile-corner-mixed-native-resolutions");
    // A 4 km terrain tile occupies four World cells but contributes once.
    TestTerrain large(0,1,256,16); lib.tiles={&large}; pos[0]=pos[2]=0;
    check(cache.update(&lib,0,0,pos,200,200,q,-0.3,mesh) && std::abs(area(mesh)-40000)<0.01,
          "4096m-terrain-distinct-from-world-tile-size");
    const float turn[]={0,float(std::sin(0.37)),0,float(std::cos(0.37))};
    pos[0]=pos[2]=1024;
    check(cache.update(&lib,0,0,pos,200,200,turn,-0.3,mesh) && std::abs(area(mesh)-40000)<0.1,
          "larger-terrain-deduplicated-across-four-world-lookups");
    // Cache check timing, no GL or disk. Report rather than machine-specific assertion.
    QElapsedTimer timer; timer.start();
    for (int i=0;i<1000;++i) cache.update(&lib,0,0,pos,200,200,turn,-0.3,mesh);
    qInfo() << "[tests:transfer-mesh] unchanged-cache mean us" << timer.nsecsElapsed()/1e6;
    TestTerrain extreme(0,0,2048,1,32); lib.tiles={&extreme}; pos[0]=pos[2]=0;
    check(cache.update(&lib,0,0,pos,100,100,turn,-0.3,mesh)
          && std::abs(area(mesh)-10000)<0.02
          && validUv(mesh,TransferMesh::Rectangle::fromQuaternion(100,100,turn)),
          "2048-samples-p32-rotated-transfer");
    timer.restart();
    for (int i=0;i<20;++i) {
        cache.invalidate(); cache.update(&lib,0,0,pos,100,100,turn,-0.3,mesh);
    }
    qInfo() << "[tests:transfer-mesh] rotated 100x100m / 1m terrain rebuild mean ms"
            << timer.nsecsElapsed()/20e6 << "vertices" << mesh.size()/9;
    extreme.lod(8);
    check(cache.update(&lib,0,0,pos,100,100,turn,-0.3,mesh) && std::abs(area(mesh)-10000)<0.02,
          "2048-samples-coarse-lod-transfer");
    { QScopedValueRollback<Game::TerrainMeshMode> legacy(Game::terrainMeshMode,Game::TERRAIN_MESH_LEGACY);
      check(cache.update(&lib,0,0,pos,100,100,turn,-0.3,mesh) && mesh.size()/9>50000,
            "legacy-terrain-uses-native-topology-not-last-paged-lod"); }
    qInfo() << "[tests:transfer-mesh] passed" << passed << "failed" << failed;
    {
        // Isolate the proposed extra scan over real, separately allocated objects.
        // Excludes rendering and uses a warm map; not a whole-frame benchmark.
        std::vector<std::unique_ptr<TransferObj>> objects;
        std::unordered_map<int,WorldObj*> entries;
        for (int i=0;i<10000;++i) {
            objects.emplace_back(new TransferObj);
            objects.back()->typeID=i%100==0 ? WorldObj::transfer : WorldObj::undefined;
            entries.emplace(i,objects.back().get());
        }
        volatile int matches=0;
        timer.restart();
        for (int repeat=0;repeat<1000;++repeat) {
            int count=0;
            for (int i=0;i<10000;++i) {
                const auto found=entries.find(i);
                if (found!=entries.end() && found->second && found->second->typeID==WorldObj::transfer) ++count;
            }
            matches=count;
        }
        qInfo() << "[tests:transfer-mesh] extra 10000-object scan mean ms"
                << timer.nsecsElapsed()/1e9 << "matches" << matches;
    }
    return failed?1:0;
}

int TsreTests::runTransferDepthGlSuite() {
    QOpenGLContext context;
    QSurfaceFormat format; format.setVersion(3,3); format.setProfile(QSurfaceFormat::CompatibilityProfile);
    context.setFormat(format);
    if (!context.create()) return 2;
    QOffscreenSurface surface; surface.setFormat(context.format()); surface.create();
    if (!context.makeCurrent(&surface)) return 2;
    auto *f=context.functions();
    auto *extra=context.extraFunctions();
    auto *gluu=GLUU::get();
    gluu->initShader(); gluu->currentShader=gluu->shaders["StandardFog"];
    if (!gluu->currentShader->bind()) return 2;
    Mat4::identity(gluu->pMatrix); Mat4::identity(gluu->fMatrix);
    Mat4::identity(gluu->pShadowMatrix); Mat4::identity(gluu->pShadowMatrix2);
    Mat4::identity(gluu->mvMatrix); Mat4::identity(gluu->objStrMatrix);
    gluu->alpha=1; gluu->setMatrixUniforms();
    gluu->currentShader->setUniformValue(gluu->currentShader->shaderShadowsEnabled,0.0f);
    gluu->currentShader->setUniformValue(gluu->currentShader->terrainPaged,0);
    // The active terrain uniform block needs backing storage even for ordinary vertices.
    GLuint ubo; extra->glGenBuffers(1,&ubo); extra->glBindBuffer(GL_UNIFORM_BUFFER,ubo);
    extra->glBufferData(GL_UNIFORM_BUFFER,8192,nullptr,GL_STATIC_DRAW);
    const auto block=extra->glGetUniformBlockIndex(gluu->currentShader->programId(),"TerrainPatchBlock");
    if (block!=GL_INVALID_INDEX) extra->glUniformBlockBinding(gluu->currentShader->programId(),block,0);
    int failed=0;
    {
        QOpenGLFramebufferObject target(16,16,QOpenGLFramebufferObject::CombinedDepthStencil);
        target.bind(); f->glViewport(0,0,16,16); f->glEnable(GL_DEPTH_TEST); f->glDepthFunc(GL_LESS);
        f->glClearColor(0,0,0,0);
        OpenGL3Renderer renderer; Mat4::identity(renderer.mvMatrix);
        QScopedValueRollback<Renderer*> current(Game::currentRenderer,&renderer);
        OglObj shape; shape.setMaterial(1,0,0);
        float vertices[]={-1,-1,0, 3,-1,0, -1,3,0};
        shape.init(vertices,9,RenderItem::V,GL_TRIANGLES);
        for (bool queued : {false,true}) for (bool decalEnabled : {false,true})
            for (quint32 selection : {0u,123u}) for (bool initialMask : {false,true})
            for (bool initialOffset : {false,true}) {
                f->glDepthMask(GL_TRUE); f->glClearDepthf(1); f->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                f->glDepthMask(initialMask ? GL_TRUE : GL_FALSE);
                f->glPolygonOffset(1.25f,3.5f);
                if (initialOffset) f->glEnable(GL_POLYGON_OFFSET_FILL);
                else f->glDisable(GL_POLYGON_OFFSET_FILL);
                extra->glBindBufferBase(GL_UNIFORM_BUFFER,0,ubo);
                shape.terrainDecal=decalEnabled;
                if (queued) { shape.pushRenderItem(selection); renderer.renderFrame(); }
                else shape.render(selection);
                GLboolean restored; f->glGetBooleanv(GL_DEPTH_WRITEMASK,&restored);
                GLfloat factor,units;
                f->glGetFloatv(GL_POLYGON_OFFSET_FACTOR,&factor);
                f->glGetFloatv(GL_POLYGON_OFFSET_UNITS,&units);
                float depth=0; f->glReadPixels(8,8,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depth);
                unsigned char colour[4]={}; f->glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,colour);
                const bool expectWrite=initialMask && (!decalEnabled || selection!=0);
                const bool ok=bool(restored)==initialMask && std::abs(depth-(expectWrite?0.5f:1.0f))<0.0001f
                        && bool(f->glIsEnabled(GL_POLYGON_OFFSET_FILL))==initialOffset
                        && factor==1.25f && units==3.5f
                        && colour[3]==255 && f->glGetError()==GL_NO_ERROR;
                if (!ok) ++failed;
                qInfo() << "[tests:transfer-depth-gl] queued/decal/selection/initialMask/initialOffset"
                        << queued << decalEnabled << selection << initialMask << initialOffset << "depth" << depth << "OK" << ok;
            }
        // Reproduce equal-depth rejection, then verify that the production decal
        // draw wins without changing the ground depth, in both rendering paths.
        for (bool queued : {false,true}) for (bool decal : {false,true}) {
            f->glDisable(GL_POLYGON_OFFSET_FILL); f->glDepthMask(GL_TRUE);
            f->glClearDepthf(1); f->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            auto draw=[&]() {
                extra->glBindBufferBase(GL_UNIFORM_BUFFER,0,ubo);
                if (queued) { shape.pushRenderItem(0); renderer.renderFrame(); }
                else shape.render(0);
            };
            shape.terrainDecal=false; shape.setMaterial(0,1,0); draw();
            shape.terrainDecal=decal; shape.setMaterial(1,0,0); draw();
            unsigned char colour[4]={}; f->glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,colour);
            float depth=0; f->glReadPixels(8,8,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depth);
            const bool ok=colour[decal?0:1]>200 && colour[decal?1:0]<20
                    && std::abs(depth-0.5f)<0.000001f && f->glGetError()==GL_NO_ERROR;
            if (!ok) ++failed;
            qInfo() << "[tests:transfer-depth-gl] coplanar ground, queued/decal" << queued << decal << "OK" << ok;
        }
        {
            TestTerrain ground(0,0); Library lib; lib.tiles={&ground};
            QScopedValueRollback<TerrainLib*> terrain(Game::terrainLib,&lib);
            QScopedValueRollback<bool> limits(Game::ignoreLoadLimits,true);
            QScopedValueRollback<QString> season(Game::season,QStringLiteral("Summer"));
            // Preloaded in-memory texture: no async image loading or route files.
            auto *texture=new Texture(2,2,24);
            texture->pathid="transfer-split-test/flat.png";
            const int textureId=TexLib::addTex(texture);
            {
                TransferObj transfer;
                transfer.x=transfer.y=0; transfer.loaded=true;
                transfer.width=transfer.height=100;
                transfer.resPath="transfer-split-test"; transfer.texture="flat.png";
                for (int i=0;i<3;++i) transfer.position[i]=transfer.qDirection[i]=0;
                transfer.qDirection[3]=1;
                float origin[]={0,0,0};
                auto submit=[&](int count,bool firstIsGround,quint32 selection,const char *name) {
                    transfer.pushRenderItems(0,0,0,origin,origin,1,selection);
                    bool ok=renderer.items.size()==count;
                    for (int i=0;i<renderer.items.size();++i) {
                        const auto *item=renderer.items[i];
                        ok &= item->vertCount>0 && item->selectionId==selection;
                        ok &= item->terrainDecal==(selection==0 && i==0 && firstIsGround);
                        if (!selection) ok &= texture->glLoaded && texture->tex && item->texAddr==texture->tex[0];
                    }
                    if (count && !selection) ok &= transfer.getTexId()==textureId;
                    extra->glBindBufferBase(GL_UNIFORM_BUFFER,0,ubo);
                    renderer.renderFrame();
                    ok &= f->glGetError()==GL_NO_ERROR;
                    if (!ok) ++failed;
                    qInfo() << "[tests:transfer-depth-gl]" << name << "OK" << ok;
                };
                submit(1,true,0,"solid-transfer-single-decal-draw");
                ground.gap(128,128);
                submit(2,true,0,"mixed-transfer-two-draws-shared-texture");
                submit(2,true,123,"both-parts-use-same-picking-id-without-decal-state");
                ground.hide(true);
                submit(1,false,0,"all-hidden-transfer-only-hole-draw-and-texture-picking");
                transfer.respectTerrainHoles=true;
                submit(0,false,0,"disabled-hole-cover-releases-both-empty-meshes");
                ground.hide(false);
                submit(1,true,0,"clipped-transfer-ground-only");
                transfer.respectTerrainHoles=false;
                submit(2,true,0,"reenabled-hole-cover-restores-second-draw");
                ground.gap(128,128,false);
                submit(1,true,0,"filled-hole-removes-second-draw");
            }
            // OglObj retains texture references; isolate this fixture's cache entry.
            texture->delVBO(); TexLib::mtex.erase(textureId); delete texture;
        }
        shape.deleteVBO(); f->glDepthMask(GL_TRUE); target.release();
        delete[] renderer.mvMatrix;
        delete[] renderer.objStrMatrix;
    }
    extra->glDeleteBuffers(1,&ubo);
    qInfo() << "[tests:transfer-depth-gl] cases 44 failed" << failed;
    return failed?1:0;
}
