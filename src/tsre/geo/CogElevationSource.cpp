#include <tsre/geo/CogElevationSource.h>
#include <tsre/geo/ElevationDownload.h>
#include <tsre/geo/ElevationSource.h>
#include <tsre/geo/ElevationTiffCodec.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QUrlQuery>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Elevation {
namespace {
constexpr quint64 IndexBytes = 256*1024;
constexpr qint64 MaxCogRange = 4*1024*1024;
constexpr quint64 MaxWholeTiff = 32*1024*1024;

bool save(const QString &path, const QByteArray &bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size() && file.commit();
}
QByteArray read(const QString &path, quint64 offset=0, quint64 length=std::numeric_limits<quint64>::max()) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || offset>quint64(file.size()) || !file.seek(qint64(offset))) return {};
    const quint64 available=quint64(file.size())-offset;
    return file.read(qint64(std::min(length,available)));
}
bool inside(const Dataset &d, XY p) {
    return p.x>=d.minX && p.y>=d.minY && p.x<d.maxX && p.y<d.maxY;
}

struct Tag { quint16 type=0; quint64 count=0, offset=0; };
struct CogIndex {
    bool little=true, big=false, hasNoData=false;
    int width=0,height=0,bits=0,sampleFormat=0,compression=0,predictor=1;
    int tileWidth=0,tileHeight=0,epsg=0;
    float noData=std::numeric_limits<float>::quiet_NaN();
    std::array<double,6> transform{{0,1,0,0,0,-1}};
    QVector<quint64> offsets,byteCounts;
};

class Header {
public:
    explicit Header(const QByteArray &value): bytes(value) {}
    bool little=true,big=false;
    bool range(quint64 p,quint64 n) const { return p<=quint64(bytes.size()) && n<=quint64(bytes.size())-p; }
    quint16 u16(quint64 p) const { return little?qFromLittleEndian<quint16>(bytes.constData()+p):qFromBigEndian<quint16>(bytes.constData()+p); }
    quint32 u32(quint64 p) const { return little?qFromLittleEndian<quint32>(bytes.constData()+p):qFromBigEndian<quint32>(bytes.constData()+p); }
    quint64 u64(quint64 p) const { return little?qFromLittleEndian<quint64>(bytes.constData()+p):qFromBigEndian<quint64>(bytes.constData()+p); }
    double f64(quint64 p) const { quint64 v=u64(p);double d;std::memcpy(&d,&v,8);return d; }
    const QByteArray &bytes;
};

