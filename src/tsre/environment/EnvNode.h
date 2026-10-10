/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ENVNODE_H
#define ENVNODE_H

#include <QString>
#include <QStringList>
#include <QVector>

// The token tree of a SIMIS text file (ENV files): a value, or a block with
// a name and its items (values and blocks) in file order. The environment
// model keeps the blocks it does not know as these, so writing a file loses
// nothing.
struct EnvNode {
    QString text;          // a value's text, or a block's name
    bool block = false;
    bool quoted = false;   // a value written in quotes
    QVector<EnvNode> items;

    static EnvNode value(const QString &text, bool quoted = false);
    static EnvNode number(double value);
    static EnvNode integer(qint64 value);
    static EnvNode blockOf(const QString &name, const QVector<EnvNode> &items = {});

    bool operator==(const EnvNode &other) const;
    bool operator!=(const EnvNode &other) const { return !(*this == other); }

    // The block's leading values (before its first block), and their count.
    QStringList values() const;
    // The value at index among the block's values, as a number.
    bool numberAt(int index, double &out) const;

    // Shortest text of a float that reads back as the same float.
    static QString floatText(float value);
};

namespace EnvText {
// Reads the items of a file's text: its header value, then its blocks.
// False with a message (line and column) when the text is malformed; an
// unmatched ')' at the top level is skipped with a warning.
bool parse(const QString &text, QVector<EnvNode> &items, QString &error, QStringList *warnings = nullptr);
// Reads a file (UTF-16 or 8-bit text).
bool read(const QString &path, QVector<EnvNode> &items, QString &error, QStringList *warnings = nullptr);
// The text of items, MSTS layout: tab indents, a block with only values on
// one line.
QString write(const QVector<EnvNode> &items);
// Writes UTF-16 LE with a byte order mark and CRLF lines, as MSTS files.
bool save(const QString &path, const QVector<EnvNode> &items, QString &error);
}

#endif
