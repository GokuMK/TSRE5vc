#include <tsre/geo/WfsElevationSource.h>
#include <tsre/geo/ElevationDownload.h>
#include <tsre/geo/ElevationSource.h>
#include <tsre/geo/ElevationTiffCodec.h>

#define MINIZ_HEADER_FILE_ONLY
#include <mzip/miniz/miniz.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Elevation {
namespace {
constexpr qint64 MaxCatalogueBytes=8*1024*1024;
constexpr qint64 MaxSourceBytes=96*1024*1024;
constexpr quint64 MaxExpandedBytes=256*1024*1024;
constexpr qint64 MaxCachedTiff=128*1024*1024;

struct Bounds {
    double minX=0,minY=0,maxX=0,maxY=0;
    bool valid() const { return maxX>minX&&maxY>minY; }
    bool contains(const Bounds &other) const {
        return valid()&&other.valid()&&minX<=other.minX&&minY<=other.minY
            &&maxX>=other.maxX&&maxY>=other.maxY;
    }
    bool overlaps(const Bounds &other) const {
        return valid()&&other.valid()&&maxX>=other.minX&&minX<=other.maxX
            &&maxY>=other.minY&&minY<=other.maxY;
    }
};
struct Asset {
    QString key,sheet,format,url,file;
    int year=0;
    Bounds bounds;
    qint64 sourceBytes=0;
    QString sha256;
};
struct Query { Bounds bounds;QStringList assets; };
struct Config {
    QRegularExpression featureTypes;
    QString sheetField,yearField,formatField,resolutionField,urlField;
    QStringList acceptedFormats;
    QString sourceAxisOrder;
    bool wfsAxisSwap=false;
};

bool saveFile(const QString &path,const QByteArray &bytes){
    if(!QDir().mkpath(QFileInfo(path).absolutePath()))return false;
    QSaveFile file(path);return file.open(QIODevice::WriteOnly)
        &&file.write(bytes)==bytes.size()&&file.commit();
}
QByteArray readFile(const QString &path,qint64 limit){
    QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()<=0||file.size()>limit)return {};
    return file.readAll();
}
QJsonArray boundsJson(const Bounds &b){return {b.minX,b.minY,b.maxX,b.maxY};}
Bounds jsonBounds(const QJsonValue &value){const auto a=value.toArray();return a.size()==4
    ?Bounds{a[0].toDouble(),a[1].toDouble(),a[2].toDouble(),a[3].toDouble()}:Bounds{};}
