/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/humans/HumanSet.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <memory>
#include <tsre/Game.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/ReadFile.h>

namespace Humans {

namespace {

// A file named in the list: in the folder's SHAPES, as written.
QString shapeFile(const QString &folder, const QString &name) {
    return QDir(folder).filePath("SHAPES/" + name.trimmed());
}

void readList(FileBuffer *data, const QString &folder, HumanSet &out) {
    HumanSet::List list;
    QString token;
    while (!(token = ParserX::NextTokenInside(data).toLower()).isEmpty()) {
        if (token == "name") {
            list.name = ParserX::GetString(data);
        } else if (token == "animations") {
            const QString name = ParserX::GetString(data);
            list.clips = shapeFile(folder, name);
            if (!QFileInfo::exists(list.clips)) {
                out.problems << QString("%1: clips file %2 not found").arg(out.file, list.clips);
                list.clips.clear();
            }
        } else if (token == "human") {
            HumanSet::Human human;
            human.path = shapeFile(folder, ParserX::GetString(data));
            human.weight = ParserX::GetNumber(data);
            if (!QFileInfo::exists(human.path))
                out.problems << QString("%1: human %2 not found").arg(out.file, human.path);
            else if (human.weight > 0.0f)
                list.humans.push_back(human);
        }
        ParserX::SkipToken(data);
    }
    if (list.humans.empty())
        out.problems << QString("%1: list %2 has no humans").arg(out.file, list.name);
    else
        out.lists.push_back(list);
}

}

bool HumanSet::read(const QString &humansDat, const QString &folder, HumanSet &out) {
    QFile file(humansDat);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    // MSTS's header line (SIMISA...) when there is one, in UTF-8 or UTF-16.
    const QByteArray head = file.peek(64);
    const bool header = head.contains("SIMISA") || head.contains(QByteArray("S\0I\0M\0I\0S\0A\0", 12));
    std::unique_ptr<FileBuffer> data(ReadFile::read(&file));
    file.close();
    if (!data)
        return false;
    data->toUtf16();
    if (header)
        ParserX::NextLine(data.get());
    out.file = humansDat;
    out.shapes = QDir(folder).filePath("SHAPES");
    out.textures = QDir(folder).filePath("TEXTURES");
    QString token;
    while (!(token = ParserX::NextTokenInside(data.get()).toLower()).isEmpty()) {
        if (token == "humanlist")
            readList(data.get(), folder, out);
        ParserX::SkipToken(data.get());
    }
    return true;
}

HumanSet HumanSet::forRoute(const QString &routeFolder, int mode, const QString &tsreFolder) {
    HumanSet set;
    if (mode == Never)
        return set;
    if (!routeFolder.isEmpty() && read(QDir(routeFolder).filePath("humans.dat"), routeFolder, set)) {
        set.source = Source::Route;
        return set;
    }
    if (mode == RouteDefined)
        return set;
    set = HumanSet();
    if (!tsreFolder.isEmpty() && read(QDir(tsreFolder).filePath("humans.dat"), tsreFolder, set))
        set.source = Source::Tsre;
    return set;
}

QString HumanSet::tsreHumansFolder() {
    return QStringLiteral("appdata/") + Game::AppDataVersion + QStringLiteral("/humans");
}

}
