#include <tsre/world/ScopedBakeTFile.h>
#include "ScopedTerrainFileLegacy.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QLoggingCategory>
#include <algorithm>
#include <cstring>
#include <memory>
#include <tsre/world/TerrainFileData.h>
#include <mzip/miniz/miniz.h>

template<class File> static QByteArray encode(File &file) {
    QByteArray bytes;
    QDataStream out(&bytes,QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    file.save(out);
    return out.status()==QDataStream::Ok ? bytes : QByteArray();
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    QLoggingCategory::setFilterRules("*.debug=false"); // Exclude legacy filename logging from file-I/O timings.
    const bool typed=app.arguments().contains("--typed");
    const bool runtime=app.arguments().contains("--runtime");
    const bool io=app.arguments().contains("--io");
    QTemporaryDir temporary;
    QJsonArray results;
    for (const int p : {4,8,16,32}) {
        ScopedTerrainFileLegacy fixture;
        fixture.initNew("benchmark",256,8,p);
        const auto bytes=encode(fixture);
        if (bytes.isEmpty()) return 1;
        const auto path=temporary.filePath("fixture.t");
        if(io) {
            QByteArray compressed("SIMISA@F",8);
            QDataStream header(&compressed,QIODevice::Append);header.setByteOrder(QDataStream::LittleEndian);
            header<<quint32(bytes.size()-16);header.writeRawData("@@@@",4);
            compressed+=qCompress(bytes.mid(16),6).mid(4);
            QFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(compressed)!=compressed.size())return 5;
        }
        std::vector<double> parse,write;
        for (int iteration=-3;iteration<31;++iteration) {
            auto storage=new unsigned char[bytes.size()];
            std::memcpy(storage,bytes.constData(),bytes.size());
            FileBuffer input(storage,bytes.size());
            ScopedBakeTFile decoded;
            ScopedTerrainFileLegacy legacy;
            TerrainFile::Data typedData;QString error;
            QElapsedTimer timer;timer.start();
            const bool loaded=io
                ? (typed?typedData.readFile(path,error):runtime?decoded.readT(path):legacy.readT(path))
                : (typed?typedData.read(input,error):runtime?decoded.load(&input):legacy.load(&input));
            if(!loaded)return 2;
            const auto readNs=timer.nsecsElapsed();
            timer.restart();
            const auto output=typed?typedData.encode(error):runtime?encode(decoded):encode(legacy);
            const auto writeNs=timer.nsecsElapsed();
            if(output!=bytes)return 3;
            if(iteration>=0) {parse.push_back(readNs/1e6);write.push_back(writeNs/1e6);}
        }
        std::sort(parse.begin(),parse.end());std::sort(write.begin(),write.end());
        results.append(QJsonObject{{"patches",p},{"bytes",bytes.size()},
            {"parse_median_ms",parse[15]},{"parse_p95_ms",parse[29]},
            {"write_median_ms",write[15]},{"write_p95_ms",write[29]}});
    }
    QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 4;
    output.write(QJsonDocument(results).toJson());
}
