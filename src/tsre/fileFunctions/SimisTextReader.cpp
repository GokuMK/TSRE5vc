#include "SimisTextReader.h"
#include <QStringDecoder>
#include <QtEndian>
#include <charconv>
#include <cmath>
#include <limits>

namespace {
inline bool space(QChar c) {
    const auto value = c.unicode();
    return value <= 0x7f ? value == ' ' || (value >= '\t' && value <= '\r') : c.isSpace();
}
} // namespace
SimisTextReader::SimisTextReader(QString text) : source(std::move(text)) {}
qsizetype SimisTextReader::position() const {
    return pendingCount == 0 ? cursor : pending[pendingHead].begin;
}
QString SimisTextReader::slice(qsizetype a, qsizetype b) const { return source.mid(a, b - a); }
SimisTextReader::Token SimisTextReader::failure(qsizetype start, const QString &message) {
    const auto prefix = source.left(start);
    const int line = prefix.count('\n') + 1;
    const int column = start - prefix.lastIndexOf('\n');
    errors.push_back({start, line, column, message});
    return {Kind::Error, message, start, cursor};
}
SimisTextReader::Token SimisTextReader::scan() {
    while (cursor < source.size() &&
           (space(source.constData()[cursor]) || source.constData()[cursor] == QChar(0xfeff)))
        ++cursor;
    const auto start = cursor;
    if (cursor == source.size())
        return {Kind::End, {}, cursor, cursor};
    const auto c = source.constData()[cursor++];
    if (c == '(')
        return {Kind::Open, {}, start, cursor};
    if (c == ')')
        return {Kind::Close, {}, start, cursor};
    if (c == '"') {
        QString value;
        for (;;) {
            bool closed = false;
            while (cursor < source.size()) {
                auto ch = source.constData()[cursor++];
                if (ch == '"') {
                    closed = true;
                    break;
                }
                if (ch == '\\' && cursor < source.size()) {
                    auto esc = source.constData()[cursor];
                    if (esc == '"' || esc == '\\') {
                        ch = esc;
                        ++cursor;
                    } else if (esc == 'n') {
                        ch = '\n';
                        ++cursor;
                    } else if (esc == 't') {
                        ch = '\t';
                        ++cursor;
                    }
                    // Unknown escapes (notably Windows paths) are retained.
                }
                value += ch;
            }
            if (!closed)
                return failure(start, "Unterminated quoted string");
            auto look = cursor;
            while (look < source.size() && space(source.constData()[look]))
                ++look;
            if (look == source.size() || source.constData()[look] != '+')
                break;
            ++look;
            while (look < source.size() && space(source.constData()[look]))
                ++look;
            if (look == source.size() || source.constData()[look] != '"')
                break;
            cursor = look + 1;
        }
        return {Kind::String, value, start, cursor};
    }
    while (cursor < source.size() && !space(source.constData()[cursor]) &&
           source.constData()[cursor] != '(' && source.constData()[cursor] != ')')
        ++cursor;
    return {Kind::Atom, QString::fromRawData(source.constData() + start, cursor - start), start,
            cursor};
}
const SimisTextReader::Token &SimisTextReader::lookahead(int ahead) {
    while (pendingCount <= ahead) {
        pending[(pendingHead + pendingCount) % pending.size()] = scan();
        ++pendingCount;
    }
    return pending[(pendingHead + ahead) % pending.size()];
}
SimisTextReader::Token SimisTextReader::peek(int ahead) {
    if (ahead < 0 || ahead > 2)
        return {Kind::Error, "Lookahead out of range", cursor, cursor};
    return lookahead(ahead);
}
SimisTextReader::Kind SimisTextReader::peekKind(int ahead) {
    if (ahead < 0 || ahead > 2)
        return Kind::Error;
    if (ahead || pendingCount)
        return lookahead(ahead).kind;
    // Classifying an ordinary atom needs only its first character. Leave its
    // span unscanned until next()/a numeric read actually consumes it. Quoted
    // strings still use the full scanner so malformed-string errors agree.
    const auto *text = source.constData();
    while (cursor < source.size() && (space(text[cursor]) || text[cursor] == QChar(0xfeff)))
        ++cursor;
    if (cursor == source.size())
        return Kind::End;
    if (text[cursor] == '(')
        return Kind::Open;
    if (text[cursor] == ')')
        return Kind::Close;
    return text[cursor] == '"' ? lookahead(0).kind : Kind::Atom;
}
QStringView SimisTextReader::nextAtom() {
    if (pendingCount) {
        const auto token = next();
        return token.kind == Kind::Atom
                   ? QStringView(source.constData() + token.begin, token.end - token.begin)
                   : QStringView();
    }
    const auto *text = source.constData();
    while (cursor < source.size() && (space(text[cursor]) || text[cursor] == QChar(0xfeff)))
        ++cursor;
    if (cursor == source.size())
        return {};
    if (text[cursor] == '(' || text[cursor] == ')' || text[cursor] == '"') {
        next(); // Preserve delimiter consumption and quoted-string diagnostics.
        return {};
    }
    const auto start = cursor++;
    while (cursor < source.size() && !space(text[cursor]) && text[cursor] != '(' &&
           text[cursor] != ')')
        ++cursor;
    return QStringView(text + start, cursor - start);
}
SimisTextReader::Token SimisTextReader::next() {
    if (pendingCount == 0)
        return scan();
    auto t = std::move(pending[pendingHead]);
    pendingHead = (pendingHead + 1) % pending.size();
    --pendingCount;
    return t;
}
bool SimisTextReader::skipBlock() {
    int depth = 1;
    while (pendingCount != 0) {
        const auto t = next();
        if (t.kind == Kind::Error || t.kind == Kind::End)
            return false;
        if (t.kind == Kind::Open)
            ++depth;
        if (t.kind == Kind::Close && --depth == 0)
            return true;
    }
    const auto *text = source.constData();
    // Skipping does not create tokens or decode quoted strings. It still respects
    // escaped quotes, nested delimiters, and the same bounded-input contract.
    while (cursor < source.size()) {
        const auto start = cursor;
        const auto c = text[cursor++];
        if (c == '"') {
            bool closed = false;
            while (cursor < source.size()) {
                const auto ch = text[cursor++];
                if (ch == '\\' && cursor < source.size())
                    ++cursor;
                else if (ch == '"') {
                    closed = true;
                    break;
                }
            }
            if (!closed) {
                failure(start, "Unterminated quoted string");
                return false;
            }
        } else if (c == '(') {
            if (++depth > 256) {
                failure(start, "Nesting limit exceeded");
                return false;
            }
        } else if (c == ')' && --depth == 0)
            return true;
    }
    return false;
}
bool SimisTextReader::decode(const QByteArray &b, QString &out, QString &error) {
    if (b.startsWith("\xff\xfe") || b.startsWith("\xfe\xff")) {
        if (b.size() % 2) {
            error = "Odd UTF-16 byte count";
            return false;
        }
        QStringDecoder decoder(b.startsWith("\xff\xfe") ? QStringDecoder::Utf16LE
                                                        : QStringDecoder::Utf16BE);
        out = decoder(b);
        if (decoder.hasError()) {
            error = "Invalid UTF-16";
            return false;
        }
    } else {
        QStringDecoder decoder(QStringDecoder::Utf8);
        out = decoder(b);
        if (decoder.hasError())
            out = QString::fromLatin1(b); // MSTS legacy byte text
    }
    return true;
}
namespace {
bool numberText(QStringView token, double &v) {
    if (token.isEmpty())
        return false;
    // SIMIS numbers are ASCII even in UTF-16 files. Parse short tokens on the
    // stack with the locale-independent C++ converter, with no QString
    // allocation.
    char bytes[128];
    if (token.size() >= qsizetype(sizeof(bytes))) {
        bool ok = false;
        v = QString::fromRawData(token.constData(), token.size()).toDouble(&ok);
        return ok && std::isfinite(v);
    }
    const auto *text = token.constData();
    for (qsizetype i = 0; i < token.size(); ++i) {
        if (text[i].unicode() > 127)
            return false;
        bytes[i] = char(text[i].unicode());
    }
    const char *begin = bytes, *end = bytes + token.size();
    if (*begin == '+')
        ++begin;
    if (begin == end || (begin != bytes && (*begin == '-' || *begin == '+')))
        return false;
    auto result = std::from_chars(begin, end, v, std::chars_format::general);
    return result.ec == std::errc() && result.ptr == end && std::isfinite(v);
}
bool integerMagnitude(QStringView token, quint32 &v, bool &negative, int base) {
    if (token.isEmpty() || (base != 10 && base != 16))
        return false;
    const auto *p = token.constData(), *end = p + token.size();
    negative = *p == '-';
    if (*p == '+' || *p == '-')
        ++p;
    if (base == 16 && end - p >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        p += 2;
    if (p == end)
        return false;
    const quint32 cutoff = base == 16 ? 0x0fffffffu : 429496729u;
    const unsigned lastDigit = base == 16 ? 15u : 5u;
    quint32 n = 0;
    for (; p < end; ++p) {
        const auto c = p->unicode();
        const unsigned digit = c >= '0' && c <= '9'   ? c - '0'
                               : c >= 'a' && c <= 'f' ? c - 'a' + 10
                               : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                      : 99;
        if (digit >= unsigned(base) || (n > cutoff || (n == cutoff && digit > lastDigit)))
            return false;
        n = n * base + digit;
    }
    v = n;
    return true;
}
} // namespace
bool SimisTextReader::number(const Token &t, double &v) {
    return t.kind == Kind::Atom && numberText(t.text, v);
}
bool SimisTextReader::readNumber(double &v) { return numberText(nextAtom(), v); }
bool SimisTextReader::readInteger(qint32 &v) {
    quint32 n;
    bool negative;
    if (!integerMagnitude(nextAtom(), n, negative, 10) ||
        n > (negative ? 0x80000000u : 0x7fffffffu))
        return false;
    v = negative ? qint32(-qint64(n)) : qint32(n);
    return true;
}
bool SimisTextReader::readUnsignedInteger(quint32 &v, int base) {
    bool negative;
    return integerMagnitude(nextAtom(), v, negative, base) && !negative;
}
bool SimisTextReader::integer(const Token &t, qint32 &v) {
    quint32 n;
    bool negative;
    if (t.kind != Kind::Atom || !integerMagnitude(t.text, n, negative, 10) ||
        n > (negative ? 0x80000000u : 0x7fffffffu))
        return false;
    v = negative ? qint32(-qint64(n)) : qint32(n);
    return true;
}
bool SimisTextReader::unsignedInteger(const Token &t, quint32 &v, int base) {
    bool negative;
    return t.kind == Kind::Atom && integerMagnitude(t.text, v, negative, base) && !negative;
}
QString SimisTextReader::quote(const QString &s) {
    QString out = "\"";
    for (auto c : s) {
        if (c == '\\' || c == '"')
            out += '\\';
        if (c == '\n')
            out += "\\n";
        else if (c == '\t')
            out += "\\t";
        else
            out += c;
    }
    return out + '"';
}

bool SimisTextReader::readBlockHeader(QString &label, bool &recovered) {
    label.clear();
    recovered = false;
    if (peekKind() == Kind::Open) {
        next();
        return true;
    }
    if (peekKind() != Kind::Atom && peekKind() != Kind::String)
        return false;
    const auto first = next();
    label = QString(first.text.constData(), first.text.size());
    int words = 1;
    while (peekKind() != Kind::Open) {
        // A quoted label is already one complete name. Only unquoted words
        // participate in this compatibility extension.
        if (first.kind != Kind::Atom || peekKind() != Kind::Atom ||
            ++words > 256 || label.size() + peek().text.size() + 1 > 4096)
            return false;
        label += ' ';
        label += next().text;
        recovered = true;
    }
    next();
    return true;
}
