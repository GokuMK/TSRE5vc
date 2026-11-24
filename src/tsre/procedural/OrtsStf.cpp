/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/procedural/OrtsStf.h>

namespace OrtsStf {

QStringList tokenize(const QString &text, QStringList &diagnostics,
        QVector<int> *lines) {
    QStringList tokens;
    int line = 1;
    auto append = [&](const QString &token, int tokenLine) {
        tokens.append(token);
        if(lines != nullptr)
            lines->append(tokenLine);
    };
    int i = 0;
    while(i < text.size()){
        const QChar c = text[i];
        if(c == '\n')
            line++;
        if(c.isSpace()){
            i++;
            continue;
        }
        if((c == '/' && i + 1 < text.size() && text[i + 1] == '/') || c == '#'){
            while(i < text.size() && text[i] != '\n')
                i++;
            continue;
        }
        if(c == '(' || c == ')'){
            append(QString(c), line);
            i++;
            continue;
        }
        if(c == '"'){
            const int tokenLine = line;
            QString value;
            i++;
            while(i < text.size() && text[i] != '"'){
                if(text[i] == '\n')
                    line++;
                if(text[i] == '\\' && i + 1 < text.size()){
                    i++;
                    value.append(text[i++]);
                } else {
                    value.append(text[i++]);
                }
            }
            if(i >= text.size())
                diagnostics.append("Unterminated quoted string");
            else
                i++;
            append(value, tokenLine);
            continue;
        }
        const int start = i;
        while(i < text.size() && !text[i].isSpace()
                && text[i] != '(' && text[i] != ')')
            i++;
        append(text.mid(start, i - start), line);
    }
    return tokens;
}

bool parseNode(const QStringList &tokens, int &index, const QString &name,
        Node &node, QStringList &diagnostics, const QVector<int> *lines) {
    node.name = name;
    if(lines != nullptr && index - 1 >= 0 && index - 1 < lines->size())
        node.line = lines->at(index - 1);
    if(index >= tokens.size() || tokens[index] != "("){
        diagnostics.append("Expected '(' after " + name);
        return false;
    }
    index++;
    while(index < tokens.size() && tokens[index] != ")"){
        const QString token = tokens[index++];
        if(index < tokens.size() && tokens[index] == "("){
            Node childNode;
            if(!parseNode(tokens, index, token, childNode, diagnostics, lines))
                return false;
            node.children.append(childNode);
        } else {
            node.values.append(token);
        }
    }
    if(index >= tokens.size()){
        diagnostics.append("Unclosed block " + name);
        return false;
    }
    index++;
    return true;
}

const Node *child(const Node &node, const QString &name) {
    for(const Node &candidate : node.children){
        if(candidate.name.compare(name, Qt::CaseInsensitive) == 0)
            return &candidate;
    }
    return nullptr;
}

QVector<const Node*> children(const Node &node, const QString &name) {
    QVector<const Node*> result;
    for(const Node &candidate : node.children){
        if(candidate.name.compare(name, Qt::CaseInsensitive) == 0)
            result.append(&candidate);
    }
    return result;
}

QString firstValue(const Node &node, const QString &name,
        const QString &fallback) {
    const Node *valueNode = child(node, name);
    if(valueNode == nullptr || valueNode->values.isEmpty())
        return fallback;
    return valueNode->values.first();
}

QRegularExpression wildcard(QString filter) {
    filter.remove(' ').remove('"');
    QString pattern = QRegularExpression::escape(filter);
    pattern.replace("\\?", ".").replace("\\*", ".*");
    return QRegularExpression("^" + pattern + "$", QRegularExpression::CaseInsensitiveOption);
}

}
