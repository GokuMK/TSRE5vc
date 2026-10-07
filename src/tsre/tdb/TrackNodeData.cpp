#include <tsre/tdb/TrackNodeData.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ParserX.h>
#include <QTextStream>
#include <cmath>
#include <limits>

namespace {
QString integerToken(FileBuffer *data, bool optional) {
    // Like the positional legacy number reader, skip token names and opening
    // delimiters (notably TrPin/TrItemRef). Bound the scan to this buffer.
    while(data->off + 2 <= data->readEnd()) {
        const auto c = data->getShort();
        if(optional && c == ')') { data->off -= 2; return {}; }
        if((c >= '0' && c <= '9') || c == '-' || c == '+') {
            data->off -= 2;
            QString token;
            while(data->off + 2 <= data->readEnd()) {
                const auto next = data->getShort();
                if(next <= ' ' || next == '(' || next == ')') {
                    data->off -= 2;
                    break;
                }
                token += QChar(next);
            }
            return token;
        }
    }
    throw FileBuffer::ParseError("Missing track-node integer");
}

template<class T> T readInteger(FileBuffer *data, bool optional = false) {
    const QString token = integerToken(data, optional);
    if(optional && token.isEmpty()) return 0;
    bool ok = false;
    // All int32/uint32 values are exact in double, including exponent syntax.
    const double value = token.toDouble(&ok);
    if(!ok || !std::isfinite(value) || std::trunc(value) != value
            || value < std::numeric_limits<T>::lowest()
            || value > std::numeric_limits<T>::max())
        throw FileBuffer::ParseError("Invalid track-node integer");
    return static_cast<T>(value);
}

std::uint8_t readByte(FileBuffer *data) {
    QString token;
    while(data->off + 2 <= data->readEnd()) {
        const auto c = data->getShort();
        if(c <= ' ') {
            if(!token.isEmpty()) break;
        } else if(c == '(' || c == ')') {
            data->off -= 2;
            break;
        } else {
            token += QChar(c);
        }
    }
    bool ok = false;
    const auto value = token.toUInt(&ok, 16);
    if(!ok || token.size() != 2 || value > 0xff)
        throw FileBuffer::ParseError("Invalid track-section hexadecimal byte");
    return static_cast<std::uint8_t>(value);
}
}

std::int32_t TrackNodeText::readInt(FileBuffer *data) { return readInteger<std::int32_t>(data); }
std::uint32_t TrackNodeText::readUInt(FileBuffer *data, bool optional) {
    return readInteger<std::uint32_t>(data, optional);
}

void TrackNodeUid::load(FileBuffer *data) {
    worldTileX = TrackNodeText::readInt(data);
    worldTileZ = TrackNodeText::readInt(data);
    worldObjectId = TrackNodeText::readUInt(data);
    worldEndpointIndex = TrackNodeText::readUInt(data);
    tileX = TrackNodeText::readInt(data);
    tileZ = TrackNodeText::readInt(data);
    x = ParserX::GetNumber(data); y = ParserX::GetNumber(data); z = ParserX::GetNumber(data);
    ax = ParserX::GetNumber(data); ay = ParserX::GetNumber(data); az = ParserX::GetNumber(data);
}

void TrackNodeUid::save(QTextStream &out) const {
    out << worldTileX << " " << worldTileZ << " " << worldObjectId << " " << worldEndpointIndex
        << " " << tileX << " " << tileZ << " " << x << " " << y << " " << z
        << " " << ax << " " << ay << " " << az << " ";
}

void TrackVectorSection::load(FileBuffer *data) {
    sectionIndex = TrackNodeText::readUInt(data);
    shapeIndex = TrackNodeText::readUInt(data);
    worldTileX = TrackNodeText::readInt(data);
    worldTileZ = TrackNodeText::readInt(data);
    worldObjectId = TrackNodeText::readUInt(data);
    startEndpointIndex = TrackNodeText::readUInt(data);
    endEndpointIndex = TrackNodeText::readUInt(data);
    opaqueByte = readByte(data);
    tileX = TrackNodeText::readInt(data);
    tileZ = TrackNodeText::readInt(data);
    x = ParserX::GetNumber(data); y = ParserX::GetNumber(data); z = ParserX::GetNumber(data);
    ax = ParserX::GetNumber(data); ay = ParserX::GetNumber(data); az = ParserX::GetNumber(data);
}

void TrackVectorSection::save(QTextStream &out) const {
    out << " " << sectionIndex << " " << shapeIndex << " " << worldTileX << " " << worldTileZ
        << " " << worldObjectId << " " << startEndpointIndex << " " << endEndpointIndex
        << " " << QString::number(opaqueByte, 16).rightJustified(2, '0')
        << " " << tileX << " " << tileZ << " " << x << " " << y << " " << z
        << " " << ax << " " << ay << " " << az;
}
