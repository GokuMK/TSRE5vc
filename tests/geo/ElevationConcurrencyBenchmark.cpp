// Live comparison of equal-area TIFF retrieval strategies, with bounded concurrency.
#include <tsre/geo/ElevationSource.h>
#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>

int runConcurrencyBenchmark(const QString &outputPath, bool freshConnections) {
    QString error;
    Elevation::Dataset dataset;
    for (const auto &d : Elevation::builtInDatasets(error)) if (d.id == "pl.gugik.nmt1.kron86") dataset = d;
    if (dataset.id.isEmpty()) { std::cerr << error.toStdString(); return 2; }
    Elevation::XY point;
    const Geo::CrsTransform projection(dataset.epsg);
    projection.forward({52,19},point);
    dataset.blockPixels = 2048;
    const auto parent = Elevation::blockFor(dataset,point);
    QJsonObject document{{"startedUtc",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"connectionPolicy",freshConnections ? "clear idle connections before each group" : "reuse connections across groups"},
        {"dataset",dataset.id},{"latitude",52.0},{"longitude",19.0},
        {"description","Equal 2048 m core: four sequential 1024 m requests, four simultaneous 1024 m requests, one 2048 m request; no local cache"},
        {"transferTimeoutMs",30000},{"requestDeadlineMs",45000},{"responseCapBytes",32*1024*1024}};
    QJsonArray results;
    QNetworkAccessManager network;
    const char *names[] = {"four-sequential-1024","four-parallel-1024","one-2048"};
    const int orders[3][3] = {{0,1,2},{1,2,0},{2,0,1}};
    QByteArray referenceHash;
    int failures = 0;
    for (int round=0; round<3; ++round) for (int mode : orders[round]) {
        if (freshConnections) network.clearConnectionCache();
        const int count = mode==2 ? 1 : 4, concurrency = mode==1 ? 4 : 1;
        const int core = mode==2 ? 2048 : 1024;
        dataset.blockPixels = core;
        struct Request {
            Elevation::Block block;
            QByteArray bytes;
            QJsonObject result;
            qint64 started = 0, firstByte = -1;
            bool tooLarge = false, deadline = false, transport = false;
        };
        std::array<Request,4> requests;
        QElapsedTimer wall;
        QEventLoop loop;
        int next=0, completed=0, outstanding=0, peak=0;
        wall.start();
        std::function<void()> launch;
        launch = [&] {
            const int index = next++;
            auto &r = requests[index];
            r.block = mode==2 ? parent : Elevation::Block{parent.column*2+index%2,parent.row*2+index/2};
            const auto url = Elevation::coverageUrl(dataset,r.block);
            QNetworkRequest request(url);
            request.setRawHeader("User-Agent","TSRE5vc terrain elevation concurrency benchmark");
            request.setTransferTimeout(30000);
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
            r.started = wall.elapsed();
            auto *reply = network.get(request);
            reply->setReadBufferSize(1024*1024);
            auto *deadline = new QTimer(reply);
            deadline->setSingleShot(true);
            QObject::connect(deadline,&QTimer::timeout,reply,[&,index,reply] {
                requests[index].deadline = true; reply->abort();
            });
            QObject::connect(reply,&QNetworkReply::readyRead,&loop,[&,index,reply] {
                auto &item = requests[index];
                if (item.firstByte < 0) item.firstByte = wall.elapsed();
                item.bytes += reply->readAll();
                if (item.bytes.size()>32*1024*1024) { item.tooLarge = true; reply->abort(); }
            });
            QObject::connect(reply,&QNetworkReply::finished,&loop,[&,index,reply,deadline,url] {
                deadline->stop();
                auto &item = requests[index];
                item.bytes += reply->readAll();
                const qint64 finished = wall.elapsed();
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                item.transport = !item.tooLarge && !item.deadline
                    && reply->error()==QNetworkReply::NoError && status==200;
                item.result = QJsonObject{{"index",index},{"url",url.toString()},
                    {"startedAtMs",double(item.started)},{"firstByteAtMs",double(item.firstByte)},
                    {"finishedAtMs",double(finished)},{"transferMs",double(finished-item.started)},
                    {"bytes",double(item.bytes.size())},{"httpStatus",status},
                    {"http2Used",reply->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool()},
                    {"deadlineExceeded",item.deadline},{"sizeLimitExceeded",item.tooLarge}};
                if (!item.transport) item.result["networkError"] = reply->errorString();
                reply->deleteLater();
                --outstanding; ++completed;
                if (next<count) launch();
                if (completed==count) loop.quit();
            });
            ++outstanding; peak = std::max(peak,outstanding);
            deadline->start(45000);
        };
        for (int i=0; i<concurrency; ++i) launch();
        if (completed<count) loop.exec();
        const auto networkMs = wall.elapsed();
        std::array<Elevation::Raster,4> rasters;
        bool valid = true;
        qint64 totalBytes = 0;
        QJsonArray details;
        for (int i=0; i<count; ++i) {
            auto &r = requests[i]; auto &raster = rasters[i];
            const bool decoded = r.transport && Elevation::readGeoTiff(r.bytes,raster,error);
            const double left=dataset.originX+double(r.block.column)*core-1;
            const double top=dataset.originY-double(r.block.row)*core+1;
            const bool grid = decoded && raster.width==core+2 && raster.height==core+2
                && raster.epsg==2180 && std::abs(raster.transform[0]-left)<1e-5
                && std::abs(raster.transform[3]-top)<1e-5
                && raster.transform[1]==1 && raster.transform[5]==-1
                && raster.transform[2]==0 && raster.transform[4]==0;
            r.result["validNativeGrid"] = grid;
            if (r.transport && !decoded) r.result["decodeError"] = error;
            valid &= grid; totalBytes += r.bytes.size(); details.append(r.result);
        }
        QByteArray hash;
        if (valid) {
            // Same row-major 2048 x 2048 core, removing each response's margin.
            QCryptographicHash digest(QCryptographicHash::Sha256);
            for (int y=0; y<2048; ++y) for (int x=0; x<2048; x+=core) {
                const auto &raster = rasters[mode==2 ? 0 : (y/core)*2+x/core];
                const float *row = raster.values.constData()+(y%core+1)*raster.width+1;
                digest.addData(QByteArrayView(reinterpret_cast<const char*>(row),core*sizeof(float)));
            }
            hash = digest.result().toHex();
            if (referenceHash.isEmpty()) referenceHash = hash;
            valid = hash==referenceHash;
        }
        QJsonObject row{{"round",round+1},{"mode",QString::fromLatin1(names[mode])},
            {"networkWallMs",double(networkMs)},{"decodeAndValidateMs",double(wall.elapsed()-networkMs)},
            {"totalPayloadBytes",double(totalBytes)},{"peakOutstandingRequests",peak},
            {"validNativeGridsAndMatchingCore",valid},{"coreSha256",QString::fromLatin1(hash)},
            {"requests",details}};
        if (!valid) ++failures;
        results.append(row); document["results"] = results;
        QFile output(outputPath);
        if (!output.open(QIODevice::WriteOnly|QIODevice::Truncate)
                || output.write(QJsonDocument(document).toJson())<0) return 2;
        output.close();
        std::cout << "round=" << round+1 << " " << names[mode] << " network=" << networkMs/1000.0
            << "s MiB=" << totalBytes/1048576.0 << " valid=" << valid << std::endl;
    }
    return failures ? 1 : 0;
}
