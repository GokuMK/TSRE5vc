/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/fileFunctions/NetworkMessage.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ParserX.h>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QTextStream>
#include <QtEndian>
#include <cstring>

namespace NetworkMessage {

namespace {
const char Mark[4] = {'T', 'S', 'R', 'N'};

void put16(QByteArray &out, int at, quint16 value) {
    qToLittleEndian(value, out.data() + at);
}
void put32(QByteArray &out, int at, quint32 value) {
    qToLittleEndian(value, out.data() + at);
}
quint16 get16(const QByteArray &in, int at) {
    return qFromLittleEndian<quint16>(in.constData() + at);
}
quint32 get32(const QByteArray &in, int at) {
    return qFromLittleEndian<quint32>(in.constData() + at);
}
}

QByteArray utf16(const QString &text) {
    QByteArray data;
    QTextStream out(&data);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << text;
    out.flush();
    return data;
}

QByteArray frame(const QByteArray &payload, quint32 request, quint32 change, int compressAbove) {
    const bool text = payload.size() >= 2 && uchar(payload[0]) == 0xFF && uchar(payload[1]) == 0xFE;
    const bool compress = payload.size() > compressAbove;
    const QByteArray body = compress ? qCompress(payload, 6) : payload;
    QByteArray out(HeaderSize, char(0));
    std::memcpy(out.data(), Mark, 4);
    put16(out, 4, Protocol);
    out[6] = char(text ? Text : Binary);
    out[7] = char(compress ? Compressed : 0);
    put32(out, 8, request);
    put32(out, 12, change);
    put32(out, 16, quint32(body.size()));
    out.append(body);
    return out;
}

QByteArray textFrame(const QString &text, quint32 request, quint32 change) {
    return frame(utf16(text), request, change);
}

bool frameProtocol(const QByteArray &message, quint16 &protocol) {
    if (message.size() < 6 || std::memcmp(message.constData(), Mark, 4) != 0)
        return false;
    protocol = get16(message, 4);
    return true;
}

Read readFrame(const QByteArray &message, qint64 limit, Frame &out, QString *error) {
    auto fail = [&](Read result, const QString &why) {
        if (error != nullptr)
            *error = why;
        return result;
    };
    quint16 protocol = 0;
    if (!frameProtocol(message, protocol))
        return fail(Read::NotFrame, "not a TSRE frame (an older TSRE?)");
    out.protocol = protocol;
    if (protocol != Protocol)
        return fail(Read::OtherProtocol, QString("protocol %1 here, %2 there").arg(Protocol).arg(protocol));
    if (message.size() < HeaderSize)
        return fail(Read::Malformed, "a frame shorter than its header");
    out.kind = quint8(message[6]);
    out.flags = quint8(message[7]);
    out.request = get32(message, 8);
    out.change = get32(message, 12);
    const quint32 length = get32(message, 16);
    if (out.kind != Text && out.kind != Binary)
        return fail(Read::Malformed, QString("a frame of kind %1").arg(out.kind));
    if (qint64(length) != qint64(message.size()) - HeaderSize)
        return fail(Read::Malformed, QString("a payload of %1 bytes, %2 said").arg(message.size() - HeaderSize).arg(length));
    if (out.flags & Compressed) {
        // qCompress's own header: the uncompressed size, big endian. Checked
        // before anything is uncompressed.
        if (length < 4)
            return fail(Read::Malformed, "a compressed payload without its size");
        const quint32 size = qFromBigEndian<quint32>(message.constData() + HeaderSize);
        if (qint64(size) > limit)
            return fail(Read::TooLarge, QString("a payload of %1 bytes, at most %2").arg(size).arg(limit));
        out.payload = qUncompress(reinterpret_cast<const uchar *>(message.constData() + HeaderSize), int(length));
        if (out.payload.size() != int(size))
            return fail(Read::Malformed, "a compressed payload that does not uncompress");
    } else {
        if (qint64(length) > limit)
            return fail(Read::TooLarge, QString("a payload of %1 bytes, at most %2").arg(length).arg(limit));
        out.payload = message.mid(HeaderSize);
    }
    if (out.kind == Text && (out.payload.size() < 2 || uchar(out.payload[0]) != 0xFF || uchar(out.payload[1]) != 0xFE))
        return fail(Read::Malformed, "a text payload without its byte order mark");
    if (out.kind == Binary && (out.payload.isEmpty() || out.payload[0] != 'B'))
        return fail(Read::Malformed, "a binary payload without its mark");
    return Read::Ok;
}

QString loginAnswer(const QString &password, const QByteArray &nonce, const QString &user) {
    QMessageAuthenticationCode code(QCryptographicHash::Sha256, password.toUtf8());
    code.addData(nonce);
    code.addData(user.toUtf8());
    return QString::fromLatin1(code.result().toHex());
}

QByteArray newNonce() {
    QByteArray nonce(32, char(0));
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(nonce.data()), nonce.size() / 4);
    return nonce;
}

FileBuffer *buffer(const QByteArray &message) {
    unsigned char *bytes = new unsigned char[message.size() > 0 ? message.size() : 1];
    std::memcpy(bytes, message.constData(), size_t(message.size()));
    return new FileBuffer(bytes, int(message.size()));
}

void skipRecord(FileBuffer *data) {
    ParserX::SkipToken(data);
    if (data->length - data->off == 2 && data->data[data->off] == ')' && data->data[data->off + 1] == 0)
        data->off += 2;
}

FileBuffer *part(const FileBuffer *data, int start, int end) {
    if (start < 0 || end > data->length || end < start)
        start = end = 0;
    const int size = end - start;
    unsigned char *bytes = new unsigned char[size > 0 ? size : 1];
    if (size > 0)
        std::memcpy(bytes, data->data + start, size_t(size));
    return new FileBuffer(bytes, size);
}

QByteArray record(const FileBuffer *data, int start, int end) {
    QByteArray out;
    if (start < 0 || end > data->length || end <= start)
        return out;
    // The byte order mark of UTF-16 little endian, as the messages are read.
    out.append(char(0xFF));
    out.append(char(0xFE));
    out.append(reinterpret_cast<const char *>(data->data + start), end - start);
    out.append("\n\0", 2);
    return out;
}

QString cleanUsername(const QString &name) {
    QString out;
    for (const QChar c : name)
        if (c.isLetterOrNumber())
            out += c;
    out.truncate(12);
    if (out.isEmpty())
        out = QStringLiteral("user");
    return out;
}

QString quoted(QString text) {
    text.replace('"', '\'');
    return text;
}

namespace {
const char *const IdKindNames[IdKindCount] = {"tdb_node", "tdb_item", "rdb_node", "rdb_item",
                                              "section", "shape", "uid", "uid_sound"};
}

QString idKindName(IdKind kind) {
    return QString::fromLatin1(IdKindNames[int(kind)]);
}

bool idKindFromName(const QString &name, IdKind &kind) {
    for (int i = 0; i < IdKindCount; i++)
        if (name == QLatin1String(IdKindNames[i])) {
            kind = IdKind(i);
            return true;
        }
    return false;
}

bool idKindPerTile(IdKind kind) {
    return kind == IdKind::Uid || kind == IdKind::UidSound;
}

QString idKey(IdKind kind, int tileX, int tileZ) {
    if (idKindPerTile(kind))
        return idKindName(kind) + " " + QString::number(tileX) + " " + QString::number(tileZ);
    return idKindName(kind);
}

int idBlockSize(IdKind kind) {
    return kind == IdKind::TdbItem || kind == IdKind::RdbItem ? 32 : 256;
}

}
