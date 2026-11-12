/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/maszyna/MaszynaText.h>
#include <tsre/maszyna/Cp1250.h>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>

namespace {

constexpr size_t MaxDepth = 16;

struct Entry {
    std::shared_ptr<const QByteArray> text;
    QDateTime modified;
};

QMutex textMutex;
QHash<QString, Entry> texts;

std::shared_ptr<const QByteArray> textOf(const QString &path) {
    const QFileInfo info(path);
    if (!info.isFile())
        return nullptr;
    const QDateTime modified = info.lastModified();
    {
        QMutexLocker lock(&textMutex);
        const auto found = texts.constFind(path);
        if (found != texts.constEnd() && found->modified == modified)
            return found->text;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return nullptr;
    auto text = std::make_shared<const QByteArray>(file.readAll());
    QMutexLocker lock(&textMutex);
    texts.insert(path, Entry{text, modified});
    return text;
}

inline bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';';
}

inline char lower(char c) {
    return c >= 'A' && c <= 'Z' ? char(c + 32) : c;
}

}

MaszynaText::MaszynaText(const QString &folder, const QString &name, const QStringList &parameters) : folder(folder) {
    QList<QByteArray> given;
    for (const QString &parameter : parameters)
        given.push_back(parameter.toLatin1());
    wasOpened = push(name, given);
    missing.clear();
}

bool MaszynaText::push(const QString &name, const QList<QByteArray> &parameters) {
    if (frames.size() >= MaxDepth)
        return false;
    const QString path = QDir::cleanPath(folder + QLatin1Char('/') + name);
    std::shared_ptr<const QByteArray> text = textOf(path);
    if (text == nullptr) {
        missing.push_back(path);
        return false;
    }
    if (!read.contains(path))
        read.push_back(path);
    Frame frame;
    frame.text = text;
    frame.parameters = parameters;
    // A byte order mark some files start with.
    if (text->startsWith("\xEF\xBB\xBF"))
        frame.at = 3;
    frames.push_back(std::move(frame));
    return true;
}

void MaszynaText::skipBlank(Frame &frame, bool lines) {
    const char *data = frame.text->constData();
    const qsizetype size = frame.text->size();
    qsizetype at = frame.at;
    while (at < size) {
        const char c = data[at];
        if (isSpace(c) && !(lines && c == ';')) {
            ++at;
        } else if (c == '/' && at + 1 < size && data[at + 1] == '/') {
            while (at < size && data[at] != '\n')
                ++at;
        } else if (c == '/' && at + 1 < size && data[at + 1] == '*') {
            at += 2;
            while (at + 1 < size && !(data[at] == '*' && data[at + 1] == '/'))
                ++at;
            at = std::min(at + 2, size);
        } else {
            break;
        }
    }
    frame.at = at;
}

bool MaszynaText::rawToken(QByteArrayView &token) {
    Frame &frame = frames.back();
    skipBlank(frame, false);
    const char *data = frame.text->constData();
    const qsizetype size = frame.text->size();
    if (frame.at >= size)
        return false;
    if (data[frame.at] == '"') {
        const qsizetype start = ++frame.at;
        while (frame.at < size && data[frame.at] != '"')
            ++frame.at;
        token = QByteArrayView(data + start, frame.at - start);
        if (frame.at < size)
            ++frame.at;
        return true;
    }
    const qsizetype start = frame.at;
    while (frame.at < size && !isSpace(data[frame.at])) {
        if (data[frame.at] == '/' && frame.at + 1 < size && (data[frame.at + 1] == '/' || data[frame.at + 1] == '*'))
            break;
        ++frame.at;
    }
    token = QByteArrayView(data + start, frame.at - start);
    return true;
}

QByteArrayView MaszynaText::substituted(QByteArrayView token, const Frame &frame) {
    if (frame.parameters.isEmpty() || !token.contains("(p"))
        return token;
    scratch.clear();
    qsizetype at = 0;
    while (at < token.size()) {
        const qsizetype open = token.indexOf("(p", at);
        const qsizetype close = open >= 0 ? token.indexOf(')', open) : -1;
        if (open < 0 || close < 0) {
            scratch += token.sliced(at).toByteArray();
            break;
        }
        scratch += token.sliced(at, open - at).toByteArray();
        const int number = token.sliced(open + 2, close - open - 2).toByteArray().toInt();
        scratch += number >= 1 && number <= frame.parameters.size() ? frame.parameters[number - 1] : QByteArray("none");
        at = close + 1;
    }
    return QByteArrayView(scratch);
}

