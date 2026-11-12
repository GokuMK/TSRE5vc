/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MASZYNATEXT_H
#define MASZYNATEXT_H

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <vector>

// MaSzyna's text files, .mmd and .fiz (task maszyna 02): tokens or lines as
// views of one copy of each file, through its include ... end with their
// (pN) parameters. Comments are // and /* */, tokens split at white space
// and ;, [ a b c ] gives one of its words: the first, so a vehicle is the
// same every load, or the chooser's. A view is valid until the next call.
class MaszynaText {
public:
    // The file `name` in `folder`, which is also where its includes are
    // looked for; `parameters` are its (p1), (p2)...
    MaszynaText(const QString &folder, const QString &name, const QStringList &parameters = QStringList());
    bool opened() const { return !frames.empty() || wasOpened; }

    // The next token, false at the end. Compare with same(), and decode only
    // what is kept.
    bool next(QByteArrayView &token);
    // The next non-empty line, without its comment and the spaces around it.
    bool nextLine(QByteArrayView &line);
    // Files named by an include that were not found.
    const QStringList &problems() const { return missing; }
    // Every file read so far, this one and its includes.
    const QStringList &files() const { return read; }

    // Which of a set's n words to take (0 to n - 1), for the sets next()
    // takes and include [ a b ] end; none: the first. MaSzyna picks at
    // random at every load; a caller may pick per car (task maszyna 05).
    std::function<int(int)> chooser;
    // next() gives [ and ] as words and leaves the set to the caller
    // (MaSzyna's sound reader: [ ] there are also vectors); an include's
    // set is still taken here.
    bool keepBrackets = false;

    // Every file `name` reads, each include's set taken whole: all a car of
    // it may read, whichever member it picks (what an archive packs; a
    // reading takes one); and the files so named that are not there.
    static void allFiles(const QString &folder, const QString &name, const QStringList &parameters,
                         QStringList &found, QStringList &notFound);

    // Equal in any case (ASCII).
    static bool same(QByteArrayView a, const char *b);
    static bool startsWith(QByteArrayView a, const char *b);
    static QString decode(QByteArrayView text);
    // Everything read is read again.
    static void forget();

private:
    struct Frame {
        std::shared_ptr<const QByteArray> text;
        qsizetype at = 0;
        QList<QByteArray> parameters;
    };
    std::vector<Frame> frames;
    QString folder;
    QStringList missing, read;
    QByteArray scratch;
    bool wasOpened = false;
    // allFiles(): an include's set read whole.
    bool everyMember = false;

    bool push(const QString &name, const QList<QByteArray> &parameters);
    void skipBlank(Frame &frame, bool lines);
    // A token of the top frame as written, false when the frame has ended.
    bool rawToken(QByteArrayView &token);
    QByteArrayView substituted(QByteArrayView token, const Frame &frame);
    void include();
    // The words of a set after its [ to its ], one taken.
    bool set(QByteArrayView &token);
};

#endif // MASZYNATEXT_H