bool parseCogIndex(const QByteArray &bytes, int expectedEpsg, CogIndex &out, QString &error) {
    error.clear();
    if (bytes.size()<16) { error=QStringLiteral("Truncated COG header"); return false; }
    Header rd(bytes);
    if (bytes.startsWith("II")) rd.little=true;
    else if (bytes.startsWith("MM")) rd.little=false;
    else { error=QStringLiteral("Response is not a TIFF raster"); return false; }
    const quint16 magic=rd.u16(2);
    rd.big=magic==43;
    if (magic!=42 && magic!=43) { error=QStringLiteral("Unsupported TIFF header"); return false; }
    if (rd.big && (rd.u16(4)!=8 || rd.u16(6)!=0)) { error=QStringLiteral("Invalid BigTIFF header"); return false; }
    const quint64 ifd=rd.big?rd.u64(8):rd.u32(4);
    const quint64 prefix=rd.big?8:2, entrySize=rd.big?20:12, inlineSize=rd.big?8:4;
    if (!rd.range(ifd,prefix)) { error=QStringLiteral("Invalid COG directory"); return false; }
    const quint64 count=rd.big?rd.u64(ifd):rd.u16(ifd);
    if (count>256 || !rd.range(ifd+prefix,count*entrySize+(rd.big?8:4))) {
        error=QStringLiteral("Invalid COG directory size"); return false;
    }
    const int sizes[]={0,1,1,2,4,8,1,1,2,4,8,4,8,0,0,0,8,8,8};
    QMap<int,Tag> tags;
    for (quint64 i=0;i<count;++i) {
        const quint64 p=ifd+prefix+i*entrySize;
        Tag t; const int id=rd.u16(p);t.type=rd.u16(p+2);t.count=rd.big?rd.u64(p+4):rd.u32(p+4);
        if (t.type>=sizeof(sizes)/sizeof(sizes[0]) || !sizes[t.type] || t.count>16*1024*1024) {
            error=QStringLiteral("Unsupported COG field"); return false;
        }
        const quint64 size=t.count*quint64(sizes[t.type]);
        const quint64 valuePos=p+(rd.big?12:8);
        t.offset=size<=inlineSize?valuePos:(rd.big?rd.u64(valuePos):rd.u32(valuePos));
        if (!rd.range(t.offset,size) || tags.contains(id)) { error=QStringLiteral("COG index exceeds metadata range"); return false; }
        tags.insert(id,t);
    }
    const auto integer=[&](int id,quint64 fallback,quint64 index=0)->quint64 {
        const Tag t=tags.value(id);if(index>=t.count)return fallback;
        if(t.type==3)return rd.u16(t.offset+2*index);
        if(t.type==4)return rd.u32(t.offset+4*index);
        if(t.type==16)return rd.u64(t.offset+8*index);
        return fallback;
    };
    CogIndex c;c.little=rd.little;c.big=rd.big;
    c.width=int(integer(256,0));c.height=int(integer(257,0));c.bits=int(integer(258,0));
    c.sampleFormat=int(integer(339,1));c.compression=int(integer(259,1));c.predictor=int(integer(317,1));
    c.tileWidth=int(integer(322,0));c.tileHeight=int(integer(323,0));
    if(c.width<=0||c.height<=0||c.width>1000000||c.height>1000000||c.bits!=32||c.sampleFormat!=3
            ||c.compression!=5||(c.predictor!=1&&c.predictor!=3)||c.tileWidth<=0||c.tileHeight<=0
            ||qint64(c.tileWidth)*c.tileHeight>4*1024*1024||integer(277,1)!=1||integer(284,1)!=1) {
        error=QStringLiteral("Unsupported COG raster profile");return false;
    }
    const Tag keys=tags.value(34735);bool point=false;int linearUnits=9001;
    if(keys.type!=3||keys.count<4||integer(34735,0)!=1){error=QStringLiteral("Missing COG coordinate system");return false;}
    const quint64 keyCount=integer(34735,0,3);
    if(keyCount>(keys.count-4)/4){error=QStringLiteral("Invalid COG coordinate system");return false;}
    for(quint64 k=0;k<keyCount;++k){
        const int key=int(integer(34735,0,4+4*k));
        if(integer(34735,1,5+4*k)!=0||integer(34735,0,6+4*k)!=1)continue;
        const int value=int(integer(34735,0,7+4*k));
        if(key==1025){if(value!=1&&value!=2){error=QStringLiteral("Unsupported COG raster registration");return false;}point=value==2;}
        if(key==3072)c.epsg=value;if(key==3076)linearUnits=value;
    }
    if(c.epsg!=expectedEpsg||linearUnits!=9001){error=QStringLiteral("COG coordinate system does not match dataset");return false;}
    const Tag scale=tags.value(33550),tie=tags.value(33922);
    if(scale.type!=12||scale.count!=3||tie.type!=12||tie.count!=6){error=QStringLiteral("Missing COG grid transform");return false;}
    const double sx=rd.f64(scale.offset),sy=rd.f64(scale.offset+8);
    if(!(sx>0&&sy>0)){error=QStringLiteral("Invalid COG grid transform");return false;}
    c.transform={{rd.f64(tie.offset+24)-rd.f64(tie.offset)*sx,sx,0,
                  rd.f64(tie.offset+32)+rd.f64(tie.offset+8)*sy,0,-sy}};
    if(point){c.transform[0]-=.5*sx;c.transform[3]+=.5*sy;}
    if(tags.contains(42113)){
        const Tag t=tags.value(42113);if(t.type!=2||!t.count||t.count>128){error=QStringLiteral("Invalid COG NoData field");return false;}
        bool ok=false;c.noData=bytes.mid(t.offset,t.count).replace('\0',' ').trimmed().toFloat(&ok);
        if(!ok){error=QStringLiteral("Invalid COG NoData value");return false;}c.hasNoData=true;
    }
    const quint64 columns=(quint64(c.width)+c.tileWidth-1)/c.tileWidth;
    const quint64 rows=(quint64(c.height)+c.tileHeight-1)/c.tileHeight;
    const quint64 tiles=columns*rows;
    const Tag offsets=tags.value(324),byteCounts=tags.value(325);
    if(offsets.count!=tiles||byteCounts.count!=tiles||(offsets.type!=4&&offsets.type!=16)
            ||(byteCounts.type!=4&&byteCounts.type!=16)||tiles>4*1024*1024){error=QStringLiteral("Invalid COG block table");return false;}
    c.offsets.reserve(tiles);c.byteCounts.reserve(tiles);
    for(quint64 i=0;i<tiles;++i){c.offsets.push_back(integer(324,0,i));c.byteCounts.push_back(integer(325,0,i));}
    out=std::move(c);return true;
}

