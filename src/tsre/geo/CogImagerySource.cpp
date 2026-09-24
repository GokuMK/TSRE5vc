/* TSRE5 - Copyright (C) 2016 Piotr Gadecki. GPL-3.0-or-later. */
#include <tsre/geo/CogImagerySource.h>
#include <tsre/geo/ElevationDownload.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QSet>
#include <QUrlQuery>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace Imagery {
namespace {
constexpr quint64 IndexBytes=64*1024;
constexpr qint64 MaxRangeBytes=8*1024*1024;
constexpr quint64 MaxTableBytes=16*1024*1024;
constexpr qint64 MaxDecodedPixels=48ll*1024*1024;

struct Tag { quint16 type=0;quint64 count=0,offset=0; };
struct Directory { QMap<int,Tag> tags;quint64 next=0; };
struct Index {
    bool little=true,big=false;
    int width=0,height=0,tileWidth=0,tileHeight=0,level=0,epsg=0;
    double spacing=0;
    std::array<double,6> transform{{0,1,0,0,0,-1}};
    Tag offsetsTag,countsTag,jpegTablesTag;
    QVector<quint64> offsets,counts;
    QByteArray jpegTables;
};
struct Reader {
    explicit Reader(const QByteArray &v):bytes(v){}
    bool little=true,big=false;
    bool range(quint64 p,quint64 n)const{return p<=quint64(bytes.size())&&n<=quint64(bytes.size())-p;}
    quint16 u16(quint64 p)const{return little?qFromLittleEndian<quint16>(bytes.constData()+p):qFromBigEndian<quint16>(bytes.constData()+p);}
    quint32 u32(quint64 p)const{return little?qFromLittleEndian<quint32>(bytes.constData()+p):qFromBigEndian<quint32>(bytes.constData()+p);}
    quint64 u64(quint64 p)const{return little?qFromLittleEndian<quint64>(bytes.constData()+p):qFromBigEndian<quint64>(bytes.constData()+p);}
    double f64(quint64 p)const{quint64 v=u64(p);double d;std::memcpy(&d,&v,8);return d;}
    const QByteArray &bytes;
};
struct TileImage { QImage image;double left=0,top=0;int validWidth=0,validHeight=0; };
struct Asset {
    QString name,parts;QUrl url;Index index;QVector<TileImage> tiles;
};

int typeSize(quint16 type){
    static const int sizes[]={0,1,1,2,4,8,1,1,2,4,8,4,8,0,0,0,8,8,8};
    return type<int(sizeof(sizes)/sizeof(sizes[0]))?sizes[type]:0;
}
quint64 tagSize(const Tag &tag){return quint64(typeSize(tag.type))*tag.count;}
quint64 integer(const Reader &rd,const QMap<int,Tag> &tags,int id,quint64 fallback,quint64 index=0){
    const Tag tag=tags.value(id);if(index>=tag.count)return fallback;
    const quint64 p=tag.offset+quint64(typeSize(tag.type))*index;
    if(tag.type==3&&rd.range(p,2))return rd.u16(p);
    if(tag.type==4&&rd.range(p,4))return rd.u32(p);
    if(tag.type==16&&rd.range(p,8))return rd.u64(p);
    return fallback;
}
bool parseDirectory(const Reader &rd,quint64 position,Directory &out,QString &error){
    const quint64 prefix=rd.big?8:2,entrySize=rd.big?20:12,inlineSize=rd.big?8:4;
    if(!rd.range(position,prefix)){error=QStringLiteral("Invalid imagery COG directory");return false;}
    const quint64 count=rd.big?rd.u64(position):rd.u16(position);
    const quint64 next=position+prefix+count*entrySize;
    if(count>256||!rd.range(next,rd.big?8:4)){error=QStringLiteral("Invalid imagery COG directory size");return false;}
    for(quint64 i=0;i<count;++i){
        const quint64 p=position+prefix+i*entrySize;Tag tag;const int id=rd.u16(p);
        tag.type=rd.u16(p+2);tag.count=rd.big?rd.u64(p+4):rd.u32(p+4);
        const int size=typeSize(tag.type);
        if(!size||tag.count>16*1024*1024||out.tags.contains(id)){
            error=QStringLiteral("Unsupported imagery COG field");return false;}
        const quint64 value=p+(rd.big?12:8),bytes=quint64(size)*tag.count;
        tag.offset=bytes<=inlineSize?value:(rd.big?rd.u64(value):rd.u32(value));
        out.tags.insert(id,tag);
    }
    out.next=rd.big?rd.u64(next):rd.u32(next);return true;
}
bool decodeTable(const QByteArray &bytes,bool little,const Tag &tag,QVector<quint64> &out){
    const int size=typeSize(tag.type);
    if((tag.type!=4&&tag.type!=16)||!tag.count||tag.count>4*1024*1024
            ||quint64(bytes.size())!=quint64(size)*tag.count)return false;
    out.resize(tag.count);
    for(quint64 i=0;i<tag.count;++i){const char *p=bytes.constData()+i*size;
        out[i]=tag.type==4?(little?qFromLittleEndian<quint32>(p):qFromBigEndian<quint32>(p))
                          :(little?qFromLittleEndian<quint64>(p):qFromBigEndian<quint64>(p));}
    return true;
}
bool parseIndex(const QByteArray &bytes,int expectedEpsg,double desiredSpacing,
                Index &out,QString &error){
    if(bytes.size()<16){error=QStringLiteral("Truncated imagery COG header");return false;}
    Reader rd(bytes);
    if(bytes.startsWith("II"))rd.little=true;else if(bytes.startsWith("MM"))rd.little=false;
    else{error=QStringLiteral("Imagery asset is not a TIFF raster");return false;}
    const quint16 magic=rd.u16(2);rd.big=magic==43;
    if(magic!=42&&magic!=43){error=QStringLiteral("Unsupported imagery TIFF header");return false;}
    if(rd.big&&(rd.u16(4)!=8||rd.u16(6)!=0)){error=QStringLiteral("Invalid imagery BigTIFF header");return false;}
    QVector<Directory> directories;QSet<quint64> seen;
    quint64 position=rd.big?rd.u64(8):rd.u32(4);
    for(int level=0;level<16&&position;++level){
        if(seen.contains(position)){error=QStringLiteral("Recursive imagery COG directory");return false;}
        seen.insert(position);Directory d;if(!parseDirectory(rd,position,d,error))return false;
        directories.push_back(std::move(d));position=directories.last().next;
    }
    if(directories.isEmpty()||position){error=QStringLiteral("Invalid imagery COG overview directories");return false;}
    const auto &base=directories.first().tags;
    const int baseWidth=int(integer(rd,base,256,0)),baseHeight=int(integer(rd,base,257,0));
    const Tag scale=base.value(33550),tie=base.value(33922);
    if(baseWidth<=0||baseHeight<=0||scale.type!=12||scale.count!=3||tie.type!=12||tie.count!=6
            ||!rd.range(scale.offset,24)||!rd.range(tie.offset,48)){
        error=QStringLiteral("Missing imagery COG grid transform");return false;}
    const double baseSx=rd.f64(scale.offset),baseSy=rd.f64(scale.offset+8);
    if(!(baseSx>0&&baseSy>0)){error=QStringLiteral("Invalid imagery COG grid transform");return false;}
    int selected=0,selectedFactor=1;
    for(int level=0;level<directories.size();++level){
        const auto &tags=directories[level].tags;
        const int width=int(integer(rd,tags,256,0)),height=int(integer(rd,tags,257,0));
        if(width<=0||height<=0)continue;
        const int factor=std::max(1,int(std::llround(double(baseWidth)/width)));
        if(std::abs(baseWidth-width*factor)>factor||std::abs(baseHeight-height*factor)>factor)continue;
        const double spacing=baseSx*factor;
        if(spacing<=desiredSpacing*1.001&&spacing>=baseSx*selectedFactor){selected=level;selectedFactor=factor;}
    }
    const auto &tags=directories[selected].tags;Index c;c.little=rd.little;c.big=rd.big;
    c.level=selected;c.width=int(integer(rd,tags,256,0));c.height=int(integer(rd,tags,257,0));
    c.tileWidth=int(integer(rd,tags,322,0));c.tileHeight=int(integer(rd,tags,323,0));
    const int compression=int(integer(rd,tags,259,1));
    const int photometric=int(integer(rd,tags,262,0));
    if(c.width<=0||c.height<=0||c.width>2*1024*1024||c.height>2*1024*1024
            ||integer(rd,tags,277,1)!=3||integer(rd,tags,284,1)!=1
            ||integer(rd,tags,258,0,0)!=8||integer(rd,tags,258,0,1)!=8
            ||integer(rd,tags,258,0,2)!=8||compression!=7
            ||(photometric!=2&&photometric!=6)
            ||c.tileWidth<=0||c.tileHeight<=0||qint64(c.tileWidth)*c.tileHeight>4*1024*1024){
        error=QStringLiteral("Unsupported imagery COG raster profile");return false;
    }
    const Tag keys=base.value(34735);int modelType=0,linearUnits=9001;
    if(keys.type!=3||keys.count<4||integer(rd,base,34735,0)!=1){error=QStringLiteral("Missing imagery COG coordinate system");return false;}
    const quint64 keyCount=integer(rd,base,34735,0,3);
    if(keyCount>(keys.count-4)/4){error=QStringLiteral("Invalid imagery COG coordinate system");return false;}
    bool point=false;
    for(quint64 k=0;k<keyCount;++k){
        const int key=int(integer(rd,base,34735,0,4+4*k));
        if(integer(rd,base,34735,1,5+4*k)!=0||integer(rd,base,34735,0,6+4*k)!=1)continue;
        const int value=int(integer(rd,base,34735,0,7+4*k));
        if(key==1024)modelType=value;if(key==1025)point=value==2;
        if(key==3072)c.epsg=value;if(key==3076)linearUnits=value;
    }
    if(modelType!=1||c.epsg!=expectedEpsg||linearUnits!=9001){
        error=QStringLiteral("Imagery COG coordinate system does not match dataset");return false;}
    c.spacing=baseSx*selectedFactor;
    c.transform={{rd.f64(tie.offset+24)-rd.f64(tie.offset)*baseSx,c.spacing,0,
                  rd.f64(tie.offset+32)+rd.f64(tie.offset+8)*baseSy,0,-baseSy*selectedFactor}};
    if(point){c.transform[0]-=.5*c.spacing;c.transform[3]+=.5*baseSy*selectedFactor;}
    const quint64 columns=(quint64(c.width)+c.tileWidth-1)/c.tileWidth;
    const quint64 rows=(quint64(c.height)+c.tileHeight-1)/c.tileHeight;
    const quint64 tiles=columns*rows;
    c.offsetsTag=tags.value(324);c.countsTag=tags.value(325);c.jpegTablesTag=tags.value(347);
    if(c.offsetsTag.count!=tiles||c.countsTag.count!=tiles
            ||(c.offsetsTag.type!=4&&c.offsetsTag.type!=16)
            ||(c.countsTag.type!=4&&c.countsTag.type!=16)
            ||tiles>4*1024*1024||tagSize(c.offsetsTag)>MaxTableBytes
            ||tagSize(c.countsTag)>MaxTableBytes
            ||c.jpegTablesTag.type!=7||c.jpegTablesTag.count<4
            ||c.jpegTablesTag.count>64*1024){error=QStringLiteral("Invalid imagery COG block tables");return false;}
    out=std::move(c);return true;
}
QByteArray readFile(const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
bool saveFile(const QString &path,const QByteArray &bytes){
    if(!QDir().mkpath(QFileInfo(path).absolutePath()))return false;QSaveFile f(path);
    return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size()&&f.commit();
}
QString imageryError(QString error){return error.replace(QStringLiteral("Elevation "),QStringLiteral("Imagery "))
    .replace(QStringLiteral("elevation "),QStringLiteral("imagery "));}
QByteArray fieldBytes(const QByteArray &indexBytes,const QString &path,const Tag &tag){
    QByteArray bytes=readFile(path);
    const quint64 size=tagSize(tag);
    if(quint64(bytes.size())==size)return bytes;
    if(!bytes.isEmpty())QFile::remove(path);
    if(tag.offset<=quint64(indexBytes.size())&&size<=quint64(indexBytes.size())-tag.offset)
        return indexBytes.mid(tag.offset,size);
    return {};
}
bool ensureFields(Asset &asset,const QByteArray &indexBytes,std::atomic_bool &cancel,QString &error){
    struct Field{Tag *tag;QString name;QByteArray bytes;};
    QVector<Field> fields{{&asset.index.offsetsTag,QStringLiteral("tile-offsets.bin"),{}},
                          {&asset.index.countsTag,QStringLiteral("tile-byte-counts.bin"),{}},
                          {&asset.index.jpegTablesTag,QStringLiteral("jpeg-tables.bin"),{}}};
    QVector<int> missing;QVector<Elevation::RangeRequest> ranges;
    for(int i=0;i<fields.size();++i){fields[i].bytes=fieldBytes(indexBytes,asset.parts+'/'+fields[i].name,*fields[i].tag);
        if(fields[i].bytes.isEmpty()){missing.push_back(i);ranges.push_back({asset.url,fields[i].tag->offset,
            fields[i].tag->offset+tagSize(*fields[i].tag)-1});}}
    if(!ranges.isEmpty()){
        const auto responses=Elevation::downloadRangeWave(ranges,cancel,{},
            {qint64(MaxTableBytes+1),30000,45000});
        if(cancel)return false;
        for(int i=0;i<missing.size();++i){
            if(i>=responses.size()||!responses[i].error.isEmpty()){
                error=i>=responses.size()?QStringLiteral("Cannot read imagery COG tables")
                    :imageryError(responses[i].error);return false;}
            fields[missing[i]].bytes=responses[i].bytes;
            if(!saveFile(asset.parts+'/'+fields[missing[i]].name,responses[i].bytes)){
                error=QStringLiteral("Cannot cache imagery COG tables");return false;}
        }
    }
    if(!decodeTable(fields[0].bytes,asset.index.little,asset.index.offsetsTag,asset.index.offsets)
            ||!decodeTable(fields[1].bytes,asset.index.little,asset.index.countsTag,asset.index.counts)){
        error=QStringLiteral("Invalid imagery COG block table values");return false;}
    asset.index.jpegTables=fields[2].bytes;return true;
}
QByteArray completeJpeg(const QByteArray &tile,const QByteArray &tables){
    const QByteArray soi("\xff\xd8",2),eoi("\xff\xd9",2);
    if(tile.startsWith(soi)){
        if(tile.contains(QByteArray("\xff\xdb",2)))return tile;
        QByteArray result=soi;
        result+=tables.startsWith(soi)?tables.mid(2,tables.size()-(tables.endsWith(eoi)?4:2)):tables;
        result+=tile.mid(2);if(!result.endsWith(eoi))result+=eoi;return result;
    }
    QByteArray result=soi;
    result+=tables.startsWith(soi)?tables.mid(2,tables.size()-(tables.endsWith(eoi)?4:2)):tables;
    result+=tile;if(!result.endsWith(eoi))result+=eoi;return result;
}
bool decodeTile(const QByteArray &bytes,const Index &index,QImage &image,QString &error){
    image=QImage::fromData(completeJpeg(bytes,index.jpegTables),"JPEG");
    if(image.isNull()||image.width()!=index.tileWidth||image.height()!=index.tileHeight){
        error=QStringLiteral("Cannot decode imagery COG JPEG tile");image={};return false;}
    image=image.convertToFormat(QImage::Format_RGB888);return true;
}
QString revisionTime(const QJsonObject &feature){const auto p=feature.value("properties").toObject();
    QString result=p.value("updated").toString();if(result.isEmpty())result=p.value("created").toString();
    if(result.isEmpty())result=p.value("datetime").toString();return result;}

bool loadAsset(Asset &asset,int expectedEpsg,double minX,double minY,double maxX,double maxY,
               double desiredSpacing,std::atomic_bool &cancel,const Progress &progress,
               Report &report,QString &error){
    QByteArray indexBytes=readFile(asset.parts+"/index.bin");
    if(indexBytes.isEmpty()){
        const auto responses=Elevation::downloadRangeWave({{asset.url,0,IndexBytes-1}},cancel,{},
                                                           {qint64(IndexBytes),30000,45000});
        if(cancel)return false;
        if(responses.isEmpty()||!responses[0].error.isEmpty()){
            error=responses.isEmpty()?QStringLiteral("Cannot read imagery COG index")
                                     :imageryError(responses[0].error);return false;}
        indexBytes=responses[0].bytes;
        if(!saveFile(asset.parts+"/index.bin",indexBytes)){error=QStringLiteral("Cannot cache imagery COG index");return false;}
    }
    if(!parseIndex(indexBytes,expectedEpsg,desiredSpacing,asset.index,error)
            ||!ensureFields(asset,indexBytes,cancel,error))return false;
    const double sx=asset.index.transform[1],sy=-asset.index.transform[5];
    const double assetMinX=asset.index.transform[0],assetMaxX=assetMinX+asset.index.width*sx;
    const double assetMaxY=asset.index.transform[3],assetMinY=assetMaxY-asset.index.height*sy;
    const double x0=std::max(minX,assetMinX),x1=std::min(maxX,assetMaxX);
    const double y0=std::max(minY,assetMinY),y1=std::min(maxY,assetMaxY);
    if(x1<=x0||y1<=y0)return true;
    const int bc0=std::clamp(int(std::floor((x0-assetMinX)/sx))/asset.index.tileWidth,0,
                             (asset.index.width-1)/asset.index.tileWidth);
    const int bc1=std::clamp(int(std::floor((x1-assetMinX)/sx))/asset.index.tileWidth,0,
                             (asset.index.width-1)/asset.index.tileWidth);
    const int br0=std::clamp(int(std::floor((assetMaxY-y1)/sy))/asset.index.tileHeight,0,
                             (asset.index.height-1)/asset.index.tileHeight);
    const int br1=std::clamp(int(std::floor((assetMaxY-y0)/sy))/asset.index.tileHeight,0,
                             (asset.index.height-1)/asset.index.tileHeight);
    const int columns=(asset.index.width+asset.index.tileWidth-1)/asset.index.tileWidth;
    struct Missing{int id=0,row=0,column=0;QString path;quint64 offset=0,length=0;};
    QVector<Missing> missing;
    for(int br=br0;br<=br1;++br)for(int bc=bc0;bc<=bc1;++bc){const int id=br*columns+bc;
        const QString path=QStringLiteral("%1/level-%2/r%3-c%4.jpg").arg(asset.parts).arg(asset.index.level).arg(br).arg(bc);
        if(QFileInfo(path).isFile())++report.cacheHits;
        else missing.push_back({id,br,bc,path,asset.index.offsets[id],asset.index.counts[id]});}
    std::sort(missing.begin(),missing.end(),[](const Missing &a,const Missing &b){return a.offset<b.offset;});
    struct Group{quint64 first=0,last=0;QVector<int> members;};QVector<Group> groups;
    for(int i=0;i<missing.size();++i){const auto &m=missing[i];
        if(!m.offset||!m.length||m.length>quint64(MaxRangeBytes)){error=QStringLiteral("Invalid imagery COG tile range");return false;}
        if(m.offset>std::numeric_limits<quint64>::max()-m.length){error=QStringLiteral("Invalid imagery COG tile range");return false;}
        const quint64 last=m.offset+m.length-1;
        if(groups.isEmpty()||m.offset>groups.last().last+256*1024
                ||last-groups.last().first>=quint64(MaxRangeBytes))groups.push_back({m.offset,last,{i}});
        else{groups.last().last=std::max(groups.last().last,last);groups.last().members.push_back(i);}}
    int completed=0;
    for(int first=0;first<groups.size()&&!cancel;first+=4){const int count=std::min(4,int(groups.size())-first);
        QVector<Elevation::RangeRequest> ranges;for(int i=0;i<count;++i)ranges.push_back({asset.url,groups[first+i].first,groups[first+i].last});
        const auto responses=Elevation::downloadRangeWave(ranges,cancel,{}, {MaxRangeBytes,30000,45000});
        if(cancel)return false;
        for(int i=0;i<count;++i){const auto &group=groups[first+i];
            if(i>=responses.size()||!responses[i].error.isEmpty()){
                error=i>=responses.size()?QStringLiteral("Cannot read imagery COG tiles"):imageryError(responses[i].error);return false;}
            report.downloadedBytes+=responses[i].bytes.size();++report.downloads;
            for(const int member:group.members){const auto &m=missing[member];const quint64 relative=m.offset-group.first;
                const QByteArray tile=responses[i].bytes.mid(qsizetype(relative),qsizetype(m.length));
                if(quint64(tile.size())!=m.length||!saveFile(m.path,tile)){
                    error=QStringLiteral("Cannot cache imagery COG tile");return false;}}
            if(progress)progress(++completed,groups.size(),QStringLiteral("Downloading COG imagery"));
        }
    }
    for(int br=br0;br<=br1;++br)for(int bc=bc0;bc<=bc1;++bc){
        const QString path=QStringLiteral("%1/level-%2/r%3-c%4.jpg").arg(asset.parts).arg(asset.index.level).arg(br).arg(bc);
        QImage tile;QString issue;if(!decodeTile(readFile(path),asset.index,tile,issue)){error=issue;return false;}
        const int validWidth=std::min(asset.index.tileWidth,asset.index.width-bc*asset.index.tileWidth);
        const int validHeight=std::min(asset.index.tileHeight,asset.index.height-br*asset.index.tileHeight);
        asset.tiles.push_back({std::move(tile),assetMinX+bc*asset.index.tileWidth*sx,
                               assetMaxY-br*asset.index.tileHeight*sy,validWidth,validHeight});
    }
    return true;
}
}

bool loadStacCogImage(const QString &root,const Dataset &dataset,
        const QVector<GeographicPoint> &controlPoints,double minX,double minY,
        double maxX,double maxY,int width,int height,std::atomic_bool &cancel,
        const Progress &progress,Report &report,QImage &image,QString &error){
    double west=180,east=-180,south=90,north=-90;
    for(const auto point:controlPoints){west=std::min(west,point.longitude);east=std::max(east,point.longitude);
        south=std::min(south,point.latitude);north=std::max(north,point.latitude);}
    if(east<west){error=QStringLiteral("Invalid STAC imagery bounds");return false;}
    const double centreLatitude=.5*(south+north);
    const double latitudeMargin=10.0/111320.0;
    const double longitudeMargin=latitudeMargin/std::max(.1,std::cos(centreLatitude*3.14159265358979323846/180.0));
    west=std::max(-180.0,west-longitudeMargin);east=std::min(180.0,east+longitudeMargin);
    south=std::max(-90.0,south-latitudeMargin);north=std::min(90.0,north+latitudeMargin);
    QUrl url=dataset.endpoint;QString path=url.path();if(!path.endsWith('/'))path+='/';
    path+=QStringLiteral("collections/%1/items").arg(dataset.layer);url.setPath(path);
    QUrlQuery query;query.addQueryItem("bbox",QStringLiteral("%1,%2,%3,%4")
        .arg(west,0,'f',8).arg(south,0,'f',8).arg(east,0,'f',8).arg(north,0,'f',8));
    query.addQueryItem("limit","100");url.setQuery(query);
    QMap<QByteArray,QJsonObject> latest;
    for(int page=0;page<4&&!url.isEmpty();++page){
        const auto responses=Elevation::downloadWave({url},cancel,{}, {2*1024*1024,30000,45000});
        if(cancel)return false;
        if(responses.isEmpty()||!responses[0].error.isEmpty()){
            error=responses.isEmpty()?QStringLiteral("Cannot query imagery STAC catalogue")
                                     :imageryError(responses[0].error);return false;}
        QJsonParseError parseError;const auto document=QJsonDocument::fromJson(responses[0].bytes,&parseError);
        if(parseError.error!=QJsonParseError::NoError||!document.object().value("features").isArray()){
            error=QStringLiteral("Invalid imagery STAC response");return false;}
        for(const auto value:document.object().value("features").toArray()){
            const auto feature=value.toObject();const QByteArray key=QJsonDocument(feature.value("bbox").toArray()).toJson(QJsonDocument::Compact);
            if(!key.isEmpty()&&(!latest.contains(key)||revisionTime(feature)>revisionTime(latest[key])))latest[key]=feature;}
        QUrl next;for(const auto link:document.object().value("links").toArray()){
            const auto object=link.toObject();if(object.value("rel").toString()=="next"){next=QUrl(object.value("href").toString());break;}}
        if(page==3&&!next.isEmpty()){error=QStringLiteral("Imagery STAC query exceeds 400 items");return false;}url=next;
    }
    const double desiredSpacing=std::max((maxX-minX)/width,(maxY-minY)/height);
    QVector<Asset> assets;
    for(const auto &feature:latest){const auto object=feature.value("assets").toObject();QJsonObject selected;double selectedGsd=-1;
        for(auto it=object.begin();it!=object.end();++it){const auto candidate=it.value().toObject();
            const double gsd=candidate.value("gsd").toDouble(candidate.value("eo:gsd").toDouble(-1));
            const int epsg=candidate.value("proj:epsg").toInt(feature.value("properties").toObject().value("proj:epsg").toInt());
            if(gsd>0&&gsd<=desiredSpacing*1.001&&epsg==dataset.crs
                    &&candidate.value("type").toString().contains("profile=cloud-optimized")&&gsd>selectedGsd){selected=candidate;selectedGsd=gsd;}}
        if(selected.isEmpty())continue;const QUrl assetUrl(selected.value("href").toString());
        const QString fileName=QFileInfo(assetUrl.path()).fileName();if(assetUrl.scheme()!="https"||fileName.isEmpty())continue;
        const QByteArray material=assetUrl.toEncoded()+revisionTime(feature).toUtf8();
        const QString revision=QString::fromLatin1(QCryptographicHash::hash(material,QCryptographicHash::Sha256).toHex().left(16));
        Asset asset;asset.name=fileName;asset.url=assetUrl;
        asset.parts=QDir(root).filePath(QStringLiteral("cache/imagery/")
            +dataset.directory+'/'+fileName+".parts/"+revision);
        QString issue;if(!loadAsset(asset,dataset.crs,minX,minY,maxX,maxY,desiredSpacing,cancel,progress,report,issue)){
            if(cancel)return false;report.issues<<QStringLiteral("%1: %2").arg(fileName,issue);continue;}
        if(!asset.tiles.isEmpty())assets.push_back(std::move(asset));
    }
    if(assets.isEmpty()){error=QStringLiteral("No imagery COG asset covers the requested terrain");return false;}
    const double sx=assets.first().index.spacing,sy=-assets.first().index.transform[5];
    double mosaicMinX=std::numeric_limits<double>::infinity(),mosaicMaxX=-mosaicMinX;
    double mosaicMinY=mosaicMinX,mosaicMaxY=-mosaicMinX;
    int tileCount=0;
    for(const auto &asset:assets){if(std::abs(asset.index.spacing-sx)>1e-9||std::abs(-asset.index.transform[5]-sy)>1e-9){
            error=QStringLiteral("Imagery COG assets use inconsistent overview spacing");return false;}
        for(const auto &tile:asset.tiles){mosaicMinX=std::min(mosaicMinX,tile.left);mosaicMaxX=std::max(mosaicMaxX,tile.left+tile.validWidth*sx);
            mosaicMaxY=std::max(mosaicMaxY,tile.top);mosaicMinY=std::min(mosaicMinY,tile.top-tile.validHeight*sy);++tileCount;}}
    const qint64 mosaicWidth=std::llround((mosaicMaxX-mosaicMinX)/sx);
    const qint64 mosaicHeight=std::llround((mosaicMaxY-mosaicMinY)/sy);
    if(mosaicWidth<=0||mosaicHeight<=0||mosaicWidth*mosaicHeight>MaxDecodedPixels){
        error=QStringLiteral("Decoded imagery COG mosaic is too large");return false;}
    QImage mosaic(int(mosaicWidth),int(mosaicHeight),QImage::Format_RGB888);mosaic.fill(Qt::black);
    QPainter painter(&mosaic);
    for(const auto &asset:assets)for(const auto &tile:asset.tiles){
        const int x=int(std::llround((tile.left-mosaicMinX)/sx));
        const int y=int(std::llround((mosaicMaxY-tile.top)/sy));
        painter.drawImage(QPoint(x,y),tile.image,QRect(0,0,tile.validWidth,tile.validHeight));}
    painter.end();
    image=QImage(width,height,QImage::Format_RGB888);if(image.isNull()){error=QStringLiteral("Cannot allocate COG imagery output");return false;}
    image.fill(Qt::black);QPainter output(&image);output.setRenderHint(QPainter::SmoothPixmapTransform,true);
    const QRectF source((minX-mosaicMinX)/sx,(mosaicMaxY-maxY)/sy,(maxX-minX)/sx,(maxY-minY)/sy);
    output.drawImage(QRectF(0,0,width,height),mosaic,source);output.end();
    report.tiles+=tileCount;report.sourceMetresPerPixel=std::max(sx,sy);return true;
}
}