// "include <file> <parameters...> end", its word just read; a set's members
// all with everyMember (each read in turn, in their order).
void MaszynaText::include() {
    QByteArrayView token;
    QStringList names;
    QList<QByteArray> parameters;
    if (!next(token))
        return;
    const auto nameOf = [](QByteArrayView word) {
        return decode(word).toLower().replace(QLatin1Char('\\'), QLatin1Char('/'));
    };
    if (keepBrackets && same(token, "[")) {
        if (everyMember) {
            QByteArrayView item;
            while (rawToken(item) && !same(item, "]"))
                names << nameOf(substituted(item, frames.back()));
        } else if (set(token)) {
            names << nameOf(token);
        }
        if (names.isEmpty())
            return;
    } else {
        names << nameOf(token);
    }
    while (rawToken(token) && !same(token, "end"))
        parameters.push_back(substituted(token, frames.back()).toByteArray());
    // The last pushed is read first.
    for (qsizetype i = names.size() - 1; i >= 0; --i)
        push(names[i], parameters);
}

void MaszynaText::allFiles(const QString &folder, const QString &name, const QStringList &parameters,
                           QStringList &found, QStringList &notFound) {
    MaszynaText text(folder, name, parameters);
    text.keepBrackets = true;
    text.everyMember = true;
    QByteArrayView token;
    while (text.next(token)) {
    }
    for (const QString &file : text.files())
        if (!found.contains(file))
            found << file;
    for (const QString &file : text.problems())
        if (!notFound.contains(file))
            notFound << file;
}

bool MaszynaText::set(QByteArrayView &token) {
    QList<QByteArrayView> items;
    QByteArrayView item;
    while (rawToken(item) && !same(item, "]"))
        items.push_back(item);
    if (items.isEmpty())
        return false;
    const int chosen = chooser ? std::clamp(chooser(int(items.size())), 0, int(items.size()) - 1) : 0;
    token = substituted(items[chosen], frames.back());
    return true;
}

bool MaszynaText::next(QByteArrayView &token) {
    while (!frames.empty()) {
        if (!rawToken(token)) {
            frames.pop_back();
            continue;
        }
        if (same(token, "include")) {
            include();
            continue;
        }
        if (!keepBrackets && same(token, "[")) {
            if (set(token))
                return true;
            continue;
        }
        token = substituted(token, frames.back());
        return true;
    }
    return false;
}

bool MaszynaText::nextLine(QByteArrayView &line) {
    while (!frames.empty()) {
        Frame &frame = frames.back();
        skipBlank(frame, true);
        const char *data = frame.text->constData();
        const qsizetype size = frame.text->size();
        if (frame.at >= size) {
            frames.pop_back();
            continue;
        }
        const qsizetype start = frame.at;
        qsizetype end = start;
        while (end < size && data[end] != '\n' && data[end] != '\r'
               && !(data[end] == '/' && end + 1 < size && data[end + 1] == '/'))
            ++end;
        qsizetype next = end;
        while (next < size && data[next] != '\n' && data[next] != '\r')
            ++next;
        QByteArrayView text(data + start, end - start);
        while (!text.isEmpty() && isSpace(text.back()))
            text.chop(1);
        if (startsWith(text, "include") && (text.size() == 7 || isSpace(text[7]))) {
            frame.at = start + 7;
            include();
            continue;
        }
        frame.at = next;
        if (text.isEmpty())
            continue;
        line = substituted(text, frame);
        return true;
    }
    return false;
}

bool MaszynaText::same(QByteArrayView a, const char *b) {
    const qsizetype length = qsizetype(strlen(b));
    if (a.size() != length)
        return false;
    for (qsizetype i = 0; i < length; ++i)
        if (lower(a[i]) != lower(b[i]))
            return false;
    return true;
}

bool MaszynaText::startsWith(QByteArrayView a, const char *b) {
    const qsizetype length = qsizetype(strlen(b));
    return a.size() >= length && same(a.first(length), b);
}

QString MaszynaText::decode(QByteArrayView text) {
    return Maszyna::fromCp1250(text.data(), int(text.size()));
}

void MaszynaText::forget() {
    QMutexLocker lock(&textMutex);
    texts.clear();
}