QString safeName(QString value){value.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")),"_");return value.left(96);}
QString assetKey(const QString &sheet,int year,const QString &url){
    return QStringLiteral("%1_%2_%3").arg(year).arg(safeName(sheet),
        QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(),QCryptographicHash::Sha256).toHex().left(12)));
}
QUrl operationUrl(const QUrl &endpoint,const QList<QPair<QString,QString>> &items){
    QUrl url=endpoint;QUrlQuery query(url);
    for(const auto &item:items){query.removeAllQueryItems(item.first);query.addQueryItem(item.first,item.second);}
    url.setQuery(query);return url;
}
bool configuration(const Dataset &dataset,Config &config,QString &error){
    const QJsonObject object=dataset.definition.value("catalog").toObject();
    config.featureTypes=QRegularExpression(object.value("featureTypePattern").toString());
    config.sheetField=object.value("sheetField").toString();
    config.yearField=object.value("yearField").toString();
    config.formatField=object.value("formatField").toString();
    config.resolutionField=object.value("resolutionField").toString();
    config.urlField=object.value("urlField").toString();
    for(const auto value:object.value("acceptedFormats").toArray())config.acceptedFormats<<value.toString();
    config.sourceAxisOrder=object.value("sourceAxisOrder").toString();
    config.wfsAxisSwap=object.value("wfsAxisOrder").toString()=="northing-easting";
    if(!config.featureTypes.isValid()||config.featureTypes.pattern().isEmpty()
            ||config.sheetField.isEmpty()||config.yearField.isEmpty()
            ||config.formatField.isEmpty()||config.resolutionField.isEmpty()
            ||config.urlField.isEmpty()||config.acceptedFormats.isEmpty()){
        error=QStringLiteral("Invalid WFS asset-catalogue configuration");return false;
    }
    return true;
}
bool numberPair(const QString &text,double &a,double &b){
    const auto fields=text.simplified().split(' ');bool aOk=false,bOk=false;
    if(fields.size()!=2)return false;a=fields[0].toDouble(&aOk);b=fields[1].toDouble(&bOk);
    return aOk&&bOk&&std::isfinite(a)&&std::isfinite(b);
}
double firstNumber(const QString &text,bool &ok){
    static const QRegularExpression expression(QStringLiteral("[-+]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)"));
    const auto match=expression.match(text);if(!match.hasMatch()){ok=false;return 0;}
    return match.captured().toDouble(&ok);
}
QStringList parseCapabilities(const QByteArray &xml,const Config &config,QString &error){
    QXmlStreamReader reader(xml);QSet<QString> names;
    while(!reader.atEnd()){
        reader.readNext();
        if(reader.isDTD()){error=QStringLiteral("DTD is not supported in WFS metadata");return {};}
        if(reader.isStartElement()&&reader.name()==QLatin1String("Name")){
            const QString name=reader.readElementText().trimmed();
            if(config.featureTypes.match(name).hasMatch())names.insert(name);
        }
    }
    if(reader.hasError()||names.isEmpty()){
        error=reader.hasError()?QStringLiteral("Invalid WFS capabilities XML")
                               :QStringLiteral("WFS capabilities contain no configured feature types");
        return {};
    }
    QStringList result=names.values();std::sort(result.begin(),result.end());return result;
}
QVector<Asset> parseFeatures(const QByteArray &xml,const Config &config,
                             const Dataset &dataset,int layerYear,QString &error){
    QXmlStreamReader reader(xml);QVector<Asset> result;bool member=false;
    Asset asset;double lowerA=0,lowerB=0,upperA=0,upperB=0;bool lower=false,upper=false;
    QString resolution;
    while(!reader.atEnd()){
        reader.readNext();
        if(reader.isDTD()){error=QStringLiteral("DTD is not supported in WFS features");return {};}
        if(reader.isStartElement()&&reader.name()==QLatin1String("member")){
            member=true;asset={};asset.year=layerYear;lower=upper=false;resolution.clear();continue;
        }
        if(member&&reader.isStartElement()){
            const QString name=reader.name().toString();
            if(name==config.sheetField)asset.sheet=reader.readElementText().trimmed();
            else if(name==config.yearField){bool ok=false;const int year=reader.readElementText().trimmed().toInt(&ok);if(ok)asset.year=year;}
            else if(name==config.formatField)asset.format=reader.readElementText().trimmed();
            else if(name==config.resolutionField)resolution=reader.readElementText().trimmed();
            else if(name==config.urlField)asset.url=reader.readElementText().trimmed();
            else if(name==QLatin1String("lowerCorner"))lower=numberPair(reader.readElementText(),lowerA,lowerB);
            else if(name==QLatin1String("upperCorner"))upper=numberPair(reader.readElementText(),upperA,upperB);
        }
        if(member&&reader.isEndElement()&&reader.name()==QLatin1String("member")){
            member=false;bool resolutionOk=false;const double spacing=firstNumber(resolution,resolutionOk);
            const QUrl url(asset.url);
            if(config.wfsAxisSwap&&lower&&upper)asset.bounds={lowerB,lowerA,upperB,upperA};
            else if(lower&&upper)asset.bounds={lowerA,lowerB,upperA,upperB};
            if(!asset.sheet.isEmpty()&&asset.year>0&&resolutionOk
                    &&std::abs(spacing-dataset.resolution)<1e-6
                    &&config.acceptedFormats.contains(asset.format,Qt::CaseInsensitive)
                    &&url.scheme()=="https"&&!url.host().isEmpty()&&asset.bounds.valid()){
                asset.key=assetKey(asset.sheet,asset.year,asset.url);result.push_back(asset);
            }
        }
    }
    if(reader.hasError()){error=QStringLiteral("Invalid WFS feature XML");return {};}
    return result;
}
bool extractData(const QByteArray &input,QByteArray &data,QString &name,QString &error,int depth=0){
    if(!input.startsWith("PK")){data=input;return true;}
    if(depth>=2){error=QStringLiteral("Elevation archive nesting is too deep");return false;}
    mz_zip_archive archive={};
    if(!mz_zip_reader_init_mem(&archive,input.constData(),size_t(input.size()),0)){
        error=QStringLiteral("Invalid elevation ZIP archive");return false;
    }
    int selected=-1;QString selectedName;quint64 selectedSize=0;
    const mz_uint count=mz_zip_reader_get_num_files(&archive);
    for(mz_uint i=0;i<count;++i){mz_zip_archive_file_stat stat={};
        if(!mz_zip_reader_file_stat(&archive,i,&stat)||mz_zip_reader_is_file_a_directory(&archive,i)
                ||!mz_zip_reader_is_file_supported(&archive,i))continue;
        const QString candidate=QString::fromUtf8(stat.m_filename);
        const QString suffix=QFileInfo(candidate).suffix().toLower();
        if((suffix=="asc"||suffix=="xyz"||suffix=="txt"||suffix=="zip")
                &&stat.m_uncomp_size>0&&stat.m_uncomp_size<=MaxExpandedBytes){
            selected=int(i);selectedName=candidate;selectedSize=stat.m_uncomp_size;
            if(suffix!="zip")break;
        }
    }
    if(selected<0){mz_zip_reader_end(&archive);error=QStringLiteral("Elevation ZIP has no supported grid file");return false;}
    QByteArray extracted(qsizetype(selectedSize),Qt::Uninitialized);
    const bool ok=mz_zip_reader_extract_to_mem(&archive,mz_uint(selected),extracted.data(),size_t(extracted.size()),0);
    mz_zip_reader_end(&archive);
    if(!ok){error=QStringLiteral("Cannot extract elevation grid");return false;}
    name=selectedName;
    if(extracted.startsWith("PK"))return extractData(extracted,data,name,error,depth+1);
    data=std::move(extracted);return true;
}
bool decodeAsset(const QByteArray &source,const Asset &asset,const Config &config,
                 const Dataset &dataset,Raster &raster,QString &error){
    QByteArray data;QString name=QFileInfo(QUrl(asset.url).path()).fileName();
    if(!extractData(source,data,name,error))return false;
    const QByteArray beginning=data.left(32).trimmed().toLower();
    const bool ascii=beginning.startsWith("ncols")||QFileInfo(name).suffix().compare("asc",Qt::CaseInsensitive)==0;
    const auto rasterBounds=[](const Raster &r){return Bounds{r.transform[0],
        r.transform[3]+r.height*r.transform[5],r.transform[0]+r.width*r.transform[1],r.transform[3]};};
    const auto choose=[&](Raster normal,Raster swapped){
        const auto distance=[&](const Raster &candidate){const Bounds b=rasterBounds(candidate);
            return std::abs((b.minX+b.maxX)-(asset.bounds.minX+asset.bounds.maxX))
                 +std::abs((b.minY+b.maxY)-(asset.bounds.minY+asset.bounds.maxY));};
        raster=distance(swapped)<distance(normal)?std::move(swapped):std::move(normal);
        const Bounds selected=rasterBounds(raster);
        const double tolerance=std::max({selected.maxX-selected.minX,selected.maxY-selected.minY,
                                         asset.bounds.maxX-asset.bounds.minX,asset.bounds.maxY-asset.bounds.minY});
        return distance(raster)<=tolerance;
    };
    if(ascii){
        Raster normal;if(!readAsciiGrid(data,dataset.epsg,normal,error))return false;
        if(config.sourceAxisOrder=="easting-northing")raster=std::move(normal);
        else {Raster swapped=normal;if(!swapRasterAxes(swapped,error))return false;
            if(config.sourceAxisOrder=="northing-easting")raster=std::move(swapped);
            else if(!choose(std::move(normal),std::move(swapped))){error=QStringLiteral("Elevation grid axes do not match its catalogue footprint");return false;}}
    }else if(config.sourceAxisOrder=="catalog-detected"){
        Raster normal,swapped;
        if(!readXyzGrid(data,dataset.epsg,dataset.resolution,false,normal,error)
                ||!readXyzGrid(data,dataset.epsg,dataset.resolution,true,swapped,error))return false;
        if(!choose(std::move(normal),std::move(swapped))){error=QStringLiteral("Elevation XYZ axes do not match its catalogue footprint");return false;}
    }else if(!readXyzGrid(data,dataset.epsg,dataset.resolution,
                          config.sourceAxisOrder=="northing-easting",raster,error))return false;
    if(raster.epsg!=dataset.epsg
            ||std::abs(raster.transform[1]-dataset.resolution)>1e-8
            ||std::abs(raster.transform[5]+dataset.resolution)>1e-8){
        error=QStringLiteral("Downloaded elevation grid has unexpected CRS or resolution");return false;
    }
    return true;
}

