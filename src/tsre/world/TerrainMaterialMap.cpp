#include "TerrainMaterialMap.h"
#include <QCryptographicHash>
#include <QFile>
#include <QSaveFile>
#include <QVector>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#define MINIZ_HEADER_FILE_ONLY
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <mzip/miniz/miniz.h>

namespace {
constexpr int HeaderSize = 20;
constexpr int FileSizeSlack = 256 * 1024;
bool layoutValid(int patches, int side) { return patches > 0 && patches <= 32 && side % patches == 0; }
static_assert(TerrainMaterialMap::SamplingMode >= 1 && TerrainMaterialMap::SamplingMode <= 4);
// Future shader importance belongs here; IDs themselves are never interpolated.
double materialImportance(quint8 id) { return double(id) + 1.0; }
quint32 scatterHash(quint32 value) {
    value ^= value >> 16; value *= 0x7feb352du;
    value ^= value >> 15; value *= 0x846ca68bu;
    return value ^ (value >> 16);
}
}

bool TerrainMaterialMap::supportedSide(int value) {
    return value >= MinimumSide && value <= MaximumSide && (value & (value-1)) == 0;
}
bool TerrainMaterialMap::initialize(quint8 id, int requestedSide) {
    if (!supportedSide(requestedSide)) { ids.clear(); return false; }
    mapSide=requestedSide;
    ids=QByteArray(mapSide*mapSide,char(id));
    return true;
}
bool TerrainMaterialMap::setIds(const QByteArray &values) {
    const qsizetype count=values.size();
    const int inferred=int(std::sqrt(double(count)));
    if (!supportedSide(inferred) || qsizetype(inferred)*inferred!=count) return false;
    mapSide=inferred;
    ids=values;
    return true;
}
bool TerrainMaterialMap::valid() const {
    return supportedSide(mapSide) && ids.size()==qsizetype(mapSide)*mapSide;
}
quint8 TerrainMaterialMap::at(int x, int z) const {
    return valid() ? quint8(ids[std::clamp(z,0,mapSide-1)*mapSide+std::clamp(x,0,mapSide-1)]) : 0;
}
QSet<int> TerrainMaterialMap::usedIds() const {
    QSet<int> result;
    bool seen[256]{};
    for (char id : ids) seen[quint8(id)] = true;
    for (int i = 0; i < 256; ++i) if (seen[i]) result.insert(i);
    return result;
}
QByteArray TerrainMaterialMap::encode() const {
    if (!valid()) return {};
    QByteArray result(HeaderSize, '\0');
    memcpy(result.data(), "TSREPMAP", 8);
    qToLittleEndian<quint32>(1, result.data()+8);
    qToLittleEndian<quint32>(mapSide, result.data()+12);
    qToLittleEndian<quint32>(mapSide, result.data()+16);
    // qCompress's four-byte length is redundant: the versioned header fixes it.
    // Same zlib/file format, favor interactive saves over maximum compression.
    result += qCompress(ids, 1).mid(4);
    return result;
}
bool TerrainMaterialMap::decode(const QByteArray &file, QByteArray &out, QString &error, int *decodedSide) {
    if (file.size() <= HeaderSize || file.size() > MaximumSide*MaximumSide+FileSizeSlack
        || memcmp(file.constData(), "TSREPMAP", 8) != 0
        || qFromLittleEndian<quint32>(file.constData()+8) != 1) {
        error = "Unsupported procedural material bitmap header/size"; return false;
    }
    const quint32 width=qFromLittleEndian<quint32>(file.constData()+12);
    const quint32 height=qFromLittleEndian<quint32>(file.constData()+16);
    if (width!=height || width>quint32(std::numeric_limits<int>::max()) || !supportedSide(int(width))) {
        error="Unsupported procedural material bitmap dimensions"; return false;
    }
    const int side=int(width);
    QByteArray decoded(side*side,'\0');
    mz_stream stream{};
    stream.next_in = reinterpret_cast<const unsigned char*>(file.constData()+HeaderSize);
    stream.avail_in = file.size()-HeaderSize;
    stream.next_out = reinterpret_cast<unsigned char*>(decoded.data());
    stream.avail_out = decoded.size();
    if (mz_inflateInit(&stream) != MZ_OK) { error = "Cannot initialize bitmap decompressor"; return false; }
    const int status = mz_inflate(&stream, MZ_FINISH);
    const bool ok=status==MZ_STREAM_END && stream.total_out==mz_ulong(side)*side && stream.avail_in==0;
    mz_inflateEnd(&stream);
    if (!ok) { error = "Damaged procedural material bitmap (bounded decompression failed)"; return false; }
    out = std::move(decoded);
    if (decodedSide) *decodedSide=side;
    return true;
}
bool TerrainMaterialMap::read(const QString &path, QString &error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size()>qint64(MaximumSide)*MaximumSide+FileSizeSlack) {
        error = "Cannot read procedural material bitmap: " + path; return false;
    }
    QByteArray decoded;
    int decodedSide=0;
    if (!decode(file.readAll(),decoded,error,&decodedSide)) return false;
    mapSide=decodedSide;
    ids=std::move(decoded);
    return true;
}
bool TerrainMaterialMap::write(const QString &path, QString &error) const {
    const QByteArray bytes = encode();
    QSaveFile file(path);
    if (bytes.isEmpty() || !file.open(QIODevice::WriteOnly)
        || file.write(bytes) != bytes.size() || !file.commit()) {
        error = "Cannot save procedural material bitmap: " + path; return false;
    }
    return true;
}
QSet<int> TerrainMaterialMap::paint(double x, double z, double radius, int id,
                                    int patches, const QImage &mask, const QSet<int> &locked, bool dryRun, int mode) {
    QSet<int> changed;
    if (!valid() || !layoutValid(patches,mapSide) || id < 0 || id > 255 || radius <= 0
        || !std::isfinite(x) || !std::isfinite(z) || !std::isfinite(radius)) return changed;
    const int loX=int(std::clamp(std::floor(x-radius),0.,double(mapSide)));
    const int loZ=int(std::clamp(std::floor(z-radius),0.,double(mapSide)));
    const int hiX=int(std::clamp(std::ceil(x+radius),0.,double(mapSide)));
    const int hiZ=int(std::clamp(std::ceil(z+radius),0.,double(mapSide)));
    const int side=mapSide/patches;
    for (int iz = loZ; iz < hiZ; ++iz) {
        const int my = mask.isNull() ? 0 : std::clamp(int((iz+.5-z+radius)*mask.height()/(2*radius)), 0, mask.height()-1);
        for (int ix = loX; ix < hiX; ++ix) {
            const int patch = (iz/side)*patches + ix/side;
            if (locked.contains(patch) || quint8(ids.at(iz*mapSide+ix)) == id) continue;
            if (!mask.isNull()) {
                const int mx = std::clamp(int((ix+.5-x+radius)*mask.width()/(2*radius)), 0, mask.width()-1);
                if (qGray(mask.pixel(mx, my)) == 255) continue;
            }
            if (!dryRun) ids[iz*mapSide+ix] = char(id);
            if (mode == 3) changed.insert(patch);
            else {
                // A changed border texel also influences the next patch's
                // filter footprint, including diagonal neighbours at corners.
                for (int pz = std::max(0,(iz-1)/side); pz <= std::min(patches-1,(iz+1)/side); ++pz)
                    for (int px = std::max(0,(ix-1)/side); px <= std::min(patches-1,(ix+1)/side); ++px)
                        changed.insert(pz*patches+px);
            }
        }
    }
    return changed;
}
QSet<int> TerrainMaterialMap::fill(int x, int z, int id, int patches, bool patchOnly,
                                  const QSet<int> &locked, bool dryRun, int mode) {
    QSet<int> changed;
    if (!valid() || !layoutValid(patches,mapSide) || id<0 || id>255 || x<0 || z<0 || x>=mapSide || z>=mapSide)
        return changed;
    const int side=mapSide/patches, patch=(z/side)*patches+x/side;
    if (locked.contains(patch)) return changed;
    const char replacement=char(id), original=ids.at(z*mapSide+x);
    if (!patchOnly && original==replacement) return changed;
    if (!patchOnly && dryRun) { changed.insert(patch); return changed; }
    const int halo=mode==3 ? 0 : 1;
    auto dirtySpan=[&](int left,int right,int row) {
        for (int pz=std::max(0,row-halo)/side; pz<=std::min(mapSide-1,row+halo)/side; ++pz)
            for (int px=std::max(0,left-halo)/side; px<=std::min(mapSide-1,right+halo)/side; ++px)
                changed.insert(pz*patches+px);
    };
    if (patchOnly) {
        const int startX=(x/side)*side, startZ=(z/side)*side;
        for (int row=startZ; row<startZ+side; ++row) {
            int first=startX+side,last=-1;
            for (int col=startX; col<startX+side; ++col) if (ids.at(row*mapSide+col)!=replacement) {
                if (dryRun) { changed.insert(patch); return changed; }
                ids[row*mapSide+col]=replacement;
                first=std::min(first,col); last=col;
            }
            if (last>=first) dirtySpan(first,last,row);
        }
        return changed;
    }
    // Iterative scanline flood: recolour when scheduling a span, so the same
    // sample is never queued twice. No recursive stack or per-pixel visited map.
    bool blocked[1024]{};
    for (int p : locked) if (p>=0 && p<patches*patches) blocked[p]=true;
    char *pixels=ids.data();
    auto matches=[&](int col,int row) {
        return pixels[row*mapSide+col]==original && !blocked[(row/side)*patches+col/side];
    };
    struct Span { int left,right,row; };
    QVector<Span> pending;
    auto schedule=[&](int col,int row) {
        int left=col,right=col;
        while (left>0 && matches(left-1,row)) --left;
        while (right+1<mapSide && matches(right+1,row)) ++right;
        std::fill(pixels+row*mapSide+left,pixels+row*mapSide+right+1,replacement);
        dirtySpan(left,right,row);
        pending.push_back({left,right,row});
        return right;
    };
    schedule(x,z);
    while (!pending.isEmpty()) {
        const Span span=pending.takeLast();
        for (int row : {span.row-1,span.row+1}) {
            if (row<0 || row>=mapSide) continue;
            for (int col=span.left;col<=span.right;++col)
                if (matches(col,row)) col=schedule(col,row);
        }
    }
    return changed;
}
quint8 TerrainMaterialMap::sampleId(double x, double z, int mode, quint32 seed) const {
    if (!valid() || !std::isfinite(x) || !std::isfinite(z)) return 0;
    x=std::clamp(x,0.0,double(mapSide)); z=std::clamp(z,0.0,double(mapSide));
    if (mode == 3) return at(int(x),int(z));
    const int ix = int(std::floor(x-.5)), iz = int(std::floor(z-.5));
    const double fx = x-.5-ix, fz = z-.5-iz;
    const quint8 neighbours[] = {at(ix,iz),at(ix+1,iz),at(ix,iz+1),at(ix+1,iz+1)};
    if (neighbours[0]==neighbours[1] && neighbours[0]==neighbours[2] && neighbours[0]==neighbours[3])
        return neighbours[0];
    const double weights[] = {(1-fx)*(1-fz),fx*(1-fz),(1-fx)*fz,fx*fz};
    quint8 ids[4]{}; double support[4]{}; int count=0;
    for (int i=0;i<4;++i) {
        if (weights[i] <= 0) continue;
        int j=0;
        while (j<count && ids[j]!=neighbours[i]) ++j;
        if (j==count) ids[count++]=neighbours[i];
        support[j]+=weights[i];
    }
    if (mode == 2) {
        // Position-derived noise, never a mutable random generator or frame seed.
        double probability = double(scatterHash(seed)) / 4294967296.0;
        for (int i=0;i<count-1;++i) {
            if (probability < support[i]) return ids[i];
            probability -= support[i];
        }
        return ids[count-1];
    }
    int best=0;
    for (int i=1;i<count;++i) {
        const double score=support[i]*(mode==4 ? 1.0 : materialImportance(ids[i]));
        const double bestScore=support[best]*(mode==4 ? 1.0 : materialImportance(ids[best]));
        if (score>bestScore || (score==bestScore && ids[i]>ids[best])) best=i;
    }
    return ids[best];
}
QByteArray TerrainMaterialMap::patchKey(int patch, int patches, int mode) const {
    if (!valid() || !layoutValid(patches,mapSide) || patch < 0 || patch >= patches*patches) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const int side=mapSide/patches, x=(patch%patches)*side, z=(patch/patches)*side;
    QByteArray settings(12,'\0');
    qToLittleEndian<quint32>(mode,settings.data());
    qToLittleEndian<quint32>(side,settings.data()+4);
    qToLittleEndian<quint32>(OutputSide,settings.data()+8);
    hash.addData(settings);
    if (mode == 3) {
        for (int row=0;row<side;++row) hash.addData(QByteArrayView(ids.constData()+(z+row)*mapSide+x,side));
    } else {
        // Hash exactly the dependencies read by sampleId, with tile-edge clamp.
        QByteArray row(side+2,'\0');
        for (int rz=-1;rz<=side;++rz) {
            const char *src=ids.constData()+std::clamp(z+rz,0,mapSide-1)*mapSide;
            row[0]=src[std::max(0,x-1)];
            memcpy(row.data()+1,src+x,side);
            row[side+1]=src[std::min(mapSide-1,x+side)];
            hash.addData(row);
        }
    }
    return hash.result();
}
QImage TerrainMaterialMap::generate(int patch, int patches, const QHash<int, QImage> &sources, int mode) const {
    return generateAtSize(patch,patches,sources,OutputSide,mode);
}
QImage TerrainMaterialMap::generateAtSize(int patch, int patches, const QHash<int,QImage> &sources,
                                          int outputSide, int mode) const {
    if (!valid() || !layoutValid(patches,mapSide) || patch<0 || patch>=patches*patches
            || outputSide<=0) return {};
    QImage output(outputSide,outputSide,QImage::Format_RGB888);
    const int side=mapSide/patches, x=(patch%patches)*side, z=(patch/patches)*side;
    const QImage *images[256]{};
    for (auto it = sources.constBegin(); it != sources.constEnd(); ++it)
        if (it.key() >= 0 && it.key() <= 255 && !it.value().isNull()) images[it.key()] = &it.value();
    for (int row=0;row<outputSide;++row) {
        unsigned char *dst = output.scanLine(row);
        const double sampleZ=z+(row+.5)*side/outputSide;
        for (int col=0;col<outputSide;++col) {
            const double sampleX=x+(col+.5)*side/outputSide;
            // Patch-local pattern intentionally repeats for identical recipes,
            // preserving cache sharing across patches and tiles.
            const quint32 seed=quint32(col)*0x9e3779b9u ^ quint32(row)*0x85ebca6bu ^ 0x73518u;
            const QImage *source = images[sampleId(sampleX,sampleZ,mode,seed)];
            if (!source) return {};
            const QRgb color=source->pixel(((2*col+1)*source->width())/(2*outputSide),
                                           ((2*row+1)*source->height())/(2*outputSide));
            *dst++ = qRed(color); *dst++ = qGreen(color); *dst++ = qBlue(color);
        }
    }
    return output;
}
QImage TerrainMaterialMap::bake(int patches, const QHash<int,QImage> &sources,
                              const QHash<QByteArray,QImage> &miniatures,
                              const QImage &previous, const QSet<int> &dirtyPatches,
                              QVector<QByteArray> *recipeKeys) const {
    if (!valid() || patches<=0 || BakedSide%patches || mapSide%patches) return {};
    const bool incremental=previous.size()==QSize(BakedSide,BakedSide)
            && previous.format()==QImage::Format_RGB888;
    QImage tile=incremental ? previous : QImage(BakedSide,BakedSide,QImage::Format_RGB888);
    if (recipeKeys && recipeKeys->size()!=patches*patches) recipeKeys->fill({},patches*patches);
    const int side=BakedSide/patches;
    // Cache only reduced recipes: even a completely unique tile costs one extra
    // tile image, not a P*512-square intermediate or resident patch textures.
    QHash<QByteArray,QImage> reduced=miniatures;
    for (int patch=0;patch<patches*patches;++patch) {
        if (incremental && !dirtyPatches.contains(patch)) continue;
        QByteArray key=recipeKeys ? recipeKeys->at(patch) : QByteArray();
        if (key.isEmpty()) {
            key=patchKey(patch,patches);
            if (recipeKeys) (*recipeKeys)[patch]=key;
        }
        auto found=reduced.constFind(key);
        QImage image;
        if (found!=reduced.constEnd() && found->size()==QSize(side,side)
                && found->format()==QImage::Format_RGB888) image=*found;
        else {
            if (BakeSamplingMode==BakeSampling::FullOutput) {
                image=generate(patch,patches,sources);
                if (image.isNull()) return {};
                image=image.scaled(side,side,Qt::IgnoreAspectRatio,Qt::SmoothTransformation)
                        .convertToFormat(QImage::Format_RGB888);
            } else {
                const QImage supersampled=generateAtSize(patch,patches,sources,side*2);
                if (supersampled.isNull()) return {};
                image=QImage(side,side,QImage::Format_RGB888);
                for (int y=0;y<side;++y) {
                    auto *dst=image.scanLine(y);
                    const auto *a=supersampled.constScanLine(y*2);
                    const auto *b=supersampled.constScanLine(y*2+1);
                    for (int x=0;x<side;++x) for (int channel=0;channel<3;++channel)
                        *dst++=uchar((int(a[x*6+channel])+a[x*6+3+channel]
                                    +b[x*6+channel]+b[x*6+3+channel]+2)/4);
                }
            }
            reduced.insert(key,image);
        }
        for (int y=0;y<side;++y)
            memcpy(tile.scanLine((patch/patches)*side+y)+(patch%patches)*side*3,
                   image.constScanLine(y),side*3);
    }
    return tile;
}
QString TerrainMaterialMap::textureKey(const QImage &rgb, bool bc1) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const QImage image = rgb.convertToFormat(QImage::Format_RGB888);
    for (int y = 0; y < image.height(); ++y) hash.addData(QByteArrayView(reinterpret_cast<const char*>(image.constScanLine(y)), image.width()*3));
    return QString("terrain-proc:v1:%1x%2:%3:sha256:%4").arg(image.width()).arg(image.height())
            .arg(bc1 ? "bc1" : "rgb").arg(QString::fromLatin1(hash.result().toHex()));
}
QByteArray TerrainMaterialMap::encodeBC1(const QImage &input) {
    const QImage rgb = input.convertToFormat(QImage::Format_RGB888);
    if (rgb.isNull() || rgb.width()%4 || rgb.height()%4) return {};
    QByteArray result(rgb.width()*rgb.height()/2, '\0');
    unsigned char *out = reinterpret_cast<unsigned char*>(result.data());
    auto pack = [](const int *c) { return quint16(((c[0]>>3)<<11) | ((c[1]>>2)<<5) | (c[2]>>3)); };
    auto unpack = [](quint16 c, int *v) {
        v[0] = ((c>>11)*255+15)/31; v[1] = (((c>>5)&63)*255+31)/63; v[2] = ((c&31)*255+15)/31;
    };
    // Fast opaque BC1 fit using actual farthest colors, not component-wise bounds:
    // a red/blue block needs red/blue endpoints, not invented magenta/black ones.
    for (int y = 0; y < rgb.height(); y += 4) for (int x = 0; x < rgb.width(); x += 4) {
        int pixels[16][3];
        for (int j = 0; j < 4; ++j) for (int i = 0; i < 4; ++i) {
            const auto p = rgb.constScanLine(y+j)+(x+i)*3;
            for (int c=0;c<3;++c) pixels[j*4+i][c]=p[c];
        }
        auto farthest = [&](int from) {
            int best=from, distance=-1;
            for(int i=0;i<16;++i) {
                int d=0;
                for(int c=0;c<3;++c) { int delta=pixels[i][c]-pixels[from][c];d+=delta*delta; }
                if(d>distance) {distance=d;best=i;}
            }
            return best;
        };
        const int endpoint=farthest(0);
        quint16 a = pack(pixels[endpoint]), b = pack(pixels[farthest(endpoint)]);
        if (a == b) { if (a < 65535) ++a; else --b; }
        if (a < b) std::swap(a,b);
        int palette[4][3]; unpack(a,palette[0]); unpack(b,palette[1]);
        for (int c = 0; c < 3; ++c) { palette[2][c]=(2*palette[0][c]+palette[1][c])/3; palette[3][c]=(palette[0][c]+2*palette[1][c])/3; }
        quint32 indices = 0;
        for (int j = 0; j < 4; ++j) for (int i = 0; i < 4; ++i) {
            const auto p = rgb.constScanLine(y+j)+(x+i)*3;
            int best = 0, score = 1000000;
            for (int k = 0; k < 4; ++k) {
                int dist = 0;
                for (int c = 0; c < 3; ++c) { const int d = int(p[c])-palette[k][c]; dist += d*d; }
                if (dist < score) { score = dist; best = k; }
            }
            indices |= quint32(best) << ((j*4+i)*2);
        }
        qToLittleEndian(a,out); qToLittleEndian(b,out+2); qToLittleEndian(indices,out+4); out += 8;
    }
    return result;
}
