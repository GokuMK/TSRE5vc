#include <tsre/tests/TerrainFileTestSuite.h>
#include <QCoreApplication>
#include <tsre/world/TerrainFileData.h>
#include <tsre/world/TFile.h>
#include <QDirIterator>
#include <QDebug>
#include <QFile>
#include <QtEndian>
#include <cstring>
#include <mzip/miniz/miniz.h>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const int scan=app.arguments().indexOf("--scan");
    if(scan>=0) {
        if(scan+1>=app.arguments().size())return 2;
        int passed=0,failed=0,paired=0,flat=0,exact=0,runtimePassed=0;
        QDirIterator files(app.arguments()[scan+1],{"*.t"},QDir::Files,QDirIterator::Subdirectories);
        while(files.hasNext()) {
            const auto path=files.next();TerrainFile::Data data;QString error;
            bool ok=data.readFile(path,error);
            if(ok) {
                data.paired?++paired:++flat;
                const auto bytes=data.encode(error);
                if(bytes.isEmpty())ok=false;
                else {
                    QFile originalFile(path);
                    if(!originalFile.open(QIODevice::ReadOnly))return 2;
                    auto original=originalFile.readAll();
                    if(original.startsWith("SIMISA@F")&&original.size()>=20) {
                        const quint32 expected=qFromLittleEndian<quint32>(original.constData()+8);
                        if(expected>TerrainFile::Data::MaximumBytes-16)return 2;
                        QByteArray inflated(expected,Qt::Uninitialized);mz_ulong size=expected;
                        if(mz_uncompress(reinterpret_cast<unsigned char*>(inflated.data()),&size,
                            reinterpret_cast<const unsigned char*>(original.constData()+16),original.size()-16)!=MZ_OK
                            ||size!=expected)return 2;
                        original=QByteArray("SIMISA@@@@@@@@@@",16)+inflated;
                    }
                    // The codec intentionally emits a canonical 32-byte header.
                    // Compare every framed descriptor byte, not only writer idempotence.
                    if(original.mid(32)==bytes.mid(32))++exact;
                    else {ok=false;error="Descriptor payload changed on no-op round trip";}
                    auto memory=new unsigned char[bytes.size()];std::memcpy(memory,bytes.constData(),bytes.size());
                    FileBuffer input(memory,bytes.size());TerrainFile::Data reloaded;
                    if(ok)ok=reloaded.read(input,error)&&reloaded.encode(error)==bytes;
                }
            }
            if(ok) {
                TFile runtime;TFile::LayoutInfo info;
                ok=runtime.readT(path)&&runtime.validateRuntime(error)&&runtime.preflight(error)
                    &&TFile::readLayoutInfo(path,info)&&info.samples==int(*runtime.samples.count)
                    &&info.spacing==*runtime.samples.spacing&&info.patches==runtime.patchCount();
                if(ok)++runtimePassed;
            }
            if(ok)++passed;else {++failed;qWarning()<<path<<error;}
        }
        qInfo()<<"[terrain-tfile:scan] passed"<<passed<<"failed"<<failed<<"paired"<<paired<<"flat"<<flat
               <<"byte-exact payloads"<<exact<<"runtime/preflight/layout"<<runtimePassed;
        return failed||!passed?1:0;
    }
    return TsreTests::runTerrainFileSuite(app.arguments().contains("--verbose"));
}
