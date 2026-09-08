#ifndef TSRE_TOKEN_TEST_SUPPORT_H
#define TSRE_TOKEN_TEST_SUPPORT_H

#include <tsre/fileFunctions/FileBuffer.h>
#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QDebug>
#include <QtEndian>
#include <cstring>
#include <functional>
#include <memory>

namespace TokenTest {
inline QByteArray fields(const std::function<void(QDataStream&)>& write) {
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    write(stream);
    return result;
}
inline QByteArray uints(std::initializer_list<quint32> values) {
    return fields([&](QDataStream& s) { for (auto v : values) s << v; });
}
inline QByteArray floats(std::initializer_list<float> values) {
    return fields([&](QDataStream& s) { for (auto v : values) s << v; });
}
inline QByteArray string(const QString& value) {
    return fields([&](QDataStream& s) {
        s << quint16(value.size());
        for (QChar c : value) s << c.unicode();
    });
}
inline QByteArray block(TS::TokenId id, const QByteArray& payload = {}, const QString& label = {}) {
    return fields([&](QDataStream& s) {
        s << quint32(id) << quint32(1 + 2 * label.size() + payload.size()) << quint8(label.size());
        for (QChar c : label) s << c.unicode();
        s.writeRawData(payload.constData(), payload.size());
    });
}
inline QByteArray file(const QByteArray& root, char kind = 't') {
    QByteArray header("SIMISA@@@@@@@@@@JINX0t6b______\r\n", 32);
    header[21] = kind;
    return header + root;
}
inline QByteArray compressed(const QByteArray& plain) {
    const QByteArray payload = plain.mid(16);
    return QByteArray("SIMISA@F", 8) + uints({quint32(payload.size())})
            + QByteArray("@@@@", 4) + qCompress(payload, 6).mid(4);
}
inline std::unique_ptr<FileBuffer> buffer(const QByteArray& bytes) {
    auto data = new unsigned char[bytes.size()];
    std::memcpy(data, bytes.constData(), bytes.size());
    return std::unique_ptr<FileBuffer>(new FileBuffer(data, bytes.size()));
}
inline bool rejects(const std::function<void()>& action) {
    try { action(); } catch (const FileBuffer::ParseError&) { return true; }
    return false;
}
struct Suite {
    const char* name;
    bool verbose;
    int passed = 0, failed = 0;
    void check(bool ok, const QString& label) {
        if (ok) ++passed; else ++failed;
        if (verbose || !ok) qInfo().noquote() << name << (ok ? "PASS" : "FAIL") << label;
    }
    int finish() {
        qInfo() << name << "passed" << passed << "failed" << failed;
        return failed ? 1 : 0;
    }
};
} // namespace TokenTest
#endif
