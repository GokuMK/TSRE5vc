/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/fileFunctions/NetworkMessage.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ParserX.h>
#include <QTextStream>
#include <cstring>

namespace NetworkMessage {

QByteArray utf16(const QString &text) {
    QByteArray data;
    QTextStream out(&data);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << text;
    out.flush();
    return data;
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

}
