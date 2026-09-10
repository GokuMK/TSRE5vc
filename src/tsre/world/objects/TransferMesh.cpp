#include "TransferMesh.h"
#include <tsre/world/Terrain.h>
#include <tsre/world/TerrainLib.h>
#include <tsre/world/TerrainMeshBackend.h>
#include <tsre/world/TerrainNormals.h>
#include <QSet>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <limits>

TransferMesh::Rectangle TransferMesh::Rectangle::fromQuaternion(double width, double height, const float *q) {
    // Heading of quaternion-rotated X, matching the old negative yaw in X/Z.
    const double norm=double(q[0])*q[0]+double(q[1])*q[1]+double(q[2])*q[2]+double(q[3])*q[3];
    if (!std::isfinite(norm) || norm<1e-16) return {};
    double c=1-2*(double(q[1])*q[1]+double(q[2])*q[2])/norm;
    double s=2*(double(q[0])*q[2]-double(q[3])*q[1])/norm;
    const double length=std::hypot(c,s);
    if (length<1e-10) { c=1; s=0; } else { c/=length; s/=length; }
    return {width,height,c,s};
}
bool TransferMesh::Rectangle::valid() const {
    return std::isfinite(width) && std::isfinite(height) && width>0 && height>0
            && std::isfinite(cosine) && std::isfinite(sine);
}
void TransferMesh::Rectangle::uv(Vertex &p) const {
    p.u=(p.x*cosine+p.z*sine)/width+0.5;
    p.v=(-p.x*sine+p.z*cosine)/height+0.5;
}
void TransferMesh::clipTriangle(const Rectangle &rect, std::array<Vertex,3> triangle,
                                float alpha, QVector<float> &vertices, QVector<float> &outline) {
    if (!rect.valid()) return;
    std::array<Vertex,8> polygon{}, next{};
    int count=3;
    for (int i=0;i<3;++i) { rect.uv(triangle[i]); polygon[i]=triangle[i]; }
    for (int side=0;side<4 && count>=3;++side) {
        auto distance=[side](const Vertex &p) { return side==0 ? p.u : side==1 ? 1-p.u : side==2 ? p.v : 1-p.v; };
        int out=0;
        Vertex previous=polygon[count-1]; double dp=distance(previous);
        for (int i=0;i<count;++i) {
            const Vertex current=polygon[i]; const double dc=distance(current);
            if ((dp>=0)!=(dc>=0)) {
                const double t=dp/(dp-dc);
                auto mix=[t](double a,double b) { return a+(b-a)*t; };
                Vertex p{mix(previous.x,current.x),mix(previous.y,current.y),mix(previous.z,current.z),
                         mix(previous.nx,current.nx),mix(previous.ny,current.ny),mix(previous.nz,current.nz),
                         mix(previous.u,current.u),mix(previous.v,current.v)};
                if (side==0) p.u=0; if (side==1) p.u=1;
                if (side==2) p.v=0; if (side==3) p.v=1;
                next[out++]=p;
            }
            if (dc>=0) next[out++]=current;
            previous=current; dp=dc;
        }
        polygon=next; count=out;
    }
    if (count<3) return;
    const qsizetype originalSize=vertices.size();
    auto appendVertex=[&](const Vertex &p) {
        vertices << float(p.x) << float(p.y+0.05) << float(p.z)
                 << float(p.nx) << float(p.ny) << float(p.nz)
                 << float(std::clamp(p.u,0.0,1.0)) << float(std::clamp(p.v,0.0,1.0)) << alpha;
    };
    for (int i=1;i+1<count;++i) {
        const auto &a=polygon[0], &b=polygon[i], &c=polygon[i+1];
        const double area=(b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z);
        if (std::abs(area)<1e-12) continue;
        appendVertex(a); appendVertex(b); appendVertex(c);
    }
    if (vertices.size()==originalSize) return;
    for (int i=0;i<count;++i) {
        const auto &a=polygon[i], &b=polygon[(i+1)%count];
        const auto same=[](double a,double b,double edge) { return std::abs(a-edge)<1e-9 && std::abs(b-edge)<1e-9; };
        if (same(a.u,b.u,0) || same(a.u,b.u,1) || same(a.v,b.v,0) || same(a.v,b.v,1))
            outline << float(a.x) << float(a.y+0.5) << float(a.z)
                    << float(b.x) << float(b.y+0.5) << float(b.z);
    }
}
bool TransferMesh::Patch::operator==(const Patch &b) const {
    return terrain==b.terrain && revision==b.revision && id==b.id
            && originX==b.originX && originZ==b.originZ && hidden==b.hidden
            && lod.sourceStep==b.lod.sourceStep && lod.edgeMask==b.lod.edgeMask;
}
bool TransferMesh::update(TerrainLib *lib, int worldX, int worldZ, const float *position,
                          double width, double height, const float *q, float alpha, QVector<float> &vertices,
                          QVector<float> &holeVertices,
                          bool respectTerrainHoles) {
    const Rectangle rect=Rectangle::fromQuaternion(width,height,q);
    const std::array<double,10> current{double(worldX),double(worldZ),position[0],position[2],
                                    width,height,rect.cosine,rect.sine,alpha,double(respectTerrainHoles)};
    QVector<Patch> patches;
    const double extentX=(std::abs(rect.cosine)*width+std::abs(rect.sine)*height)/2;
    const double extentZ=(std::abs(rect.sine)*width+std::abs(rect.cosine)*height)/2;
    // Bounds are relative to the object's World tile, never route-global floats.
    if (lib && rect.valid() && std::isfinite(position[0]) && std::isfinite(position[2])
            && extentX<=65536 && extentZ<=65536) {
        const double firstX=std::floor((position[0]-extentX+1024)/2048);
        const double lastX=std::floor((position[0]+extentX+1024)/2048);
        const double firstZ=std::floor((position[2]-extentZ+1024)/2048);
        const double lastZ=std::floor((position[2]+extentZ+1024)/2048);
        if (firstX+worldX>=std::numeric_limits<int>::min() && lastX+worldX<=std::numeric_limits<int>::max()
                && firstZ+worldZ>=std::numeric_limits<int>::min() && lastZ+worldZ<=std::numeric_limits<int>::max()
                && (lastX-firstX+1)*(lastZ-firstZ+1)<=4096) {
            QSet<Terrain*> seen;
            for (qint64 z=qint64(firstZ)+worldZ;z<=qint64(lastZ)+worldZ;++z)
                for (qint64 x=qint64(firstX)+worldX;x<=qint64(lastX)+worldX;++x) {
                    Terrain *terrain=lib->getTerrainByXY(int(x),int(z),false);
                    if (!terrain || !terrain->loaded || !terrain->terrainData || terrain->lowTile || seen.contains(terrain)) continue;
                    seen.insert(terrain);
                    const auto &grid=terrain->getGridLayout();
                    if (grid.patchWorldSize<=0) continue;
                    float cx=position[0], cz=position[2]; terrain->getLocalCoords(worldX,worldZ,cx,cz);
                    const int x0=std::max(0,int(std::floor((cx-extentX)/grid.patchWorldSize)));
                    const int x1=std::min(grid.patchesPerSide-1,int(std::floor((cx+extentX)/grid.patchWorldSize)));
                    const int z0=std::max(0,int(std::floor((cz-extentZ)/grid.patchWorldSize)));
                    const int z1=std::min(grid.patchesPerSide-1,int(std::floor((cz+extentZ)/grid.patchWorldSize)));
                    for (int pz=z0;pz<=z1;++pz) for (int px=x0;px<=x1;++px) {
                        const int id=pz*grid.patchesPerSide+px;
                        patches.append({terrain,terrain->surfaceRevision(),id,-cx,-cz,
                                        terrain->surfacePatchLod(id),terrain->surfacePatchHidden(id)});
                    }
                }
        }
    }
    if (validCache && placement==current && sources==patches) return false;
    validCache=true; placement=current; sources=patches; vertices.clear(); boundary.clear();
    holeVertices.clear();
    // One template per used R/LOD/mask for this build, shared by all its patches.
    QHash<int,QVector<quint16>> templates;
    for (const auto &patch : patches) {
        if (respectTerrainHoles && patch.hidden) continue;
        Terrain *terrain=patch.terrain.data(); const auto &grid=terrain->getGridLayout();
        const int r=grid.patchResolution, side=r+1;
        const int key=(r<<16)|(patch.lod.sourceStep<<4)|patch.lod.edgeMask;
        if (!templates.contains(key)) {
            auto indices=TerrainMeshPaged::buildLodIndices(r,patch.lod.sourceStep,patch.lod.edgeMask);
            if (indices.isEmpty()) indices=TerrainMeshPaged::buildRegularIndices(r);
            templates.insert(key,indices);
        }
        const auto &indices=templates[key];
        const int sx0=grid.patchColumn(patch.id)*r, sz0=grid.patchRow(patch.id)*r;
        const bool uniform=TerrainNormals::uniformCoordinates(grid.sampleCount,grid.sampleSpacing);
        QVector<Vertex> samples(side*side); QVector<quint8> ready(side*side,0);
        for (int i=0;i+2<indices.size();i+=3) {
            std::array<Vertex,3> triangle;
            bool gap=false;
            for (int v=0;v<3;++v) {
                const int index=indices[i+v], sx=sx0+index%side, sz=sz0+index/side;
                gap |= terrain->surfaceSampleGap(sx,sz);
                triangle[v]={patch.originX+double(sx)*grid.sampleSpacing,terrain->terrainData[sz][sx],
                             patch.originZ+double(sz)*grid.sampleSpacing};
            }
            if (respectTerrainHoles && gap) continue;
            if (std::all_of(triangle.begin(),triangle.end(),[&](const Vertex &p) { return p.x < -extentX; })
                    || std::all_of(triangle.begin(),triangle.end(),[&](const Vertex &p) { return p.x > extentX; })
                    || std::all_of(triangle.begin(),triangle.end(),[&](const Vertex &p) { return p.z < -extentZ; })
                    || std::all_of(triangle.begin(),triangle.end(),[&](const Vertex &p) { return p.z > extentZ; })) continue;
            for (int v=0;v<3;++v) {
                const int index=indices[i+v];
                if (!ready[index]) {
                    const auto normal=TerrainNormals::calculate(terrain->terrainData,grid.sampleCount,
                                grid.sampleSpacing,sx0+index%side,sz0+index/side,uniform);
                    samples[index]=triangle[v]; samples[index].nx=normal.x; samples[index].ny=normal.y; samples[index].nz=normal.z;
                    ready[index]=1;
                }
                triangle[v]=samples[index];
            }
            // Classify once, before clipping. Both parts use identical positions,
            // normals, UVs and lift; no triangle is emitted into both buffers.
            auto &output=(gap || patch.hidden) ? holeVertices : vertices;
            clipTriangle(rect,triangle,alpha,output,boundary);
            if (vertices.size()+holeVertices.size()>16*1024*1024) {
                qWarning() << "Transfer mesh exceeds 64 MiB budget; reduce transfer dimensions";
                vertices.clear(); holeVertices.clear(); boundary.clear(); return true;
            }
        }
    }
    return true;
}
