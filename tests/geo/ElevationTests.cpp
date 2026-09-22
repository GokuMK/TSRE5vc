#include <tsre/geo/ElevationRaster.h>
#include <tsre/geo/ElevationSource.h>
#include <tsre/geo/ElevationTiffCodec.h>
#include <tsre/geo/CogElevationSource.h>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QUrlQuery>
#include <QtEndian>
#include <QElapsedTimer>
#include <cmath>
#include <cstring>
#include <iostream>

using namespace Elevation;
namespace {
int checks = 0, failures = 0;
void check(bool condition, const char *name) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
}
bool near(double a, double b, double tolerance = 1e-4) { return std::abs(a-b) <= tolerance; }
QByteArray fixture(const char *name) {
    QFile f(QStringLiteral(TSRE_GEO_FIXTURES)+"/"+QString::fromLatin1(name));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}
bool write(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
QByteArray hgt(int side, qint16 value) {
    QByteArray bytes(side*side*2,Qt::Uninitialized);
    for (int i = 0; i < side*side; ++i) qToBigEndian<qint16>(value,bytes.data()+i*2);
    return bytes;
}
QByteArray tinyGeographicCog() {
    const qint32 values[] = {1000,1010,2147483647,1030,1100,1110,1120,1130,
                             1200,1210,1220,1230,1300,1310,1320,1330};
    QByteArray predicted(16*4,Qt::Uninitialized);
    for(int row=0;row<4;++row)for(int x=0;x<4;++x){
        const quint32 value=quint32(values[row*4+x]);
        const quint32 previous=x?quint32(values[row*4+x-1]):0;
        qToLittleEndian<quint32>(value-previous,predicted.data()+4*(row*4+x));
    }
    const QByteArray encoded=qCompress(predicted).mid(4);
    const QByteArray metadata="<GDALMetadata><Item name=\"OFFSET\" sample=\"0\">0</Item>"
        "<Item name=\"SCALE\" sample=\"0\">0.1</Item></GDALMetadata>\0";
    const QByteArray noData("2147483647\0",11);
    constexpr int entries=17,ifd=8,directoryEnd=ifd+2+entries*12+4;
    const int scaleAt=directoryEnd,tieAt=scaleAt+24,keysAt=tieAt+48;
    const int metadataAt=keysAt+40,noDataAt=metadataAt+metadata.size();
    const int tileAt=noDataAt+noData.size();
    QByteArray result(tileAt+encoded.size(),'\0');
    result[0]='I';result[1]='I';qToLittleEndian<quint16>(42,result.data()+2);
    qToLittleEndian<quint32>(ifd,result.data()+4);qToLittleEndian<quint16>(entries,result.data()+ifd);
    int position=ifd+2;
    const auto entry=[&](quint16 tag,quint16 type,quint32 count,quint32 value){
        qToLittleEndian<quint16>(tag,result.data()+position);
        qToLittleEndian<quint16>(type,result.data()+position+2);
        qToLittleEndian<quint32>(count,result.data()+position+4);
        qToLittleEndian<quint32>(value,result.data()+position+8);position+=12;
    };
    entry(256,4,1,4);entry(257,4,1,4);entry(258,3,1,32);entry(259,3,1,8);
    entry(277,3,1,1);entry(284,3,1,1);entry(317,3,1,2);entry(322,4,1,4);
    entry(323,4,1,4);entry(324,4,1,tileAt);entry(325,4,1,encoded.size());
    entry(339,3,1,2);entry(33550,12,3,scaleAt);entry(33922,12,6,tieAt);
    entry(34735,3,20,keysAt);entry(42112,2,metadata.size(),metadataAt);entry(42113,2,noData.size(),noDataAt);
    qToLittleEndian<quint32>(0,result.data()+position);
    const auto number=[&](int at,double value){quint64 raw;std::memcpy(&raw,&value,8);qToLittleEndian<quint64>(raw,result.data()+at);};
    number(scaleAt,.001);number(scaleAt+8,.001);number(scaleAt+16,0);
    const double tieValues[]={0,0,0,19,52,0};for(int i=0;i<6;++i)number(tieAt+8*i,tieValues[i]);
    const quint16 keys[]={1,1,0,4,1024,0,1,2,1025,0,1,1,2048,0,1,4326,2054,0,1,9102};
    for(int i=0;i<20;++i)qToLittleEndian<quint16>(keys[i],result.data()+keysAt+2*i);
    std::memcpy(result.data()+metadataAt,metadata.constData(),size_t(metadata.size()));
    std::memcpy(result.data()+noDataAt,noData.constData(),size_t(noData.size()));
    std::memcpy(result.data()+tileAt,encoded.constData(),size_t(encoded.size()));
    return result;
}
void mutateTag(QByteArray &bytes, quint16 tag, quint32 value) {
    const auto ifd = qFromLittleEndian<quint32>(bytes.constData()+4);
    const auto count = qFromLittleEndian<quint16>(bytes.constData()+ifd);
    for (int i = 0; i < count; ++i) {
        const auto p = ifd+2+12*i;
        if (qFromLittleEndian<quint16>(bytes.constData()+p) == tag) qToLittleEndian<quint32>(value,bytes.data()+p+8);
    }
}
bool mutateGeoKey(QByteArray &bytes, quint16 wanted, quint16 value) {
    if (bytes.size() < 8 || bytes[0] != 'I' || bytes[1] != 'I') return false;
    const auto ifd=qFromLittleEndian<quint32>(bytes.constData()+4);
    if(ifd+2>quint32(bytes.size()))return false;
    const auto count=qFromLittleEndian<quint16>(bytes.constData()+ifd);
    for(quint16 i=0;i<count;++i){const quint32 entry=ifd+2+i*12;
        if(entry+12>quint32(bytes.size()))return false;
        if(qFromLittleEndian<quint16>(bytes.constData()+entry)!=34735)continue;
        const quint32 keyCount=qFromLittleEndian<quint32>(bytes.constData()+entry+4);
        const quint32 offset=qFromLittleEndian<quint32>(bytes.constData()+entry+8);
        if(keyCount<4||offset+keyCount*2>quint32(bytes.size()))return false;
        const quint16 entries=qFromLittleEndian<quint16>(bytes.constData()+offset+6);
        for(quint16 k=0;k<entries;++k){const quint32 key=offset+8+k*8;
            if(key+8>quint32(bytes.size()))return false;
            if(qFromLittleEndian<quint16>(bytes.constData()+key)==wanted){
                qToLittleEndian<quint16>(value,bytes.data()+key+6);return true;}}
    }
    return false;
}
}
void runDownloadTests(const std::function<void(bool,const char*)> &check);
void runArcGisImageServerTests(const std::function<void(bool,const char*)> &check);
void runCzechWcsTests(const std::function<void(bool,const char*)> &check);
void runNoDataFillTests(const std::function<void(bool,const char*)> &check);
int main(int argc, char **argv) {
    QCoreApplication app(argc,argv);
    const auto args = app.arguments();
    if (args.size() >= 4 && (args[1] == "--live" || args[1] == "--live-area"
            || args[1] == "--live-at" || args[1] == "--live-grid"
            || args[1] == "--live-secrets-at" || args[1] == "--live-secrets-grid")) {
        // Explicit opt-in only; normal ctest is fully offline.
        std::atomic_bool cancel{false};
        QVector<Point> points{{52.0,19.0},{52.00005,19.00005}};
        if (args[1] == "--live-at" || args[1] == "--live-secrets-at") {
            bool latOk = false, lonOk = false;
            const double lat = args.value(4).toDouble(&latOk), lon = args.value(5).toDouble(&lonOk);
            const int expected=args[1]=="--live-secrets-at"?7:6;
            if (!latOk || !lonOk || args.size() != expected) return 2;
            points = {{lat,lon},{lat+.00005,lon+.00005}};
        }
        if (args[1] == "--live-area") {
            points.clear();
            for (int y=0; y<16; ++y) for (int x=0; x<16; ++x)
                points.push_back({52.0+y*.0184/15,19.0+x*.0299/15});
        }
        if (args[1] == "--live-grid" || args[1] == "--live-secrets-grid") {
            bool latOk=false,lonOk=false,sideOk=false,latSpanOk=false,lonSpanOk=false;
            const double lat=args.value(4).toDouble(&latOk),lon=args.value(5).toDouble(&lonOk);
            const int side=args.value(6).toInt(&sideOk);
            const double latSpan=args.value(7).toDouble(&latSpanOk),lonSpan=args.value(8).toDouble(&lonSpanOk);
            const int expected=args[1]=="--live-secrets-grid"?10:9;
            if(args.size()!=expected||!latOk||!lonOk||!sideOk||!latSpanOk||!lonSpanOk||side<2||side>4096)return 2;
            points.clear();points.reserve(qsizetype(side)*side);
            for(int y=0;y<side;++y)for(int x=0;x<side;++x)
                points.push_back({lat+y*latSpan/(side-1),lon+x*lonSpan/(side-1)});
        }
        QMap<QString,QString> secrets;
        if(args[1]=="--live-secrets-at"||args[1]=="--live-secrets-grid"){
            const int secretArgument=args[1]=="--live-secrets-at"?6:9;
            QFile file(args[secretArgument]);if(!file.open(QIODevice::ReadOnly)||file.size()>1024*1024)return 2;
            QJsonParseError parseError;const auto document=QJsonDocument::fromJson(file.readAll(),&parseError);
            if(parseError.error!=QJsonParseError::NoError||!document.object().value("secrets").isObject())return 2;
            const auto object=document.object().value("secrets").toObject();
            for(auto it=object.begin();it!=object.end();++it)if(it.value().isString())secrets.insert(it.key(),it.value().toString());
        }
        QElapsedTimer timer; timer.start();
        const auto result = generate(args[2],args[3],points,1.0,0,cancel,{},secrets);
        std::cout << "success=" << result.success() << " primary=" << result.report.primarySamples
                  << " fallback=" << result.report.fallbackSamples << " downloads=" << result.report.downloads
                  << " nodata=" << result.report.noDataSamples << " unavailable=" << result.report.unavailableSamples
                  << " cache=" << result.report.cacheHits << " elapsedMs=" << timer.elapsed() << '\n';
        if (args[1] == "--live" || args[1] == "--live-at" || args[1] == "--live-secrets-at")
            for (float h : result.heights) std::cout << h << '\n';
        std::cerr << result.error.toStdString() << '\n' << result.report.issues.join('\n').toStdString() << '\n';
        return result.success() ? 0 : 1;
    }
    QString error;
    Raster r;
    const auto native = fixture("native-sample.tif");
    check(readGeoTiff(native,r,error),"decode live numeric GeoTIFF fixture");
    check(r.width == 32 && r.height == 32 && r.epsg == 2180,"TIFF dimensions and CRS");
    check(r.transform[1] == 1 && r.transform[5] == -1,"Geoportal source spacing is exactly 1 m");
    check(near(r.sample({566600.5,243131.5}).height,203.14999389648438),"TIFF top-left pixel center");
    check(near(r.sample({566601,243131.5}).height,(203.14999389648438+203.1199951171875)/2),"bilinear interpolation");
    check(r.sample({566599,243131}).status == SampleStatus::Outside,"outside raster never clamps silently");
    check(!readGeoTiff(fixture("evrf-sample.tif"),r,error) && error.contains("RGB"),"reject service RGB TIFF as elevation");
    check(!readGeoTiff(QByteArray("<ExceptionReport/>"),r,error),"reject service XML as TIFF");
    check(!readGeoTiff(native.left(400),r,error),"reject truncated TIFF");
    QByteArray userDefinedCrs=native;
    check(mutateGeoKey(userDefinedCrs,3072,32767)
        && readWcsTiff(userDefinedCrs,2180,r,error) && r.epsg==2180,
        "service metadata resolves an otherwise user-defined GeoTIFF projected CRS");
    check(!readGeoTiff(userDefinedCrs,r,error),
        "standalone user-defined GeoTIFF CRS remains unsupported without service metadata");
    auto bad = native; mutateTag(bad,273,0xfffffff0);
    check(!readGeoTiff(bad,r,error),"reject strip offset outside input");
    bad = native; mutateTag(bad,259,5);
    check(!readGeoTiff(bad,r,error),"reject data falsely labelled as LZW");
    bad = native; mutateTag(bad,256,65535);
    check(!readGeoTiff(bad,r,error),"reject inconsistent strip dimensions");
    QVector<float> decodedBlock;
    const QByteArray lzwFloat = QByteArray::fromBase64("gAAACAH4AAAAIAAABgYAAAApIUBA");
    check(decodeTiffBlock(lzwFloat,5,1,true,32,3,4,1,decodedBlock,error)
          && decodedBlock == QVector<float>({1.0f,2.0f,-3.5f,42.25f}),
          "TIFF LZW Float32 block decoding");
    const QByteArray predictedLzwFloat = QByteArray::fromBase64("gA/AKAQQ+QBgZLXAAAAAAAAAAEBA");
    check(decodeTiffBlock(predictedLzwFloat,5,3,true,32,3,4,1,decodedBlock,error)
          && decodedBlock == QVector<float>({1.0f,2.0f,-3.5f,42.25f}),
          "TIFF floating-point predictor decoding");
    check(!decodeTiffBlock(predictedLzwFloat.chopped(1),5,3,true,32,3,4,1,decodedBlock,error),
          "truncated TIFF LZW block is rejected");
    QByteArray float64Block(4*8,Qt::Uninitialized);
    const double float64Values[] = {1.25,-2.5,48.125,399.75};
    for (int i=0; i<4; ++i) {
        quint64 raw;
        std::memcpy(&raw,&float64Values[i],8);
        qToLittleEndian<quint64>(raw,float64Block.data()+8*i);
    }
    check(decodeTiffBlock(float64Block,1,1,true,64,3,4,1,decodedBlock,error)
          && decodedBlock == QVector<float>({1.25f,-2.5f,48.125f,399.75f}),
          "TIFF Float64 block conversion to the elevation float buffer");
    QByteArray float32Block(4*4,Qt::Uninitialized);
    const float float32Values[] = {1.0f,2.0f,-3.5f,42.25f};
    for (int i=0; i<4; ++i) {
        quint32 raw;
        std::memcpy(&raw,&float32Values[i],4);
        qToLittleEndian<quint32>(raw,float32Block.data()+4*i);
    }
    const QByteArray deflatedFloat = qCompress(float32Block).mid(4);
    check(decodeTiffBlock(deflatedFloat,8,1,true,32,3,4,1,decodedBlock,error)
          && decodedBlock == QVector<float>({1.0f,2.0f,-3.5f,42.25f}),
          "TIFF Deflate Float32 block decoding");
    QByteArray predictedFloat(4*4,Qt::Uninitialized);
    for(int x=0;x<4;++x){quint32 value,previous=0;std::memcpy(&value,&float32Values[x],4);
        if(x)std::memcpy(&previous,&float32Values[x-1],4);
        qToLittleEndian<quint32>(value-previous,predictedFloat.data()+4*x);}
    const QByteArray deflatedPredictedFloat=qCompress(predictedFloat).mid(4);
    check(decodeTiffBlock(deflatedPredictedFloat,8,2,true,32,3,4,1,decodedBlock,error)
          && decodedBlock == QVector<float>({1.0f,2.0f,-3.5f,42.25f}),
          "TIFF horizontal predictor decodes Float32 sample words");
    const qint32 integerValues[] = {1000,1002,-5,250,-20,-18,0,50000};
    QByteArray predictedInteger(8*4,Qt::Uninitialized);
    for(int row=0;row<2;++row)for(int x=0;x<4;++x){
        const quint32 value=quint32(integerValues[row*4+x]);
        const quint32 previous=x?quint32(integerValues[row*4+x-1]):0;
        qToLittleEndian<quint32>(value-previous,predictedInteger.data()+4*(row*4+x));
    }
    const QByteArray deflatedInteger=qCompress(predictedInteger).mid(4);
    check(decodeTiffBlock(deflatedInteger,8,2,true,32,2,4,2,decodedBlock,error)
          && decodedBlock == QVector<float>({1000,1002,-5,250,-20,-18,0,50000}),
          "TIFF Deflate signed Int32 horizontal-predictor decoding");
    check(!decodeTiffBlock(deflatedInteger,8,3,true,32,2,4,2,decodedBlock,error),
          "floating-point TIFF predictor is rejected for integer samples");
    check(readGeoTiff(fixture("signed16-big-endian.tif"),r,error),"big-endian signed16 TIFF with two strips");
    check(r.sample({100.5,199.5}).height == -2 && r.sample({101.5,199.5}).height == 0
          && r.sample({100.5,198.5}).height == 10,"signed strips preserve row order and zero height");
    check(r.sample({101.5,198.5}).status == SampleStatus::NoData,"TIFF NoData tag");
    check(readGeoTiff(fixture("signed16-pixel-is-point.tif"),r,error)
          && r.sample({100,200}).height == -2,"PixelIsPoint registration has no half-pixel displacement");
    check(readAsciiGrid(fixture("evrf-sample.asc"),2180,r,error),"decode live multipart EVRF ASCII grid");
    check(r.width == 32 && r.height == 32 && r.transform[1] == 1,"ASCII grid is 1 m");
    check(near(r.sample({566600.5,243131.5}).height,203.38999938964844),"ASCII numeric heights preserve decimals");
    check(!readAsciiGrid("ncols 9999999999 nrows 2 cellsize 1 xllcorner 0 yllcorner 0",2180,r,error),"reject excessive ASCII allocation");
    check(!readAsciiGrid("ncols 2 nrows 2 cellsize 1 xllcorner 0 yllcorner 0 1 2",2180,r,error),"reject truncated ASCII");
    check(readAsciiGrid("ncols 2 nrows 2 xllcenter 0 yllcenter 0 cellsize 1 NODATA_value -9999\n0 -2 4 -9999",4326,r,error),"ASCII center origin and NoData");
    check(r.sample({0,1}).valid() && r.sample({0,1}).height == 0,"zero is valid in generic raster");
    check(r.sample({0,1},true).status == SampleStatus::NoData,"dataset-specific zero policy");
    check(r.sample({1,1}).height == -2,"negative elevation preserved");
    check(r.sample({.5,.5}).status == SampleStatus::NoData,"interpolation does not mix a void");
    check(r.sample({0,0}).valid(),"zero-weight void neighbor does not invalidate a post");
    Raster rotated;
    rotated.width = rotated.height = 2; rotated.values = {1,2,3,4};
    rotated.transform = {{100,0,-2,200,2,0}};
    check(near(rotated.sample({99,201}).height,1),"rotated affine grid");
    XY p;
    const Geo::CrsTransform cs92(2180);
    const Geo::CrsTransform geographic(4326);
    const Geo::CrsTransform unsupported(9999);
    check(cs92.forward({52,19},p) && near(p.x,500000,1e-5)
          && near(p.y,459309.20940316166,.001),"CS92 reference from geographic WCS subset");
    check(!readGeoTiff(fixture("projection-west.tif"),r,error),"reject service response with user-defined CRS");
    check(!readGeoTiff(fixture("projection-east.tif"),r,error),"reject unidentifiable CRS without assuming it is CS92");
    const auto references = QJsonDocument::fromJson(fixture("projection-reference.json")).object().value("points").toArray();
    check(references.size() == 130,"independent projection reference grid");
    double maxProjectionError = 0;
    for (const auto entry : references) {
        const auto point = entry.toObject();
        const bool valid = cs92.forward({point["latitude"].toDouble(),point["longitude"].toDouble()},p);
        const double delta = std::hypot(p.x-point["x"].toDouble(),p.y-point["y"].toDouble());
        maxProjectionError = std::max(maxProjectionError,delta);
        check(valid && delta < .001,"CS92 forward projection agrees with independent PROJ reference within 1 mm");
    }
    std::cout << "Maximum projection difference: " << maxProjectionError << " m\n";
    check(geographic.forward({52,19},p) && p.x == 19 && p.y == 52,"explicit lon/lat raster order");
    const Geo::CrsTransform sweref99Tm(3006),etrs89Utm33(25833);
    XY swerefPoint,utmPoint;
    check(sweref99Tm.forward({59.3,18.05},swerefPoint)
        && etrs89Utm33.forward({59.3,18.05},utmPoint)
        && near(swerefPoint.x,utmPoint.x,1e-6) && near(swerefPoint.y,utmPoint.y,1e-6),
        "SWEREF 99 TM uses the ETRS89 UTM zone 33 projection parameters");
    check(!unsupported.forward({52,19},p) && !cs92.forward({0,0},p),"reject unsupported projection and domain");
    check(readHgt(hgt(3,-5),-1,-2,r,error),"read big-endian HGT");
    check(sampleLegacyHgt(r,{-.5,-1.5}).valid() && sampleLegacyHgt(r,{-.5,-1.5}).height == -5,"negative HGT positions and heights");
    QByteArray gradientHgt(18,Qt::Uninitialized);
    for(int i=0;i<9;++i)qToBigEndian<qint16>(qint16(i*10),gradientHgt.data()+2*i);
    check(readHgt(gradientHgt,0,0,r,error)
        && near(sampleLegacyHgt(r,{.75,.25}).height,20),
        "HGT interpolation uses side minus one geographic intervals");
    check(!readHgt(QByteArray(19,'x'),0,0,r,error),"reject malformed HGT dimensions");
    check(readHgt(hgt(3,-32768),0,0,r,error) && sampleLegacyHgt(r,{.5,.5}).status == SampleStatus::NoData,"HGT void detection");
    const auto catalog = datasets(error);
    check(!catalog.isEmpty() && error.isEmpty(),"embedded dataset catalogue");
    QJsonArray entries;
    for (const auto &entry : catalog) entries.append(entry.definition);
    const auto parseEntries = [&](const QJsonArray &items) {
        return parseDatasets(QJsonDocument(QJsonObject{{"version",1},{"datasets",items}}).toJson(),error);
    };
    QJsonObject wcsEntry;
    for (const auto &dataset : catalog) {
        if (dataset.id == QStringLiteral("pl.gugik.nmt1.kron86")) {
            wcsEntry = dataset.definition;
            break;
        }
    }
    auto invalid = wcsEntry; invalid["id"] = "fixture.invalid-auth";
    invalid["authentication"] = QJsonObject{{"type","unsupported"},{"secret","fixture.key"}};
    auto mixed = entries; mixed.insert(1,invalid);
    auto validEntries = parseEntries(mixed);
    check(validEntries.size() == catalog.size() && error.contains("fixture.invalid-auth")
        && validEntries.last().id == catalog.last().id,"invalid authentication rejects only its object and parsing continues");
    invalid = wcsEntry; invalid["id"] = "fixture.invalid-crs"; invalid["crs"] = 9999;
    mixed = entries; mixed.prepend(invalid); mixed.append(wcsEntry); mixed.append(false);
    validEntries = parseEntries(mixed);
    check(validEntries.size() == catalog.size() && error.contains("fixture.invalid-crs")
        && error.contains(wcsEntry.value("id").toString()) && error.contains("entry "),
        "unsupported CRS, duplicate ID and non-object are individually rejected");
    invalid.remove("origin");
    const auto invalidGrid = parseEntries(QJsonArray{invalid});
    check(invalidGrid.isEmpty() && error.contains("grid definition"),"invalid grid is diagnosed");
    check(parseDatasets("{",error).isEmpty() && error.contains("JSON"),"malformed JSON has a file-level diagnostic");
    check(parseDatasets("{\"version\":1}",error).isEmpty() && !error.isEmpty(),"missing datasets array is a file-level error");
    QMap<QString,Dataset> byId;
    for (const auto &entry : catalog) byId.insert(entry.id,entry);
    bool requiredPresent = true;
    for (const QString &id : {QString("pl.gugik.nmt1.kron86"),QString("pl.gugik.nmt1.evrf2007"),
                             QString("cz.cuzk.dmr4g"),QString("cz.cuzk.dmr5g")})
        requiredPresent &= byId.contains(id);
    check(requiredPresent && byId.size() == catalog.size(),"required datasets and unique IDs survive catalogue expansion");
    if (!requiredPresent) return 1;
    const auto finland = byId.value("fi.nls.dem2");
    const auto netherlands = byId.value("nl.pdok.ahn.dtm05");
    const auto england = byId.value("gb.ea.lidar.dtm1");
    const auto estonia = byId.value("ee.maru.dtm1");
    const auto denmark = byId.value("dk.datafordeler.dhm.terraen");
    const auto worldHgt = byId.value("world-hgt");
    const auto austria = byId.value("at.bev.als-dgm1");
    const auto luxembourg = byId.value("lu.act.dtm2024");
    const auto wales = byId.value("gb.wales.lidar.dtm1");
    const auto sweden = byId.value("se.lantmateriet.markhojdmodell1");
    const auto switzerland = byId.value("ch.swisstopo.swissalti3d.2m");
    const auto portugal = byId.value("pt.dgt.mdt2m");
    const auto gedtm = byId.value("world.gedtm30");
    const auto france = byId.value("fr.ign.lidar-hd.mnt05");
    check(defaultFileSourceId(catalog) == worldHgt.id && worldHgt.provider == "file"
        && worldHgt.directory == "world_hgt" && worldHgt.fileGrid == "degree"
        && worldHgt.minX == -180 && worldHgt.minY == -90 && worldHgt.maxX == 180 && worldHgt.maxY == 90,
        "catalogue defines the world-wide default HGT file source");
    check(fileDownloadUrl(worldHgt,-1,-2).toString()
        == "https://s3.amazonaws.com/elevation-tiles-prod/skadi/S01/S01W002.hgt.gz",
        "degree-grid download template resolves southern and western cells");
    check(gedtm.provider == "file" && gedtm.format == "geotiff"
        && gedtm.fileGrid == "cog" && gedtm.epsg == 4326
        && gedtm.resolution == 30 && gedtm.cogOverviewFactor == 1
        && gedtm.fileRevision == "v20250619"
        && gedtm.directory == "world_gedtm30"
        && gedtm.downloadUrlTemplate.contains("filtered.dtm_edtm_m_30m"),
        "GEDTM30 catalogue defines a geographic bare-earth range COG");
    QTemporaryDir geographicCogRoot;
    Dataset geographicCog=gedtm;
    geographicCog.id="fixture.geographic-cog";geographicCog.directory="fixture_geographic_cog";
    geographicCog.downloadUrlTemplate="https://fixture.invalid/gedtm.tif";
    check(write(QDir(geographicCogRoot.path()).filePath("fixture_geographic_cog/gedtm.tif"),tinyGeographicCog()),
        "create geographic signed-Int32 COG fixture");
    Report geographicCogReport;std::atomic_bool geographicCogCancel{false};QString geographicCogError;
    auto geographicCogSource=createCogElevationSource(geographicCogRoot.path(),geographicCog,geographicCogReport);
    const QVector<Point> geographicCogPoints{{51.9994999999,19.0005000001},{51.9994999999,19.0025000001}};
    const bool geographicCogPrepared=geographicCogSource->prepare(geographicCogPoints,geographicCogCancel,{},geographicCogError);
    const Sample geographicValue=geographicCogSource->sample(geographicCogPoints[0]);
    const Sample geographicNoData=geographicCogSource->sample(geographicCogPoints[1]);
    if(!geographicCogPrepared||!geographicCogError.isEmpty()||!near(geographicValue.height,100)
            ||geographicNoData.status!=SampleStatus::NoData)
        std::cerr << "geographic COG: " << geographicCogPrepared << ' ' << geographicCogError.toStdString()
                  << " issues=" << geographicCogReport.issues.join('|').toStdString()
                  << " value=" << geographicValue.height << '/' << int(geographicValue.status)
                  << " nodata=" << geographicNoData.height << '/' << int(geographicNoData.status) << '\n';
    check(geographicCogPrepared && geographicCogError.isEmpty()
        && near(geographicValue.height,100) && geographicNoData.status==SampleStatus::NoData,
        "geographic COG applies Int32 predictor, GDAL scale and raw NoData");
    check(austria.provider == "file" && austria.format == "geotiff"
        && austria.fileGrid == "projected" && austria.epsg == 3035
        && austria.fileTileSize == 50000 && austria.concurrentRequests == 4
        && austria.fileRevision == "20250915"
        && austria.downloadUrlTemplate.contains("N{northing}E{easting}"),
        "Austria catalogue defines a projected 50 km range-COG grid");
    check(luxembourg.provider == "file" && luxembourg.format == "geotiff"
        && luxembourg.fileGrid == "cog" && luxembourg.epsg == 2169
        && luxembourg.resolution == 1 && luxembourg.concurrentRequests == 4
        && luxembourg.fileRevision == "lidar2024"
        && luxembourg.downloadUrlTemplate.endsWith("/MNT_Lidar2024.tif"),
        "Luxembourg catalogue selects the 1 m overview of one national range COG");
    check(wales.provider == "file" && wales.format == "geotiff"
        && wales.fileGrid == "cog" && wales.epsg == 27700
        && wales.resolution == 1 && wales.coordinateTransform == "ostn15-lite"
        && wales.transformAssetPath == "assets/geo/OSTN15_OSGM15_Lite_DataFile.txt"
        && wales.transformAssetUrl.host() == "www.ordnancesurvey.co.uk",
        "Wales catalogue defines a single range COG and on-demand OSTN15 Lite asset");
    check(sweden.provider == "file" && sweden.format == "geotiff"
        && sweden.fileGrid == "stac" && sweden.epsg == 3006
        && sweden.resolution == 1 && sweden.stacSearchRoot && sweden.stacRange
        && sweden.stacAssetEpsg == 5845
        && sweden.stacResolutionProperty == "geometriskupplosning"
        && sweden.basicUsernameSecret == "geo.elevation.se.lantmateriet.username"
        && sweden.basicPasswordSecret == "geo.elevation.se.lantmateriet.password",
        "Sweden catalogue defines authenticated root-STAC range COG access");
    auto invalidSweden=sweden.definition;invalidSweden["id"]="fixture.invalid-sweden-auth";
    auto invalidSwedenAuth=invalidSweden["authentication"].toObject();
    invalidSwedenAuth["passwordSecret"]="../outside";invalidSweden["authentication"]=invalidSwedenAuth;
    mixed=entries;mixed.prepend(invalidSweden);validEntries=parseEntries(mixed);
    check(validEntries.size()==catalog.size()&&error.contains("fixture.invalid-sweden-auth"),
        "invalid STAC credential reference rejects only its catalogue object");
    auto invalidTransform = wales.definition;
    invalidTransform["id"] = "fixture.invalid-transform";
    auto invalidTransformDefinition = invalidTransform["coordinateTransform"].toObject();
    invalidTransformDefinition.remove("archiveEntry");
    invalidTransform["coordinateTransform"] = invalidTransformDefinition;
    mixed = entries; mixed.prepend(invalidTransform);
    validEntries = parseEntries(mixed);
    check(validEntries.size() == catalog.size() && error.contains("fixture.invalid-transform"),
        "invalid transform asset rejects only its catalogue object");
    check(switzerland.provider == "file" && switzerland.format == "geotiff"
        && switzerland.fileGrid == "stac" && switzerland.epsg == 2056
        && switzerland.resolution == 2
        && switzerland.stacEndpoint.host() == "data.geo.admin.ch"
        && switzerland.stacCollection == "ch.swisstopo.swissalti3d",
        "Switzerland catalogue defines the generic 2 m STAC/GeoTIFF source");
    check(portugal.provider == "file" && portugal.format == "geotiff"
        && portugal.fileGrid == "directory" && portugal.directory == "pt_dgt_mdt2m"
        && portugal.epsg == 3763 && portugal.resolution == 2
        && portugal.downloadPage.host() == "cdd.dgterritorio.gov.pt"
        && !portugal.information.isEmpty() && !portugal.license.isEmpty(),
        "Portugal catalogue defines a user-managed indexed GeoTIFF directory");
    const QUrlQuery franceQuery(wmsUrl(france,{636,-6702}));
    check(france.provider == "wms-1.3.0" && france.epsg == 2154
        && france.resolution == 1 && france.blockPixels == 1024
        && france.concurrentRequests == 4
        && franceQuery.queryItemValue("SERVICE") == "WMS"
        && franceQuery.queryItemValue("REQUEST") == "GetMap"
        && franceQuery.queryItemValue("LAYERS") == france.coverage
        && franceQuery.queryItemValue("STYLES") == "normal"
        && franceQuery.queryItemValue("CRS") == "EPSG:2154"
        && franceQuery.queryItemValue("FORMAT") == "image/geotiff"
        && franceQuery.queryItemValue("WIDTH") == "1026"
        && franceQuery.queryItemValue("HEIGHT") == "1026",
        "France catalogue defines generic 1 m numeric-WMS GeoTIFF blocks");
    auto manualFile = worldHgt.definition;
    manualFile["id"] = "fixture.manual-hgt"; manualFile["directory"] = "manual_hgt";
    manualFile.remove("download");
    auto manualCatalog = parseEntries({manualFile});
    check(manualCatalog.size() == 1 && manualCatalog.first().downloadUrlTemplate.isEmpty(),
        "file source without download definition remains manual-only");
    manualFile["directory"] = "../outside";
    check(parseEntries({manualFile}).isEmpty(),"file-source directory traversal is rejected");
    check(denmark.apiKeyParameter == "apikey" && denmark.apiKeySecret == "geo.elevation.dk.datafordeler.apiKey"
        && denmark.resolution == 1 && QUrlQuery(coverageUrl(denmark,{0,0})).queryItemValue("FORMAT") == "GTiff"
        && !QUrlQuery(coverageUrl(denmark,{0,0})).hasQueryItem("apikey"),
        "Denmark uses a 1 m grid and secret reference; public request URL contains no key");
    const QUrlQuery gbQuery(coverageUrl(england,{-8,-3279}));
    check(gbQuery.allQueryItemValues("SUBSET").value(0).startsWith("X(")
        && gbQuery.allQueryItemValues("SUBSET").value(1).startsWith("Y(")
        && gbQuery.queryItemValue("SCALESIZE") == "i(1026),j(1026)",
        "England uses projected subset axes separately from scaling axes");
    check(QUrlQuery(coverageUrl(estonia,{2689,-8086})).queryItemValue("FORMAT") == "image/tiff"
        && QUrlQuery(coverageUrl(byId.value("de.bw.dgm1"),{0,0})).queryItemValue("FORMAT") == "GeoTIFF",
        "Estonia format override preserves existing WCS 1 TIFF alias");
    const auto eeFixture = fixture("../2026-09-19/ee-gb/ee-land-32.tif");
    check(readGeoTiff(eeFixture,r,error) && r.epsg == 3857 && r.transform[1] == 2
        && near(r.values.value(528),2.690624952316284) && r.values.value(0) == 0,
        "Estonia live Float32 fixture preserves fractional and zero heights");
    const Raster localFixture=r;
    QTemporaryDir localRoot;
    Dataset localDirectory;
    localDirectory.id="fixture.local-directory";localDirectory.provider="file";
    localDirectory.format="geotiff";localDirectory.fileGrid="directory";
    localDirectory.directory="local_tiff";localDirectory.epsg=3857;localDirectory.resolution=2;
    localDirectory.minX=localFixture.transform[0];
    localDirectory.maxX=localFixture.transform[0]+localFixture.width*localFixture.transform[1];
    localDirectory.maxY=localFixture.transform[3];
    localDirectory.minY=localFixture.transform[3]+localFixture.height*localFixture.transform[5];
    const QString localFile=QDir(localRoot.path()).filePath("local_tiff/tile.tif");
    check(localRoot.isValid()&&write(localFile,eeFixture),"create user-managed GeoTIFF directory fixture");
    constexpr double webRadius=6378137.0,pi=3.14159265358979323846;
    const double localX=localFixture.transform[0]+10.5*localFixture.transform[1];
    const double localY=localFixture.transform[3]+10.5*localFixture.transform[5];
    const Point localPoint{(2*std::atan(std::exp(localY/webRadius))-pi/2)*180/pi,
                           localX/webRadius*180/pi};
    Report localReport;std::atomic_bool localCancel{false};QString localError;
    auto localSource=createCogElevationSource(localRoot.path(),localDirectory,localReport);
    const bool localPrepared=localSource->prepare({localPoint},localCancel,{},localError);
    QFile localIndex(QDir(localRoot.path()).filePath("local_tiff/.tsre-elevation-index.json"));
    const bool localIndexOpen=localIndex.open(QIODevice::ReadOnly);
    const QJsonObject localIndexRoot=QJsonDocument::fromJson(localIndex.readAll()).object();
    const QJsonArray localIndexFiles=localIndexRoot.value("files").toArray();
    const QJsonObject localIndexFile=localIndexFiles.isEmpty()
        ?QJsonObject():localIndexFiles.first().toObject();
    check(localPrepared
        && localError.isEmpty() && localSource->sample(localPoint).valid()
        && localReport.cacheHits==1
        && localIndexOpen && localIndexRoot.value("dataset")==localDirectory.id
        && localIndexFile.value("name")=="tile.tif"
        && localIndexFile.value("bounds").toArray().size()==4,
        "user-managed GeoTIFF directory indexes bounds and loads only overlapping local files");
    bad = eeFixture; mutateTag(bad,257,31);
    check(readGeoTiff(bad,r,error) && r.values.size() == 32*31,
        "final TIFF strip may include padding rows beyond image height");
    check(!readGeoTiff(bad.chopped(4),r,error),"truncated padded strip remains rejected");
    mutateTag(bad,279,4097);
    check(!readGeoTiff(bad,r,error),"arbitrary extra strip bytes remain rejected");
    check(readGeoTiff(fixture("../2026-09-19/ee-gb/gb-catalog-32.tif"),r,error)
        && r.epsg == 3857 && r.hasNoData && near(r.values.value(59),33.3849983215332),
        "England live big-endian tiled Float32 fixture and NoData decode");
    check(r.sample({r.transform[0]+r.transform[1]*.5,r.transform[3]+r.transform[5]*.5}).status == SampleStatus::NoData,
        "England extreme negative sentinel is NoData, never a terrain height");
    Dataset probeGrid = england;
    probeGrid.blockPixels = 30; probeGrid.originX = -16384; probeGrid.originY = 6715392;
    const Raster expanded = r;
    check(validateRasterGrid(probeGrid,{0,0},r,error),"expanded grid covers all requested sample centres");
    probeGrid.allowExpandedGrid = false;
    check(!validateRasterGrid(probeGrid,{0,0},r,error),"strict datasets reject server-expanded grids");
    probeGrid.allowExpandedGrid = true;
    r.transform[0] += 4;
    check(!validateRasterGrid(probeGrid,{0,0},r,error),"expanded grid cannot miss a requested edge");
    r = expanded; r.transform[1] *= 2;
    check(!validateRasterGrid(probeGrid,{0,0},r,error),"expanded grid cannot exceed its extent budget");
    r = expanded; r.epsg = 2180;
    check(!validateRasterGrid(probeGrid,{0,0},r,error),"expanded grid still requires matching CRS");
    r = expanded; --r.width;
    check(!validateRasterGrid(probeGrid,{0,0},r,error),"expanded grid still requires exact dimensions");
    r = expanded; r.transform[2] = .01;
    check(!validateRasterGrid(probeGrid,{0,0},r,error),"expanded grid rejects rotation");
    r = expanded; r.transform[0] = std::numeric_limits<double>::quiet_NaN();
    check(!validateRasterGrid(probeGrid,{0,0},r,error),"expanded grid rejects non-finite transforms");
    const Geo::CrsTransform finlandProjection(3067);
    const Geo::CrsTransform europeProjection(3035);
    const Geo::CrsTransform swissProjection(2056);
    const Geo::CrsTransform luxembourgProjection(2169);
    const Geo::CrsTransform portugalProjection(3763);
    const Geo::CrsTransform franceProjection(2154);
    check(finland.epsg == 3067 && Geo::CrsTransform::supports(3067) && finlandProjection.forward({60,27},p)
        && near(p.x,500000),"Finland retains its supported native TM35FIN grid");
    check(europeProjection.forward({52,10},p) && near(p.x,4321000,.001) && near(p.y,3210000,.001),
        "ETRS89 LAEA Europe projection origin");
    swissProjection.forward({46.9510811111,7.4386372222},p);
    check(near(p.x,2600000,1) && near(p.y,1200000,1),
        "official swisstopo Bern reference maps to LV95 origin");
    struct LuxembourgReference { double latitude,longitude,easting,northing; };
    const LuxembourgReference luxembourgReferences[] = {
        {49.6116,6.1319,77382.1116744523,75219.76194942078},
        {49.5,5.8,53334.455034026396,62871.688285392534},
        {50.1,6.45,100166.93650059443,129580.7635124627},
        {49.8,6.0,67896.05848052897,96187.21516507938}
    };
    bool luxembourgMatches = Geo::CrsTransform::supports(2169);
    for (const auto &reference : luxembourgReferences) {
        luxembourgMatches &= luxembourgProjection.forward(
            {reference.latitude,reference.longitude},p)
            && near(p.x,reference.easting,.02) && near(p.y,reference.northing,.02);
    }
    check(luxembourgMatches,
        "LUREF2020 and Luxembourg TM agree with official ACT converter controls within 2 cm");
    check(Geo::CrsTransform::supports(3763)
        && portugalProjection.forward({39.4417496,-8.6906939},p)
        && near(p.x,-48000,.05) && near(p.y,-25000,.05),
        "Portugal TM06 agrees with a DGT MDT tile control within 5 cm");
    check(Geo::CrsTransform::supports(2154)
        && franceProjection.forward({46.5,3},p)
        && near(p.x,700000,.001) && near(p.y,6600000,.001),
        "Lambert-93 false origin maps exactly to its published EPSG:2154 coordinates");
    check(franceProjection.forward({48,-2},p)
        && near(p.x,327351.199,.002) && near(p.y,6778425.923,.002),
        "Lambert-93 agrees with the official IGN numeric control within 2 mm");
    Geo::CrsTransform britishProjection(27700);
    std::vector<std::array<double,2>> ostn15(36*63,{0,0});
    ostn15[443]={93.328,-77.086};
    ostn15[444]={93.719,-76.984};
    ostn15[480]={93.602,-76.674};
    ostn15[479]={93.206,-76.716};
    check(britishProjection.setHorizontalShiftGrid(0,0,20000,36,63,std::move(ostn15))
        && britishProjection.forward({52.139417789376,-4.571313103567},p)
        && near(p.x,224134.49586,.01) && near(p.y,252130.80956,.01),
        "British National Grid projection and OSTN15 bilinear shifts match an official control");
    check(finland.apiKeySecret == "geo.elevation.fi.nls.apiKey"
        && !coverageUrl(finland,{0,0}).toString().contains("api-key"),"Finland catalogue stores a secret reference, not a credential URL");
    const QUrlQuery nlQuery(coverageUrl(netherlands,{1226,-11337}));
    check(netherlands.epsg == 25831 && nlQuery.queryItemValue("SUBSETTINGCRS").endsWith("/25831")
        && nlQuery.queryItemValue("OUTPUTCRS").endsWith("/25831")
        && nlQuery.queryItemValue("GEOTIFF:COMPRESSION") == "None",
        "Netherlands uses explicit UTM subsetting/output and uncompressed TIFF");
    const auto &d = byId["pl.gugik.nmt1.kron86"];
    const auto &polishAscii = byId["pl.gugik.nmt1.evrf2007"];
    check(d.resolution == 1 && polishAscii.resolution == 1 && byId["cz.cuzk.dmr4g"].resolution == 5,"Polish 1 m and Czech 5 m datasets");
    check(d.blockPixels == 1024 && d.concurrentRequests == 4
        && polishAscii.blockPixels == 512 && polishAscii.concurrentRequests == 1,
        "TIFF uses four concurrent 1024 m blocks; ASCII keeps verified serial 512 m blocks");
    const auto url = coverageUrl(d,{2,3});
    const QUrlQuery query(url);
    check(query.allQueryItemValues("SUBSET").size() == 2 && query.queryItemValue("SCALESIZE") == "x(1026),y(1026)","WCS repeated subsets and fixed native-resolution dimensions");
    check(query.queryItemValue("COVERAGEID") == "DTM_PL-KRON86-NH_TIFF","numeric TIFF coverage selection");
    check(cacheRelativePath(d,{0,0}) != cacheRelativePath(polishAscii,{0,0}),"dataset-separated cache identity");
    Dataset revised = d; revised.definition["resolution"] = 5;
    check(cacheRelativePath(d,{0,0}) != cacheRelativePath(revised,{0,0}),"configuration changes invalidate cache identity");
    revised = d; revised.definition["noDataPolicy"] = "fill";
    check(cacheRelativePath(d,{0,0}) == cacheRelativePath(revised,{0,0}),"NoData policy preserves the original raw-data cache identity");
    check(d.noDataPolicy == "fallback" && netherlands.noDataPolicy == "fill",
        "NoData policy defaults to fallback and Netherlands opts into fill");
    check(blockFor(d,{d.originX+1024,d.originY-100}).column == 1,"consistent adjacent block boundary");
    check(nearDataset(d,{{52,19}}) && !nearDataset(finland,{{52,19}})
        && nearDataset(finland,{}),"location filter keeps nearby sources and leaves an unknown location unfiltered");
    Dataset local; local.epsg = 3857;
    const Geo::CrsTransform webMercator(3857);
    XY centre; webMercator.forward({60,24},centre);
    local.minX=centre.x+19000; local.maxX=centre.x+19500;
    local.minY=centre.y-100; local.maxY=centre.y+100;
    check(nearDataset(local,{{60,24}}),"10 km ground buffer accounts for Mercator scale at 60 degrees");
    local.minX=centre.x+21000; local.maxX=centre.x+22000;
    check(!nearDataset(local,{{60,24}}),"sources beyond the buffer are hidden");
    local.epsg=4326; local.minX=19; local.maxX=20; local.minY=51; local.maxY=52;
    check(nearDataset(local,{{50.9,18.9},{52.1,20.1}},0),"filter includes a dataset intersecting the tile footprint");
    Dataset oldGrid = d;
    oldGrid.definition["blockPixels"] = 512; oldGrid.definition.remove("concurrentRequests");
    check(cacheRelativePath(oldGrid,{0,0}) != cacheRelativePath(d,{0,0}),"old 512 m TIFF cache cannot be reused as 1024 m data");
    check(hgtFileName(-1,-2) == "S01W002.hgt","HGT hemisphere filename");
    QTemporaryDir temp;
    check(write(temp.path()+"/N52E019.hgt",hgt(3,10)),"create obsolete root HGT fixture");
    check(write(temp.path()+"/hgt/N52E019.hgt",hgt(3,15)),"create obsolete HGT-subdirectory fixture");
    check(findHgtFile(temp.path(),worldHgt,52,19).isEmpty(),"file source does not search legacy directories");
    check(write(temp.path()+"/world_hgt/N52E019.hgt",hgt(3,20)),"create user-managed world HGT fixture");
    check(findHgtFile(temp.path(),worldHgt,52,19) == temp.path()+"/world_hgt/N52E019.hgt",
        "catalogue directory locates user-managed HGT files");
    const QByteArray compressedHgt = QByteArray::fromBase64("H4sIAAAAAAAEAGNQZECDABCP3aMSAAAA");
    check(write(temp.path()+"/world_hgt/N53E019.hgt.gz",compressedHgt),"create compressed HGT fixture");
    Raster compressedRaster;
    check(readHgtFile(temp.path()+"/world_hgt/N53E019.hgt.gz",53,19,compressedRaster,error)
        && sampleLegacyHgt(compressedRaster,{53.5,19.5}).height == 33,
        "compressed downloaded HGT validates and samples without expansion on disk");
    auto corruptGzip = compressedHgt; corruptGzip[corruptGzip.size()-8] ^= 1;
    check(write(temp.path()+"/world_hgt/N54E019.hgt.gz",corruptGzip)
        && !readHgtFile(temp.path()+"/world_hgt/N54E019.hgt.gz",54,19,compressedRaster,error)
        && error.contains("checksum"),"compressed HGT checksum is enforced");
    std::atomic_bool cancel{false};
    check(write(temp.path()+"/world_hgt/N60E027.hgt",hgt(3,15)),"prepare Finland fallback fixture");
    auto withoutKey = generate(temp.path(),finland.id,{{60.1,27.1}},2.0,0,cancel);
    check(withoutKey.success() && withoutKey.report.downloads == 0 && withoutKey.report.fallbackSamples == 1
        && withoutKey.report.issues.join('\n').contains(finland.apiKeySecret),
        "missing API key skips requests and visibly reports the reference with HGT fallback");
    const QString invalidKey = "invalid:fixture-key";
    withoutKey = generate(temp.path(),finland.id,{{60.1,27.1}},2.0,0,cancel,{},{{finland.apiKeySecret,invalidKey}});
    check(withoutKey.success() && withoutKey.report.downloads == 0
        && !withoutKey.report.issues.join('\n').contains(invalidKey),"invalid Basic username is rejected without exposing its value");
    auto generated = generate(temp.path(),"",{{52.5,19.5}},1.0,2,cancel);
    check(generated.success() && generated.heights[0] == 22 && generated.report.primarySamples == 1,"catalogue HGT generation and offset");
    generated = generate(temp.path(),"",{{52.5,19.5},{54.5,19.5}},1.0,0,cancel);
    check(!generated.success() && generated.heights.isEmpty(),"partial missing tile never returns commit-ready heights");
    cancel = true;
    generated = generate(temp.path(),d.id,{{52,19}},1.0,0,cancel);
    check(generated.cancelled && generated.heights.isEmpty(),"cancel before network work");
    cancel = false;
    write(temp.path()+"/world_hgt/N40E019.hgt",hgt(3,12));
    generated = generate(temp.path(),d.id,{{40.5,19.5}},1.0,0,cancel);
    check(generated.success() && generated.report.fallbackSamples == 1 && generated.report.outsideSamples == 1,"outside Poland uses reported HGT fallback without HTTP");
    check(!generate("","",{{52,19}},1.0,0,cancel).success(),"empty geoPath cannot write into working directory");
    // Complete prepared block + metadata, so this exercises the production disk
    // cache path without any network service or fake projection implementation.
    const auto &ascii = polishAscii;
    cs92.forward({52,19},p);
    const auto block = blockFor(ascii,p);
    const double left = ascii.originX+block.column*512-1;
    const double bottom = ascii.originY-block.row*512+1-514;
    QByteArray cachedGrid = QString("ncols 514\nnrows 514\nxllcorner %1\nyllcorner %2\ncellsize 1\nNODATA_value -9999\n")
        .arg(left,0,'f',9).arg(bottom,0,'f',9).toLatin1();
    const QByteArray gridHeader = cachedGrid;
    for (int i = 0; i < 514*514; ++i) cachedGrid += "120 ";
    const QString cachePath = QDir(temp.path()).filePath(cacheRelativePath(ascii,block));
    const auto storeGrid = [&](const QByteArray &grid) {
        QJsonObject metadata;
        metadata["sha256"] = QString::fromLatin1(QCryptographicHash::hash(grid,QCryptographicHash::Sha256).toHex());
        return write(cachePath,grid) && write(cachePath+".json",QJsonDocument(metadata).toJson());
    };
    check(storeGrid(cachedGrid),"prepare offline cache fixture");
    generated = generate(temp.path(),ascii.id,{{52,19},{52.00005,19.00005}},1.0,3,cancel);
    check(generated.success() && generated.report.cacheHits == 1 && generated.report.downloads == 0
          && generated.report.primarySamples == 2 && generated.heights[0] == 123,"offline cached native grid generation");
    cachedGrid = gridHeader;
    for (int i = 0; i < 514*514; ++i) cachedGrid += "-9999 ";
    check(storeGrid(cachedGrid),"prepare explicit NoData cache fixture");
    generated = generate(temp.path(),ascii.id,{{52,19}},1.0,0,cancel);
    check(generated.success() && generated.report.noDataSamples == 1 && generated.report.fallbackSamples == 1
          && generated.heights[0] == 20,"cached NoData falls back to catalogue HGT with provenance");
    runCzechWcsTests(check);
    runArcGisImageServerTests(check);
    runDownloadTests(check);
    runNoDataFillTests(check);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
