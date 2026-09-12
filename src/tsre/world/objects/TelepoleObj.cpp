/* This file is part of TSRE5. Licensed under GPL-3.0-or-later; see LICENSE.md. */
#include <tsre/world/objects/TelepoleObj.h>
#include <QTextStream>
#include <algorithm>
#include <limits>

namespace {
constexpr TS::TokenId fields[] = {
    TS::UiD, TS::Population, TS::StartPosition, TS::EndPosition,
    TS::StartType, TS::EndType, TS::StartDirection, TS::EndDirection,
    TS::Config, TS::Quality, TS::Position, TS::Direction, TS::MaxVisDistance, TS::VDbId
};

QString number(float value) {
    // Independent of the surrounding stream's precision/locale; round-trip finite floats.
    return QString::number(value, 'g', std::numeric_limits<float>::max_digits10);
}
QString number(unsigned int value) { return QString::number(value); }
QString number(const std::array<float, 3>& value) {
    return number(value[0]) + " " + number(value[1]) + " " + number(value[2]);
}
template<typename T>
void writeField(QTextStream& out, const char* name, const T& value) {
    out << "\t\t" << name << " ( " << number(value) << " )\n";
}
template<typename T>
void writeOptional(QTextStream& out, const char* name, const std::optional<T>& value) {
    if (value) writeField(out, name, *value);
}

QString numericAtom(FileBuffer* data) {
    // ParserX still traverses the world blocks. Read this form's plain numeric
    // values with checked Qt conversions: ParserX::GetNumber accumulates in
    // float precision and mishandles positive exponent signs on reload.
    QString atom;
    for (;;) {
        const QChar c(data->getShort());
        if (c.isSpace()) {
            if (atom.isEmpty()) continue;
            break;
        }
        if (c == '(' || c == ')') {
            data->off -= 2;
            break;
        }
        atom += c;
        if (atom.size() > 128) throw FileBuffer::ParseError("Telepole numeric value too long");
    }
    return atom;
}
}

TelepoleObj::TelepoleObj() {
    type = "telepole";
    typeID = telepole;
    size = 0;
    skipLevel = 1;
    x = y = 0;
    std::fill_n(position, 3, 0);
    std::fill_n(placedAtPosition, 3, 0);
    std::fill_n(firstPosition, 3, 0);
    std::fill_n(qDirection, 4, 0);
    qDirection[3] = 1;
    setMartix();
}

WorldObj* TelepoleObj::clone() { return new TelepoleObj(*this); }

void TelepoleObj::load(int tileX, int tileY) {
    x = tileX;
    y = tileY;
    if (!coordinatesConverted) {
        position[2] = -position[2];
        coordinatesConverted = true;
    }
    setMartix();
    loaded = true;
    modified = false;
}

void TelepoleObj::set(TS::TokenId token, FileBuffer* data) {
    if (std::find(std::begin(fields), std::end(fields), token) == std::end(fields)) {
        WorldObj::set(token, data);
        return;
    }
    data->skipLabel();
    readField(token, data, true);
}

void TelepoleObj::set(QString token, FileBuffer* data) {
    for (auto id : fields) {
        if (token.compare(QLatin1String(TS::name(id)), Qt::CaseInsensitive) == 0) {
            readField(id, data, false);
            return;
        }
    }
    WorldObj::set(token, data);
}

void TelepoleObj::readField(TS::TokenId token, FileBuffer* data, bool binary) {
    const auto readUint = [&]() {
        if (binary) return data->getUint();
        bool ok;
        const auto value = numericAtom(data).toUInt(&ok);
        if (!ok) throw FileBuffer::ParseError("Invalid Telepole unsigned integer");
        return value;
    };
    const auto scalar = [&]() {
        if (binary) return data->getFloat();
        bool ok;
        const auto value = numericAtom(data).toFloat(&ok);
        if (!ok) throw FileBuffer::ParseError("Invalid Telepole float");
        return value;
    };
    const auto vector = [&]() { return Vector{scalar(), scalar(), scalar()}; };
    switch (token) {
    case TS::UiD: UiD = readUint(); hasUid = true; break;
    case TS::Population: population = readUint(); break;
    case TS::StartPosition: startPosition = vector(); break;
    case TS::EndPosition: endPosition = vector(); break;
    case TS::StartType: startType = readUint(); break;
    case TS::EndType: endType = readUint(); break;
    case TS::StartDirection: startDirection = scalar(); break;
    case TS::EndDirection: endDirection = scalar(); break;
    case TS::Config: config = readUint(); break;
    case TS::Quality: quality = readUint(); break;
    case TS::Position: {
        const auto value = vector();
        std::copy(value.begin(), value.end(), position);
        if (coordinatesConverted) position[2] = -position[2];
        if (!hasPosition) ++jestPQ;
        hasPosition = true;
        break;
    }
    case TS::Direction: direction = vector(); break;
    case TS::MaxVisDistance: maxVisDistance = scalar(); break;
    case TS::VDbId: vDbId = readUint(); hasVdbId = true; break;
    }
}

void TelepoleObj::save(QTextStream* out) {
    if (!loaded) return;
    *out << "\tTelepole (\n";
    if (hasUid) writeField(*out, "UiD", UiD);
    writeOptional(*out, "Population", population);
    writeOptional(*out, "StartPosition", startPosition);
    writeOptional(*out, "EndPosition", endPosition);
    writeOptional(*out, "StartType", startType);
    writeOptional(*out, "EndType", endType);
    writeOptional(*out, "StartDirection", startDirection);
    writeOptional(*out, "EndDirection", endDirection);
    writeOptional(*out, "Config", config);
    writeOptional(*out, "Quality", quality);
    if (hasPosition) writeField(*out, "Position", Vector{
        position[0], position[1], coordinatesConverted ? -position[2] : position[2]});
    writeOptional(*out, "Direction", direction);
    writeOptional(*out, "MaxVisDistance", maxVisDistance);
    if (hasVdbId) writeField(*out, "VDbId", vDbId);
    *out << "\t)\n";
}
