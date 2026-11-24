/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#ifndef ORTSSTF_H
#define ORTSSTF_H

#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

// The STF text reader of Open Rails' TrProfile files, shared by the profile
// parser and the route's track profile filters (tracks task 17): words,
// quoted strings, ( ) blocks; // and # comments to the line's end.
namespace OrtsStf {

struct Node {
    QString name;
    QStringList values;
    QVector<Node> children;
    // The line of the block's name in the text (1 the first), 0 unknown.
    int line = 0;
};

// The text's tokens; lines, when given, gets each token's line.
QStringList tokenize(const QString &text, QStringList &diagnostics,
        QVector<int> *lines = nullptr);
// The block whose '(' is tokens[index] (its name before it); index ends
// after its ')'. lines: tokenize()'s, for Node::line.
bool parseNode(const QStringList &tokens, int &index, const QString &name,
        Node &node, QStringList &diagnostics,
        const QVector<int> *lines = nullptr);
// Children by name, any case.
const Node *child(const Node &node, const QString &name);
QVector<const Node*> children(const Node &node, const QString &name);
QString firstValue(const Node &node, const QString &name,
        const QString &fallback = QString());
// Open Rails' ConvertToRegex: * and ? as wildcards over the whole name, any
// case (spaces and quotes dropped, as Open Rails does).
QRegularExpression wildcard(QString filter);

}

#endif
