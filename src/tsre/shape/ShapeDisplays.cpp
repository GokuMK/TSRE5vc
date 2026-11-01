/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/shape/ShapeDisplays.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/ReadFile.h>

#include <QFile>

namespace ShapeDisplays {

namespace {

void numbers(FileBuffer *data, float *out, int count) {
    for (int i = 0; i < count; ++i)
        out[i] = ParserX::GetNumber(data);
}

Display block(FileBuffer *data) {
    Display display;
    QString sh;
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        if (sh == "document") {
            display.document = ParserX::GetString(data).trimmed();
        } else if (sh == "image") {
            display.image = ParserX::GetString(data).trimmed();
        } else if (sh == "shapehierarchy") {
            display.hierarchy = ParserX::GetString(data).trimmed();
        } else if (sh == "position") {
            numbers(data, display.position, 3);
            display.square = true;
        } else if (sh == "rotation") {
            numbers(data, display.rotation, 3);
        } else if (sh == "size") {
            numbers(data, display.size, 3);
        } else if (sh == "resolution") {
            display.resolution[0] = int(ParserX::GetNumber(data));
            display.resolution[1] = int(ParserX::GetNumber(data));
        } else if (sh == "surface") {
            display.surface = ParserX::GetString(data).trimmed().compare("paper", Qt::CaseInsensitive) == 0
                    ? Display::Surface::Paper : Display::Surface::Glass;
        } else if (sh == "refreshrate") {
            display.refreshRate = ParserX::GetNumber(data);
        } else if (sh == "input") {
            const QString input = ParserX::GetString(data).trimmed().toLower();
            display.input = input == "clock" ? Display::Input::Clock
                    : input == "platform" ? Display::Input::Platform
                    : input == "station" ? Display::Input::Station : Display::Input::None;
        }
        ParserX::SkipToken(data);
    }
    // A square needs its size; a texture its image.
    display.square = display.square && display.image.isEmpty() && display.size[0] > 0.0f && display.size[1] > 0.0f;
    return display;
}

}

bool readBlock(FileBuffer *data, Display &display) {
    display = block(data);
    return !display.document.isEmpty() && (display.square || !display.image.isEmpty());
}

QVector<Display> read(const QString &shapePath) {
    QVector<Display> displays;
    QFile file(ContentPath::withExtension(shapePath, "sd"));
    if (!file.open(QIODevice::ReadOnly))
        return displays;
    FileBuffer *data = ReadFile::read(&file);
    file.close();
    data->toUtf16();
    data->skipBOM();
    QString sh;
    // As SFileLegacy::loadSd: the header passes, the shape's block is read.
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        if (sh != "shape")
            continue;
        ParserX::GetString(data);
        while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
            Display display;
            if (sh == "esd_tsredisplay" && readBlock(data, display))
                displays.push_back(display);
            ParserX::SkipToken(data);
        }
        ParserX::SkipToken(data);
    }
    delete data;
    return displays;
}

QString documentsFolder(const QString &shapePath) {
    const QStringList parts = QString(shapePath).replace(QLatin1Char('\\'), QLatin1Char('/')).split(QLatin1Char('/'));
    for (int i = 0; i + 2 < parts.size(); ++i) {
        // ROUTES/<route>/... and TRAINS/TRAINSET/<dir>/...
        if (parts[i].compare("ROUTES", Qt::CaseInsensitive) == 0)
            return QStringList(parts.mid(0, i + 2)).join(QLatin1Char('/')) + "/DISPLAYS";
        if (parts[i].compare("TRAINS", Qt::CaseInsensitive) == 0
                && parts[i + 1].compare("TRAINSET", Qt::CaseInsensitive) == 0 && i + 3 < parts.size())
            return QStringList(parts.mid(0, i + 3)).join(QLatin1Char('/')) + "/DISPLAYS";
    }
    return QString();
}

}
