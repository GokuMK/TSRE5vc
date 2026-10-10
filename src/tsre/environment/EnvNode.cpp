/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/environment/EnvNode.h>
#include <tsre/fileFunctions/SimisTextReader.h>

#include <QFile>
#include <QSaveFile>
#include <QStringEncoder>
#include <cmath>

using Kind = SimisTextReader::Kind;

EnvNode EnvNode::value(const QString &text, bool quoted) {
    EnvNode node;
    node.text = text;
    node.quoted = quoted;
    return node;
}

EnvNode EnvNode::number(double value) {
    return EnvNode::value(floatText(float(value)));
}

EnvNode EnvNode::integer(qint64 value) {
    return EnvNode::value(QString::number(value));
}

EnvNode EnvNode::blockOf(const QString &name, const QVector<EnvNode> &items) {
    EnvNode node;
    node.text = name;
    node.block = true;
    node.items = items;
    return node;
}

bool EnvNode::operator==(const EnvNode &other) const {
    return block == other.block && quoted == other.quoted && text == other.text
            && items == other.items;
}

QStringList EnvNode::values() const {
    QStringList list;
    for (const EnvNode &item : items) {
        if (item.block)
            break;
        list << item.text;
    }
    return list;
}

bool EnvNode::numberAt(int index, double &out) const {
    const QStringList list = values();
    if (index < 0 || index >= list.size())
        return false;
    SimisTextReader::Token token;
    token.kind = Kind::Atom;
    token.text = list[index];
    return SimisTextReader::number(token, out);
}

QString EnvNode::floatText(float value) {
    if (!std::isfinite(value))
        return QStringLiteral("0");
    // Plain decimals, as MSTS files write numbers: the fewest that read back
    // as the same float (230, 0.25, -0.008); exponents only for numbers too
    // small or large for that.
    const float magnitude = std::abs(value);
    if (magnitude == 0.0f || (magnitude >= 1e-4f && magnitude < 1e9f)) {
        for (int decimals = 0; decimals <= 12; ++decimals) {
            const QString text = QString::number(double(value), 'f', decimals);
            if (text.toFloat() == value)
                return text == "-0" ? QStringLiteral("0") : text;
        }
    }
    for (int precision = 1; precision <= 9; ++precision) {
        const QString text = QString::number(double(value), 'g', precision);
        if (text.toFloat() == value)
            return text;
    }
    return QString::number(double(value), 'g', 9);
}

namespace {

QString location(const QString &text, qsizetype offset) {
    const QString before = text.left(offset);
    return QString("line %1, column %2").arg(before.count('\n') + 1)
            .arg(offset - before.lastIndexOf('\n'));
}

// Reads items up to the end of the enclosing block (its ')' consumed) or of
// the text.
bool readItems(SimisTextReader &reader, const QString &text, QVector<EnvNode> &items,
               bool topLevel, QString &error, QStringList *warnings) {
    for (;;) {
        const Kind kind = reader.peekKind();
        if (kind == Kind::Error) {
            const auto &errors = reader.diagnostics();
            error = errors.isEmpty() ? QStringLiteral("Malformed text")
                                     : QString("%1 at line %2, column %3").arg(errors.back().message)
                                       .arg(errors.back().line).arg(errors.back().column);
            return false;
        }
        if (kind == Kind::End) {
            if (topLevel)
                return true;
            error = QStringLiteral("The file ends inside a block");
            return false;
        }
        const SimisTextReader::Token token = reader.next();
        if (kind == Kind::Close) {
            if (topLevel) {
                // Some stock files end with one ')' too many; MSTS and Open
                // Rails accept them.
                if (warnings)
                    *warnings << "Skipped an unmatched ')' at " + location(text, token.begin);
                continue;
            }
            return true;
        }
        EnvNode node;
        if (kind == Kind::Open) {
            // A block without a name.
            node.block = true;
        } else {
            node.text = QString(token.text.constData(), token.text.size());
            node.quoted = kind == Kind::String;
            if (reader.peekKind() == Kind::Open) {
                reader.next();
                node.block = true;
            }
        }
        if (node.block && !readItems(reader, text, node.items, false, error, warnings))
            return false;
        items.push_back(node);
    }
}

QString valueText(const EnvNode &value) {
    if (value.quoted)
        return SimisTextReader::quote(value.text);
    // An empty value or one the lexer would split must be quoted.
    bool plain = !value.text.isEmpty();
    for (QChar c : value.text)
        if (c.isSpace() || c == '(' || c == ')' || c == '"')
            plain = false;
    return plain ? value.text : SimisTextReader::quote(value.text);
}

void writeBlock(const EnvNode &node, int depth, QString &out) {
    const QString indent(depth, '\t');
    out += indent + node.text + (node.text.isEmpty() ? "(" : " (");
    int i = 0;
    for (; i < node.items.size() && !node.items[i].block; ++i)
        out += ' ' + valueText(node.items[i]);
    if (i == node.items.size()) {
        out += " )\n";
        return;
    }
    out += '\n';
    for (; i < node.items.size(); ++i) {
        if (node.items[i].block)
            writeBlock(node.items[i], depth + 1, out);
        else
            out += QString(depth + 1, '\t') + valueText(node.items[i]) + '\n';
    }
    out += indent + ")\n";
}

} // namespace

bool EnvText::parse(const QString &text, QVector<EnvNode> &items, QString &error, QStringList *warnings) {
    items.clear();
    SimisTextReader reader(text);
    return readItems(reader, text, items, true, error, warnings);
}

bool EnvText::read(const QString &path, QVector<EnvNode> &items, QString &error, QStringList *warnings) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = "Cannot open " + path;
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.startsWith("SIMISA@F") || bytes.mid(2, 16) == QByteArray("S\0I\0M\0I\0S\0A\0@\0F\0", 16)) {
        error = "Compressed files are not supported: " + path;
        return false;
    }
    QString text;
    if (!SimisTextReader::decode(bytes, text, error))
        return false;
    if (!parse(text, items, error, warnings)) {
        error = path + ": " + error;
        return false;
    }
    return true;
}

QString EnvText::write(const QVector<EnvNode> &items) {
    QString out;
    for (const EnvNode &item : items) {
        if (item.block) {
            out += '\n';
            writeBlock(item, 0, out);
        } else {
            out += valueText(item) + '\n';
        }
    }
    return out;
}

bool EnvText::save(const QString &path, const QVector<EnvNode> &items, QString &error) {
    QString text = write(items);
    text.replace("\n", "\r\n");
    QStringEncoder encoder(QStringEncoder::Utf16LE);
    const QByteArray bytes = QByteArray("\xff\xfe", 2) + QByteArray(encoder(text));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = "Cannot write " + path;
        return false;
    }
    return true;
}
