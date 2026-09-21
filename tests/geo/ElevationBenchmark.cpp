// Opt-in, sequential live WCS probe. Does not change the application cache.
#include <tsre/geo/ElevationSource.h>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <cmath>
#include <iostream>

int runConcurrencyBenchmark(const QString &outputPath, bool freshConnections);
int main(int argc, char **argv) {
    QCoreApplication app(argc,argv);
    const auto args = app.arguments();
    if (args.size() == 3 && (args[1] == "--concurrency" || args[1] == "--concurrency-fresh"))
        return runConcurrencyBenchmark(args[2],args[1] == "--concurrency-fresh");
    if (args.size() != 3 || args[1] != "--live") {
        std::cerr << "Usage: tsre_elevation_benchmark --live|--concurrency|--concurrency-fresh results.json\n";
        return 2;
    }
    QString error;
    auto catalog = Elevation::datasets(error);
    catalog.removeIf([](const Elevation::Dataset &d) { return !d.id.startsWith("pl.gugik."); });
    if (catalog.size() != 2) { std::cerr << error.toStdString(); return 2; }
    QJsonObject document{{"startedUtc",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"latitude",52.0},{"longitude",19.0},
        {"description","Three sequential rounds, rotated size order, identical nested native-grid bounds; no local cache"}};
    QJsonArray results;
    QNetworkAccessManager network;
    const int orders[3][3] = {{512,1024,2048},{2048,512,1024},{1024,2048,512}};
    int failures = 0;
    for (int round = 0; round < 3; ++round) for (const int size : orders[round]) {
        for (int source = 0; source < catalog.size(); ++source) {
            // Alternate format order between rounds to reduce ordering effects.
            auto dataset = catalog[(source+round)%catalog.size()];
            Elevation::XY point;
            const Geo::CrsTransform projection(dataset.epsg);
            projection.forward({52.0,19.0},point);
            dataset.blockPixels = 2048;
            auto block = Elevation::blockFor(dataset,point);
            block.column *= 2048/size; block.row *= 2048/size;
            dataset.blockPixels = size;
            const QUrl url = Elevation::coverageUrl(dataset,block);
            QNetworkRequest request(url);
            request.setRawHeader("User-Agent","TSRE5vc terrain elevation size benchmark");
            request.setTransferTimeout(90000);
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
            QElapsedTimer clock;
            clock.start();
            auto *reply = network.get(request);
            reply->setReadBufferSize(1024*1024);
            QByteArray bytes;
            qint64 firstByte = -1;
            bool capped = false, timedOut = false;
            QEventLoop loop;
            QTimer deadline;
            QObject::connect(reply,&QNetworkReply::readyRead,&loop,[&] {
                if (firstByte < 0) firstByte = clock.elapsed();
                bytes += reply->readAll();
                if (bytes.size() > 128*1024*1024) { capped = true; reply->abort(); }
            });
            QObject::connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit);
            QObject::connect(&deadline,&QTimer::timeout,&loop,[&] { timedOut = true; reply->abort(); });
            deadline.setSingleShot(true); deadline.start(120000);
            if (!reply->isFinished()) loop.exec();
            const qint64 transferMs = clock.elapsed();
            bytes += reply->readAll();
            QJsonObject row{{"round",round+1},{"dataset",dataset.id},{"coreSizeMetres",size},
                {"url",url.toString()},{"httpStatus",reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()},
                {"bytes",double(bytes.size())},{"firstByteMs",double(firstByte)},{"transferMs",double(transferMs)},
                {"contentType",QString::fromLatin1(reply->rawHeader("Content-Type"))},
                {"contentEncoding",QString::fromLatin1(reply->rawHeader("Content-Encoding"))},
                {"serverAge",QString::fromLatin1(reply->rawHeader("Age"))},
                {"benchmarkDeadlineExceeded",timedOut},{"benchmarkSizeLimitExceeded",capped},
                {"exceedsApplication32MiBLimit",bytes.size()>32*1024*1024},
                {"exceedsApplication45sDeadline",transferMs>45000}};
            const bool transport = !capped && !timedOut && reply->error()==QNetworkReply::NoError
                && row["httpStatus"].toInt()==200;
            if (!transport) row["networkError"] = reply->errorString();
            delete reply;
            Elevation::Raster raster;
            clock.restart();
            const bool decoded = transport && (dataset.format=="image/tiff"
                ? Elevation::readGeoTiff(bytes,raster,error)
                : Elevation::readAsciiGrid(bytes,dataset.epsg,raster,error));
            row["decodeMs"] = double(clock.elapsed());
            const double left=dataset.originX+double(block.column)*size-1;
            const double top=dataset.originY-double(block.row)*size+1;
            bool valid = decoded && raster.width==size+2 && raster.height==size+2
                && raster.epsg==2180 && std::abs(raster.transform[0]-left)<1e-5
                && std::abs(raster.transform[3]-top)<1e-5
                && raster.transform[1]==1 && raster.transform[5]==-1
                && raster.transform[2]==0 && raster.transform[4]==0;
            row["validNativeGrid"] = valid;
            if (decoded) {
                row["width"]=raster.width; row["height"]=raster.height;
                row["transform"]=QJsonArray{raster.transform[0],raster.transform[1],raster.transform[2],
                    raster.transform[3],raster.transform[4],raster.transform[5]};
                if (raster.width > 500 && raster.height > 500) {
                    QJsonArray samples;
                    for (int i : {10,100,250,500}) samples.append(raster.values[i*raster.width+i]);
                    row["sharedPixelHeights"]=samples;
                }
            } else row["decodeError"]=error;
            if (!valid) ++failures;
            results.append(row); document["results"]=results;
            QFile output(args[2]);
            if (!output.open(QIODevice::WriteOnly|QIODevice::Truncate)
                    || output.write(QJsonDocument(document).toJson())<0) return 2;
            output.close();
            std::cout << "round=" << round+1 << " " << dataset.id.toStdString()
                << " size=" << size << " HTTP=" << row["httpStatus"].toInt()
                << " MiB=" << bytes.size()/1048576.0 << " first=" << firstByte/1000.0
                << "s total=" << transferMs/1000.0 << "s decode=" << row["decodeMs"].toDouble()
                << "ms valid=" << valid << std::endl;
        }
    }
    return failures ? 1 : 0;
}
