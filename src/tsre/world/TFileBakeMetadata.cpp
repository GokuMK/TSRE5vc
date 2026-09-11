#include "TFile.h"
#include <QDataStream>
#include <QBuffer>
#include <QCryptographicHash>

namespace {
void putString(QDataStream &out,const QString &value) {
    out<<quint16(value.size()); for (auto c:value) out<<c.unicode();
}
bool getString(QDataStream &in,QString &value) {
    quint16 n; in>>n;
    if (n>4096 || in.device()->bytesAvailable()<qint64(n)*2) return false;
    value.clear(); for (int i=0;i<n;++i) {quint16 c;in>>c;value+=QChar(c);}
    return in.status()==QDataStream::Ok;
}
}
QByteArray TFile::bakeMetadata() const {
    if (bakedMaterialInfo.isEmpty() && seasonalBakes.isEmpty()) return {};
    QByteArray bytes; QDataStream out(&bytes,QIODevice::WriteOnly);out.setByteOrder(QDataStream::LittleEndian);
    out<<quint8(0)<<quint32(2)<<materialContentRevision;
    for (auto it=seasonalBakes.cbegin();it!=seasonalBakes.cend();++it) {
        QByteArray entry; QDataStream record(&entry,QIODevice::WriteOnly);record.setByteOrder(QDataStream::LittleEndian);
        record<<quint8(0);putString(record,it.key());record<<it->revision<<it->resolution;
        putString(record,it->settings);putString(record,it->sources);putString(record,it->validation);
        out<<quint32(TS::TSRETerrainBakedMaterial)<<quint32(entry.size());out.writeRawData(entry.constData(),entry.size());
    }
    return bytes;
}
bool TFile::readBakeMetadata(const QByteArray &bytes) {
    bakedMaterialsValid=false; seasonalBakes.clear();bakedMaterialInfo=":invalid bake metadata:";
    QDataStream in(bytes);in.setByteOrder(QDataStream::LittleEndian);
    quint8 label=0;quint32 version=0;quint64 revision=0;
    in>>label;
    if (bytes.size()<13+label*2) return false;
    in.skipRawData(label*2);in>>version>>revision;
    if (version!=2) return false;
    QMap<QString,BakeRecord> records;
    int count=0;
    while (!in.atEnd()) {
        if (++count>64 || in.device()->bytesAvailable()<8) return false;
        quint32 token,length;in>>token>>length;
        if (length>quint64(in.device()->bytesAvailable()) || length>65536) return false;
        QByteArray payload(int(length),Qt::Uninitialized);in.readRawData(payload.data(),int(length));
        if(token!=TS::TSRETerrainBakedMaterial) continue;
        QDataStream r(payload);r.setByteOrder(QDataStream::LittleEndian);
        r>>label;if (length<quint32(1+label*2)) return false;r.skipRawData(label*2);
        QString variant;BakeRecord b;
        if (!getString(r,variant) || variant.isEmpty() || records.contains(variant)) return false;
        r>>b.revision>>b.resolution;
        if (!getString(r,b.settings)||!getString(r,b.sources)||!getString(r,b.validation)
                || r.status()!=QDataStream::Ok || !r.atEnd() || b.resolution<1 || b.resolution>16384) return false;
        records.insert(variant,b);
    }
    materialContentRevision=revision;seasonalBakes=records;bakedMaterialsValid=true;
    bakedMaterialInfo="v1:pending";selectBakeVariant("Base");return true;
}
void TFile::selectBakeVariant(const QString &variant) {
    if (!bakedMaterialsValid) return;
    if (bakedMaterialInfo.isEmpty() && seasonalBakes.isEmpty()) return;
    const auto it=seasonalBakes.constFind(variant);
    if (it==seasonalBakes.cend()) {bakedMaterialInfo="v1:pending";return;}
    const QString inputs=QString::fromLatin1(QCryptographicHash::hash((it->settings+it->sources).toUtf8(),QCryptographicHash::Sha256).toHex());
    bakedMaterialInfo=it->validation.isEmpty()
            ? "v1:unchecked:"+inputs+":"+variant+"-"+QString::number(it->revision)+"-"+it->sources
            : "v1:checked:"+inputs+":"+it->validation;
}