struct Asset {
    QString name,localFile,parts;
    QUrl url;
    CogIndex index;
    Raster raster;
    int firstColumn=0,firstRow=0;
    QSet<int> availableBlocks;
};

class CogFileSource final : public Source {
public:
    CogFileSource(QString path,Dataset data,Report &value)
        :root(std::move(path)),dataset(std::move(data)),report(value){}

    bool prepare(const QVector<Point> &points,std::atomic_bool &cancel,
                 const Progress &progress,QString &error) override {
        assets.clear();stacMosaic={};
        return dataset.fileGrid=="projected"?prepareProjected(points,cancel,progress,error)
                                             :prepareStac(points,cancel,progress,error);
    }
    Sample sample(Point point) override {
        XY xy;if(!project(point,dataset.epsg,xy)||!inside(dataset,xy))return {0,SampleStatus::Outside};
        if(dataset.fileGrid=="stac"&&!stacMosaic.values.isEmpty()){
            const Sample value=stacMosaic.sample(xy,dataset.zeroIsNoData);
            return value.status==SampleStatus::Outside?Sample{0,SampleStatus::Unavailable}:value;
        }
        for(const Asset &asset:assets){
            const double col=(xy.x-asset.index.transform[0])/asset.index.transform[1]-.5;
            const double row=(xy.y-asset.index.transform[3])/asset.index.transform[5]-.5;
            if(!asset.availableBlocks.isEmpty()&&col>=0&&row>=0&&col<=asset.index.width-1&&row<=asset.index.height-1){
                const int x=std::clamp(int(std::floor(col)),0,asset.index.width-1);
                const int y=std::clamp(int(std::floor(row)),0,asset.index.height-1);
                const int x1=std::min(x+1,asset.index.width-1),y1=std::min(y+1,asset.index.height-1);
                const int columns=(asset.index.width+asset.index.tileWidth-1)/asset.index.tileWidth;
                const int ids[]={y/asset.index.tileHeight*columns+x/asset.index.tileWidth,
                    y/asset.index.tileHeight*columns+x1/asset.index.tileWidth,
                    y1/asset.index.tileHeight*columns+x/asset.index.tileWidth,
                    y1/asset.index.tileHeight*columns+x1/asset.index.tileWidth};
                for(int id:ids)if(!asset.availableBlocks.contains(id))return {0,SampleStatus::Unavailable};
            }
            const Sample value=asset.raster.sample(xy,dataset.zeroIsNoData);
            if(value.status!=SampleStatus::Outside)return value;
        }
        return {0,SampleStatus::Unavailable};
    }
private:
    QString projectedName(qint64 northing,qint64 easting) const {
        return QFileInfo(projectedUrl(northing,easting).path()).fileName();
    }
    QUrl projectedUrl(qint64 northing,qint64 easting) const {
        QString url=dataset.downloadUrlTemplate;
        url.replace("{northing}",QString::number(northing)).replace("{easting}",QString::number(easting));
        return QUrl(url);
    }
    bool prepareProjected(const QVector<Point> &points,std::atomic_bool &cancel,
                          const Progress &progress,QString &error){
        struct Group{qint64 northing=0,easting=0;QVector<XY> points;};QMap<QString,Group> groups;
        for(Point p:points){if(cancel)return false;XY xy;if(!project(p,dataset.epsg,xy)||!inside(dataset,xy))continue;
            const qint64 e=qint64(std::floor(xy.x/dataset.fileTileSize))*qint64(dataset.fileTileSize);
            const qint64 n=qint64(std::floor(xy.y/dataset.fileTileSize))*qint64(dataset.fileTileSize);
            const QString key=QString::number(n)+'_'+QString::number(e);groups[key].northing=n;groups[key].easting=e;groups[key].points.push_back(xy);
        }
        int done=0;for(const Group &group:groups){if(cancel)return false;Asset asset;asset.name=projectedName(group.northing,group.easting);asset.url=projectedUrl(group.northing,group.easting);
            asset.localFile=QDir(root).filePath(dataset.directory+'/'+asset.name);asset.parts=asset.localFile+".parts/"+dataset.fileRevision;
            if(!loadProjectedAsset(asset,group.points,cancel,error)){
                report.issue(QStringLiteral("%1: %2").arg(asset.name,error));error.clear();
            }else assets.push_back(std::move(asset));
            if(progress)progress(++done,groups.size(),QStringLiteral("Preparing COG elevation files"));
        }return !cancel;
    }
    bool loadProjectedAsset(Asset &asset,const QVector<XY> &points,std::atomic_bool &cancel,QString &error){
        QByteArray indexBytes;
        if(QFileInfo(asset.localFile).isFile())indexBytes=read(asset.localFile,0,IndexBytes);
        else indexBytes=read(asset.parts+"/index.bin");
        if(indexBytes.isEmpty()){
            auto response=downloadRangeWave({{asset.url,0,IndexBytes-1}},cancel,{}, {qint64(IndexBytes),30000,45000});
            if(cancel)return false;if(response.isEmpty()||!response[0].error.isEmpty()){error=response.isEmpty()?QStringLiteral("Cannot read COG index"):response[0].error;return false;}
            indexBytes=response[0].bytes;if(!save(asset.parts+"/index.bin",indexBytes)){error=QStringLiteral("Cannot store COG index");return false;}
        }
        if(!parseCogIndex(indexBytes,dataset.epsg,asset.index,error))return false;
        double minCol=std::numeric_limits<double>::infinity(),maxCol=-minCol,minRow=minCol,maxRow=-minCol;
        for(XY xy:points){const double col=(xy.x-asset.index.transform[0])/asset.index.transform[1]-.5;
            const double row=(xy.y-asset.index.transform[3])/asset.index.transform[5]-.5;
            if(col<0||row<0||col>asset.index.width-1||row>asset.index.height-1){
                error=QStringLiteral("COG tile does not cover its configured grid cell");return false;}
            minCol=std::min(minCol,col);maxCol=std::max(maxCol,col);minRow=std::min(minRow,row);maxRow=std::max(maxRow,row);}
        int x0=std::clamp(int(std::floor(minCol)),0,asset.index.width-1),x1=std::clamp(int(std::floor(maxCol))+1,0,asset.index.width-1);
        int y0=std::clamp(int(std::floor(minRow)),0,asset.index.height-1),y1=std::clamp(int(std::floor(maxRow))+1,0,asset.index.height-1);
        const int bc0=x0/asset.index.tileWidth,bc1=x1/asset.index.tileWidth,br0=y0/asset.index.tileHeight,br1=y1/asset.index.tileHeight;
        asset.firstColumn=bc0*asset.index.tileWidth;asset.firstRow=br0*asset.index.tileHeight;
        const int endColumn=std::min(asset.index.width,(bc1+1)*asset.index.tileWidth),endRow=std::min(asset.index.height,(br1+1)*asset.index.tileHeight);
        asset.raster.width=endColumn-asset.firstColumn;asset.raster.height=endRow-asset.firstRow;asset.raster.epsg=asset.index.epsg;
        asset.raster.transform=asset.index.transform;asset.raster.transform[0]+=asset.firstColumn*asset.index.transform[1];asset.raster.transform[3]+=asset.firstRow*asset.index.transform[5];
        asset.raster.hasNoData=asset.index.hasNoData;asset.raster.noData=asset.index.noData;
        if(qint64(asset.raster.width)*asset.raster.height>32*1024*1024){error=QStringLiteral("COG window exceeds 32 million pixels");return false;}
        asset.raster.values.fill(std::numeric_limits<float>::quiet_NaN(),qsizetype(asset.raster.width)*asset.raster.height);
        const int columns=(asset.index.width+asset.index.tileWidth-1)/asset.index.tileWidth;
        struct Missing{int id,row,column;QString path;quint64 offset,length;};QVector<Missing> missing;
        for(int br=br0;br<=br1;++br)for(int bc=bc0;bc<=bc1;++bc){const int id=br*columns+bc;
            const QString path=QStringLiteral("%1/level-0/r%2-c%3.lzw").arg(asset.parts).arg(br).arg(bc);
            if(QFileInfo(path).isFile()){++report.cacheHits;}else if(QFileInfo(asset.localFile).isFile()){}
            else missing.push_back({id,br,bc,path,asset.index.offsets[id],asset.index.byteCounts[id]});}
        for(qsizetype first=0;first<missing.size();first+=dataset.concurrentRequests){if(cancel)return false;const int count=int(std::min(qsizetype(dataset.concurrentRequests),missing.size()-first));QVector<RangeRequest> requests;
            for(int i=0;i<count;++i){const auto &m=missing[first+i];if(!m.offset||!m.length||m.length>quint64(MaxCogRange)){error=QStringLiteral("Invalid COG block range");return false;}requests.push_back({asset.url,m.offset,m.offset+m.length-1});}
            const auto responses=downloadRangeWave(requests,cancel,{}, {MaxCogRange,30000,45000});if(cancel)return false;
            for(int i=0;i<count;++i){if(!responses[i].error.isEmpty()){report.issue(QStringLiteral("%1 block %2,%3: %4").arg(asset.name).arg(missing[first+i].column).arg(missing[first+i].row).arg(responses[i].error));continue;}
                if(!save(missing[first+i].path,responses[i].bytes)){report.issue(QStringLiteral("Cannot store COG block: %1").arg(missing[first+i].path));continue;}++report.downloads;}}
        for(int br=br0;br<=br1;++br)for(int bc=bc0;bc<=bc1;++bc){if(cancel)return false;const int id=br*columns+bc;QByteArray encoded;
            if(QFileInfo(asset.localFile).isFile())encoded=read(asset.localFile,asset.index.offsets[id],asset.index.byteCounts[id]);
            else encoded=read(QStringLiteral("%1/level-0/r%2-c%3.lzw").arg(asset.parts).arg(br).arg(bc));
            if(encoded.isEmpty())continue;QVector<float> block;QString blockError;
            if(!decodeTiffBlock(encoded,asset.index.compression,asset.index.predictor,asset.index.little,asset.index.bits,asset.index.sampleFormat,asset.index.tileWidth,asset.index.tileHeight,block,blockError)){
                report.issue(QStringLiteral("%1 block %2,%3: %4").arg(asset.name).arg(bc).arg(br).arg(blockError));continue;}
            asset.availableBlocks.insert(id);const int dx=bc*asset.index.tileWidth-asset.firstColumn,dy=br*asset.index.tileHeight-asset.firstRow;
            for(int y=0;y<asset.index.tileHeight&&dy+y<asset.raster.height;++y)for(int x=0;x<asset.index.tileWidth&&dx+x<asset.raster.width;++x)
                asset.raster.values[(dy+y)*asset.raster.width+dx+x]=block[y*asset.index.tileWidth+x];}
        return !asset.availableBlocks.isEmpty();
    }
    bool buildStacMosaic(QString &error){
        stacMosaic={};if(assets.isEmpty())return true;
        const Raster &reference=assets.first().raster;const double sx=reference.transform[1],sy=-reference.transform[5];
        if(!(sx>0&&sy>0)){error=QStringLiteral("Invalid STAC raster spacing");return false;}
        double minX=reference.transform[0],maxX=minX+reference.width*sx;
        double maxY=reference.transform[3],minY=maxY-reference.height*sy;
        for(const Asset &asset:assets){const Raster &r=asset.raster;
            if(r.epsg!=dataset.epsg||r.width<=0||r.height<=0||r.transform[2]!=0||r.transform[4]!=0
                    ||std::abs(r.transform[1]-sx)>1e-8||std::abs(r.transform[5]+sy)>1e-8){
                error=QStringLiteral("STAC rasters do not share one aligned grid");return false;}
            minX=std::min(minX,r.transform[0]);maxX=std::max(maxX,r.transform[0]+r.width*sx);
            maxY=std::max(maxY,r.transform[3]);minY=std::min(minY,r.transform[3]-r.height*sy);}
        const double wd=(maxX-minX)/sx,hd=(maxY-minY)/sy;const qint64 width=std::llround(wd),height=std::llround(hd);
        if(width<=0||height<=0||std::abs(wd-width)>1e-6||std::abs(hd-height)>1e-6
                ||width>32*1024*1024||height>32*1024*1024||width*height>32*1024*1024){
            error=QStringLiteral("STAC raster mosaic exceeds 32 million aligned pixels");return false;}
        Raster mosaic;mosaic.width=int(width);mosaic.height=int(height);mosaic.epsg=dataset.epsg;
        mosaic.transform={{minX,sx,0,maxY,0,-sy}};
        mosaic.values.fill(std::numeric_limits<float>::quiet_NaN(),width*height);
        for(const Asset &asset:assets){const Raster &r=asset.raster;
            const int dx=int(std::llround((r.transform[0]-minX)/sx));
            const int dy=int(std::llround((maxY-r.transform[3])/sy));
            if(std::abs(r.transform[0]-(minX+dx*sx))>1e-6||std::abs(r.transform[3]-(maxY-dy*sy))>1e-6
                    ||dx<0||dy<0||dx+r.width>mosaic.width||dy+r.height>mosaic.height){
                error=QStringLiteral("STAC raster origins are not grid-aligned");return false;}
            for(int y=0;y<r.height;++y)for(int x=0;x<r.width;++x){const float value=r.values[y*r.width+x];
                if(std::isfinite(value)&&!(r.hasNoData&&value==r.noData)&&!(dataset.zeroIsNoData&&value==0))
                    mosaic.values[(dy+y)*mosaic.width+dx+x]=value;}}
        stacMosaic=std::move(mosaic);assets.clear();return true;
    }
    bool prepareStac(const QVector<Point> &points,std::atomic_bool &cancel,const Progress &progress,QString &error){
        double west=180,east=-180,south=90,north=-90;for(Point p:points){XY xy;if(project(p,dataset.epsg,xy)&&inside(dataset,xy)){west=std::min(west,p.longitude);east=std::max(east,p.longitude);south=std::min(south,p.latitude);north=std::max(north,p.latitude);}}
        if(east<west)return true;
        constexpr double pi=3.14159265358979323846,metresPerDegree=111320.0;
        const double latitude=(south+north)/2,margin=dataset.resolution*2;
        const double latitudeMargin=margin/metresPerDegree;
        const double longitudeMargin=margin/(metresPerDegree*std::max(.1,std::cos(latitude*pi/180.0)));
        west-=longitudeMargin;east+=longitudeMargin;south-=latitudeMargin;north+=latitudeMargin;
        QUrl url=dataset.stacEndpoint;QString path=url.path();if(!path.endsWith('/'))path+='/';path+=QStringLiteral("collections/%1/items").arg(dataset.stacCollection);url.setPath(path);QUrlQuery query;query.addQueryItem("bbox",QStringLiteral("%1,%2,%3,%4").arg(west,0,'f',8).arg(south,0,'f',8).arg(east,0,'f',8).arg(north,0,'f',8));query.addQueryItem("limit","100");url.setQuery(query);
        QMap<QByteArray,QJsonObject> latest;for(int page=0;page<4&&!url.isEmpty();++page){auto response=downloadWave({url},cancel,{}, {2*1024*1024,30000,45000});if(cancel)return false;if(response.isEmpty()||!response[0].error.isEmpty()){error=response.isEmpty()?QStringLiteral("Cannot query STAC catalogue"):response[0].error;return false;}QJsonParseError pe;const auto doc=QJsonDocument::fromJson(response[0].bytes,&pe);if(pe.error!=QJsonParseError::NoError||!doc.object().value("features").isArray()){error=QStringLiteral("Invalid STAC response");return false;}
            for(const auto value:doc.object().value("features").toArray()){const auto feature=value.toObject();const QByteArray key=QJsonDocument(feature.value("bbox").toArray()).toJson(QJsonDocument::Compact);if(key.isEmpty())continue;const QString time=feature.value("properties").toObject().value("datetime").toString();if(!latest.contains(key)||time>latest[key].value("properties").toObject().value("datetime").toString())latest[key]=feature;}
            QUrl next;for(const auto link:doc.object().value("links").toArray()){const auto object=link.toObject();if(object.value("rel").toString()=="next"){next=QUrl(object.value("href").toString());break;}}
            if(page==3&&!next.isEmpty()){error=QStringLiteral("STAC query exceeds 400 items; generate a smaller area");return false;}url=next;}
        struct Remote{QString name,path;QUrl url;};QVector<Remote> remote;
        for(const auto &feature:latest){QJsonObject selected;const auto object=feature.value("assets").toObject();for(auto it=object.begin();it!=object.end();++it){const auto asset=it.value().toObject();if(std::abs(asset.value("eo:gsd").toDouble(-1)-dataset.resolution)<1e-9&&asset.value("proj:epsg").toInt()==dataset.epsg&&asset.value("type").toString().contains("profile=cloud-optimized")){selected=asset;break;}}
            if(selected.isEmpty())continue;const QUrl assetUrl(selected.value("href").toString());const QString fileName=QFileInfo(assetUrl.path()).fileName();
            if(assetUrl.scheme()!="https"||assetUrl.host().isEmpty()||fileName.isEmpty())continue;
            Remote r{fileName,QDir(root).filePath(dataset.directory+'/'+fileName),assetUrl};Raster raster;QString decodeError;
            const QByteArray bytes=QFileInfo(r.path).size()<=qint64(MaxWholeTiff)?read(r.path,0,MaxWholeTiff+1):QByteArray();
            if(!bytes.isEmpty()&&readGeoTiff(bytes,raster,decodeError)){Asset a;a.name=r.name;a.localFile=r.path;a.url=r.url;a.raster=std::move(raster);assets.push_back(std::move(a));++report.cacheHits;}else remote.push_back(r);}
        int done=assets.size();const int total=done+remote.size();for(qsizetype first=0;first<remote.size();first+=dataset.concurrentRequests){if(cancel)return false;const int count=int(std::min(qsizetype(dataset.concurrentRequests),remote.size()-first));QVector<QUrl> urls;for(int i=0;i<count;++i)urls.push_back(remote[first+i].url);const auto responses=downloadWave(urls,cancel,[&](int finished){if(progress)progress(done+finished,total,QStringLiteral("Downloading COG elevation files"));});if(cancel)return false;
            for(int i=0;i<count;++i){QString issue=responses[i].error;Raster raster;if(issue.isEmpty()&&!readGeoTiff(responses[i].bytes,raster,issue)){}if(issue.isEmpty()&&!save(remote[first+i].path,responses[i].bytes))issue=QStringLiteral("Cannot store COG file");if(issue.isEmpty()){Asset a;a.name=remote[first+i].name;a.localFile=remote[first+i].path;a.url=remote[first+i].url;a.raster=std::move(raster);assets.push_back(std::move(a));++report.downloads;}else report.issue(QStringLiteral("%1: %2").arg(remote[first+i].name,issue));++done;}}
        return !cancel&&buildStacMosaic(error);
    }
    QString root;Dataset dataset;Report &report;QVector<Asset> assets;Raster stacMosaic;
};
}

std::unique_ptr<Source> createCogElevationSource(const QString &root,
        const Dataset &dataset,Report &report){
    return std::make_unique<CogFileSource>(root,dataset,report);
}
}