class WfsSource final:public Source{
public:
    WfsSource(QString cacheRoot,Dataset data,double spacing,Report &output)
        :root(std::move(cacheRoot)),dataset(std::move(data)),projection(dataset.epsg),
          targetSpacing(spacing),report(output){}
    bool prepare(const QVector<Point> &points,std::atomic_bool &cancel,
                 const Progress &progress,QString &error) override{
        rasters.clear();last=-1;
        if(!configuration(dataset,config,error))return false;
        Bounds request;bool any=false;
        request.minX=request.minY=std::numeric_limits<double>::infinity();
        request.maxX=request.maxY=-request.minX;
        for(Point point:points){if(cancel)return false;XY xy;
            if(projection.forward(point,xy)&&inside(xy)){
                request.minX=std::min(request.minX,xy.x);request.maxX=std::max(request.maxX,xy.x);
                request.minY=std::min(request.minY,xy.y);request.maxY=std::max(request.maxY,xy.y);any=true;
            }}
        if(!any)return true;const double margin=std::max(dataset.resolution*2,targetSpacing*.5);
        request.minX-=margin;request.minY-=margin;request.maxX+=margin;request.maxY+=margin;
        directory=QDir(root).filePath(dataset.directory);
        if(!QDir().mkpath(directory)){error=QStringLiteral("Cannot create elevation cache directory: %1").arg(directory);return false;}
        loadIndex();QStringList wanted;bool known=false;
        for(const Query &query:queries)if(query.bounds.contains(request)){wanted=query.assets;known=true;break;}
        if(!known){
            QString issue;QVector<Asset> discovered=discover(request,cancel,progress,issue);
            if(cancel)return false;
            if(!issue.isEmpty())report.issue(QStringLiteral("Elevation catalogue: %1").arg(issue));
            if(issue.isEmpty()){
                QMap<QString,Asset> newest;
                for(const Asset &asset:discovered){const Asset old=newest.value(asset.sheet);
                    if(old.sheet.isEmpty()||asset.year>old.year
                            ||(asset.year==old.year&&asset.url>old.url))newest[asset.sheet]=asset;}
                for(Asset asset:newest){
                    const Asset cached=assets.value(asset.key);
                    if(!cached.file.isEmpty()&&cached.url==asset.url){asset.file=cached.file;
                        asset.sourceBytes=cached.sourceBytes;asset.sha256=cached.sha256;}
                    assets[asset.key]=asset;wanted<<asset.key;
                }
                queries.push_back({request,wanted});saveIndex();known=true;
            }else{
                for(const Asset &asset:assets)if(asset.bounds.overlaps(request))wanted<<asset.key;
            }
        }
        wanted.removeDuplicates();QVector<Asset> missing;
        for(const QString &key:wanted){Asset asset=assets.value(key);if(asset.key.isEmpty())continue;
            const QString path=QDir(directory).filePath(asset.file);
            if(asset.file.isEmpty()||!QFileInfo(path).isFile())missing.push_back(asset);}
        int completed=0;QSet<QString> downloaded;
        for(qsizetype first=0;first<missing.size();first+=dataset.concurrentRequests){
            if(cancel)return false;QVector<QUrl> urls;
            const int count=int(std::min(qsizetype(dataset.concurrentRequests),missing.size()-first));
            for(int i=0;i<count;++i)urls<<QUrl(missing[first+i].url);
            auto responses=downloadWave(urls,cancel,[&](int done){if(progress)progress(completed+done,missing.size(),QStringLiteral("Downloading elevation source sheets"));},
                                        {MaxSourceBytes,120000,180000});
            if(cancel)return false;
            for(int i=0;i<count;++i){Asset &asset=missing[first+i];const auto &response=responses[i];
                if(!response.error.isEmpty()){report.issue(QStringLiteral("%1: %2").arg(asset.sheet,response.error));continue;}
                Raster raster;QString issue;
                if(!decodeAsset(response.bytes,asset,config,dataset,raster,issue)){
                    report.issue(QStringLiteral("%1: %2").arg(asset.sheet,issue));continue;}
                QByteArray tiff;if(!encodeGeoTiff(raster,tiff,issue)){
                    report.issue(QStringLiteral("%1: %2").arg(asset.sheet,issue));continue;}
                asset.file=asset.key+QStringLiteral(".tif");asset.sourceBytes=response.bytes.size();
                asset.sha256=QString::fromLatin1(QCryptographicHash::hash(response.bytes,QCryptographicHash::Sha256).toHex());
                const Bounds actual{raster.transform[0],raster.transform[3]+raster.height*raster.transform[5],
                                    raster.transform[0]+raster.width*raster.transform[1],raster.transform[3]};
                asset.bounds=actual;const QString path=QDir(directory).filePath(asset.file);
                if(!saveFile(path,tiff)||!saveSidecar(asset)){
                    QFile::remove(path);report.issue(QStringLiteral("Cannot store converted elevation sheet %1").arg(asset.sheet));continue;}
                assets[asset.key]=asset;downloaded.insert(asset.key);++report.downloads;
            }
            completed+=count;saveIndex();
        }
        QVector<Asset> ordered;for(const QString &key:wanted){const Asset asset=assets.value(key);
            if(!asset.key.isEmpty()&&asset.bounds.overlaps(request))ordered<<asset;}
        std::sort(ordered.begin(),ordered.end(),[](const Asset&a,const Asset&b){return a.year>b.year;});
        for(const Asset &asset:ordered){if(cancel)return false;const QByteArray bytes=readFile(QDir(directory).filePath(asset.file),MaxCachedTiff);
            Raster raster;QString issue;if(bytes.isEmpty()||!readGeoTiff(bytes,raster,issue)){
                report.issue(QStringLiteral("%1: %2").arg(asset.sheet,issue.isEmpty()?QStringLiteral("cannot read cached GeoTIFF"):issue));continue;}
            if(raster.epsg!=dataset.epsg||raster.transform[2]!=0||raster.transform[4]!=0
                    ||std::abs(raster.transform[1]-dataset.resolution)>1e-8
                    ||std::abs(raster.transform[5]+dataset.resolution)>1e-8){
                report.issue(QStringLiteral("%1: cached raster CRS or spacing does not match the source").arg(asset.sheet));continue;}
            rasters.push_back(std::move(raster));if(!downloaded.contains(asset.key))++report.cacheHits;}
        buildMosaic(request);
        if(rasters.isEmpty()&&known)report.issue(QStringLiteral("No GUGiK source sheet is available for the requested terrain"));
        return true;
    }
    Sample sample(Point point) override{
        XY xy;if(!projection.forward(point,xy)||!inside(xy))return {0,SampleStatus::Outside};
        const int taps=filterTaps(point);if(taps==1)return sampleProjected(xy);
        const double span=targetSpacing;double height=0;
        for(int y=0;y<taps;++y)for(int x=0;x<taps;++x){
            const Sample value=sampleProjected({xy.x+((x+.5)/taps-.5)*span,
                                                xy.y+((y+.5)/taps-.5)*span});
            if(!value.valid())return value;height+=value.height;}
        return {float(height/(taps*taps)),SampleStatus::Valid};
    }
private:
    void buildMosaic(const Bounds &request){
        if(rasters.size()<2)return;
        const double step=dataset.resolution;
        const Raster &reference=rasters.first();
        const double referenceX=reference.transform[0]+step*.5;
        const double referenceY=reference.transform[3]-step*.5;
        const double minCenterX=referenceX+std::floor((request.minX-referenceX)/step)*step;
        const double maxCenterX=referenceX+std::ceil((request.maxX-referenceX)/step)*step;
        const double minCenterY=referenceY+std::floor((request.minY-referenceY)/step)*step;
        const double maxCenterY=referenceY+std::ceil((request.maxY-referenceY)/step)*step;
        const qint64 width=std::llround((maxCenterX-minCenterX)/step)+1;
        const qint64 height=std::llround((maxCenterY-minCenterY)/step)+1;
        if(width<=0||height<=0||width*height>32*1024*1024)return;
        Raster mosaic;mosaic.width=int(width);mosaic.height=int(height);mosaic.epsg=dataset.epsg;
        mosaic.transform={{minCenterX-step*.5,step,0,maxCenterY+step*.5,0,-step}};
        mosaic.hasNoData=true;mosaic.noData=-9999;
        mosaic.values.fill(std::numeric_limits<float>::quiet_NaN(),width*height);
        for(const Raster &source:rasters){
            const int dx=int(std::llround((source.transform[0]-mosaic.transform[0])/step));
            const int dy=int(std::llround((mosaic.transform[3]-source.transform[3])/step));
            const int sourceX0=std::max(0,-dx),sourceY0=std::max(0,-dy);
            const int sourceX1=std::min(source.width,mosaic.width-dx);
            const int sourceY1=std::min(source.height,mosaic.height-dy);
            for(int y=sourceY0;y<sourceY1;++y)for(int x=sourceX0;x<sourceX1;++x){
                const float value=source.values[y*source.width+x];
                float &destination=mosaic.values[(dy+y)*mosaic.width+dx+x];
                if(!std::isfinite(destination)&&std::isfinite(value)
                        &&!(source.hasNoData&&value==source.noData)
                        &&!(dataset.zeroIsNoData&&value==0))destination=value;
            }
        }
        rasters={std::move(mosaic)};last=-1;
    }
    bool inside(XY p)const{return p.x>=dataset.minX&&p.y>=dataset.minY&&p.x<dataset.maxX&&p.y<dataset.maxY;}
    int filterTaps(Point)const{return !std::isfinite(targetSpacing)||targetSpacing<=dataset.resolution*1.05
        ?1:std::clamp(int(std::ceil(targetSpacing/dataset.resolution)),2,32);}
    Sample sampleProjected(XY xy){
        bool noData=false;
        if(last>=0&&last<rasters.size()){const Sample sample=rasters[last].sample(xy,dataset.zeroIsNoData);
            if(sample.valid())return sample;if(sample.status==SampleStatus::NoData)noData=true;}
        for(int i=0;i<rasters.size();++i){if(i==last)continue;const Sample sample=rasters[i].sample(xy,dataset.zeroIsNoData);
            if(sample.valid()){last=i;return sample;}if(sample.status==SampleStatus::NoData)noData=true;}
        return {0,noData?SampleStatus::NoData:SampleStatus::Unavailable};
    }
    QVector<Asset> discover(const Bounds &request,std::atomic_bool &cancel,const Progress &progress,QString &error){
        const QUrl capabilities=operationUrl(dataset.endpoint,{{"SERVICE","WFS"},{"REQUEST","GetCapabilities"},{"VERSION","2.0.0"}});
        auto response=downloadWave({capabilities},cancel,{}, {MaxCatalogueBytes,30000,45000});
        if(cancel)return {};if(response.isEmpty()||!response[0].error.isEmpty()){
            error=response.isEmpty()?QStringLiteral("Cannot read WFS capabilities"):response[0].error;return {};}
        QStringList types=parseCapabilities(response[0].bytes,config,error);if(!error.isEmpty())return {};
        QVector<Asset> found;int completed=0;
        for(qsizetype first=0;first<types.size();first+=4){QVector<QUrl> urls;QVector<int> years;
            const int count=int(std::min(qsizetype(4),types.size()-first));
            for(int i=0;i<count;++i){const QString type=types[first+i];const auto match=config.featureTypes.match(type);
                int year=0;for(int capture=1;capture<=match.lastCapturedIndex();++capture){bool ok=false;const int candidate=match.captured(capture).toInt(&ok);if(ok){year=candidate;break;}}
                years<<year;const QString bbox=config.wfsAxisSwap
                    ?QStringLiteral("%1,%2,%3,%4,urn:ogc:def:crs:EPSG::%5").arg(request.minY,0,'f',3).arg(request.minX,0,'f',3).arg(request.maxY,0,'f',3).arg(request.maxX,0,'f',3).arg(dataset.epsg)
                    :QStringLiteral("%1,%2,%3,%4,urn:ogc:def:crs:EPSG::%5").arg(request.minX,0,'f',3).arg(request.minY,0,'f',3).arg(request.maxX,0,'f',3).arg(request.maxY,0,'f',3).arg(dataset.epsg);
                urls<<operationUrl(dataset.endpoint,{{"SERVICE","WFS"},{"REQUEST","GetFeature"},{"VERSION","2.0.0"},{"TYPENAMES",type},{"SRSNAME",QStringLiteral("urn:ogc:def:crs:EPSG::%1").arg(dataset.epsg)},{"BBOX",bbox}});}
            auto responses=downloadWave(urls,cancel,[&](int done){if(progress)progress(completed+done,types.size(),QStringLiteral("Searching elevation source catalogue"));},{MaxCatalogueBytes,30000,60000});
            if(cancel)return {};
            for(int i=0;i<count;++i){if(!responses[i].error.isEmpty()){error=responses[i].error;return {};}
                QString issue;const auto layer=parseFeatures(responses[i].bytes,config,dataset,years[i],issue);
                if(!issue.isEmpty()){error=issue;return {};}found+=layer;}
            completed+=count;
        }
        return found;
    }
    void loadIndex(){assets.clear();queries.clear();const auto document=QJsonDocument::fromJson(readFile(QDir(directory).filePath(".tsre-elevation-catalog.json"),8*1024*1024));
        const auto object=document.object();if(object.value("version").toInt()!=1
                ||object.value("dataset").toString()!=dataset.id
                ||object.value("epsg").toInt()!=dataset.epsg
                ||std::abs(object.value("resolution").toDouble()-dataset.resolution)>1e-9)return;
        for(const auto value:object.value("assets").toArray()){const auto o=value.toObject();Asset a;
            a.key=o.value("key").toString();a.sheet=o.value("sheet").toString();a.year=o.value("year").toInt();
            a.format=o.value("format").toString();a.url=o.value("url").toString();a.file=o.value("file").toString();
            a.bounds=jsonBounds(o.value("bounds"));a.sourceBytes=qint64(o.value("sourceBytes").toDouble());a.sha256=o.value("sha256").toString();
            const QUrl url(a.url);
            if(!a.key.isEmpty()&&!a.file.contains('/')&&!a.file.contains('\\')&&a.bounds.valid()
                    &&url.scheme()=="https"&&!url.host().isEmpty())assets[a.key]=a;}
        for(const auto value:object.value("queries").toArray()){const auto o=value.toObject();Query q;q.bounds=jsonBounds(o.value("bounds"));
            for(const auto key:o.value("assets").toArray())q.assets<<key.toString();if(q.bounds.valid())queries<<q;}}
    void saveIndex(){QJsonArray assetArray,queryArray;for(const Asset &a:assets)assetArray.append(QJsonObject{{"key",a.key},{"sheet",a.sheet},{"year",a.year},{"format",a.format},{"url",a.url},{"file",a.file},{"bounds",boundsJson(a.bounds)},{"sourceBytes",double(a.sourceBytes)},{"sha256",a.sha256}});
        for(const Query &q:queries){QJsonArray keys;for(const QString &key:q.assets)keys.append(key);queryArray.append(QJsonObject{{"bounds",boundsJson(q.bounds)},{"assets",keys}});}
        saveFile(QDir(directory).filePath(".tsre-elevation-catalog.json"),QJsonDocument(QJsonObject{{"version",1},{"dataset",dataset.id},{"epsg",dataset.epsg},{"resolution",dataset.resolution},{"assets",assetArray},{"queries",queryArray}}).toJson(QJsonDocument::Compact));}
    bool saveSidecar(const Asset &a){return saveFile(QDir(directory).filePath(a.file+".json"),QJsonDocument(QJsonObject{{"dataset",dataset.id},{"sheet",a.sheet},{"year",a.year},{"format",a.format},{"sourceUrl",a.url},{"sourceBytes",double(a.sourceBytes)},{"sourceSha256",a.sha256},{"bounds",boundsJson(a.bounds)}}).toJson(QJsonDocument::Indented));}
    QString root,directory;Dataset dataset;Geo::CrsTransform projection;double targetSpacing;Report &report;Config config;
    QMap<QString,Asset> assets;QVector<Query> queries;QVector<Raster> rasters;int last=-1;
};
}

std::unique_ptr<Source> createWfsElevationSource(const QString &root,const Dataset &dataset,
                                                 double targetSpacing,Report &report){
    return std::make_unique<WfsSource>(root,dataset,targetSpacing,report);
}
}
