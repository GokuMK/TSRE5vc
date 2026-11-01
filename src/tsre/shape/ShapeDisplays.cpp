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
#include <tsre/renderer/RenderItem.h>
#include <tsre/shape/MstsMaterials.h>
#include <tsre/shape/SFileDocument.h>

#include <QFile>

namespace ShapeDisplays {

namespace {

// A field's values in turn: from a token stream (ParserX's) or a parsed
// .sd's node.
struct Tokens {
    FileBuffer *data;
    QString string() { return ParserX::GetString(data); }
    float number() { return ParserX::GetNumber(data); }
};
struct Scalars {
    const SFileDetail::Node &node;
    int next = 0;
    QString string() { return node.scalar(next++); }
    float number() { return float(node.number(next++)); }
};

template <class Values>
void numbers(Values &values, float *out, int count) {
    for (int i = 0; i < count; ++i)
        out[i] = values.number();
}

// A block's field, its name read (lower case).
template <class Values>
void field(Display &display, const QString &name, Values &values) {
    if (name == "document") {
        display.document = values.string().trimmed();
    } else if (name == "image") {
        display.image = values.string().trimmed();
    } else if (name == "shapehierarchy") {
        display.hierarchy = values.string().trimmed();
    } else if (name == "position") {
        numbers(values, display.position, 3);
        display.square = true;
    } else if (name == "rotation") {
        numbers(values, display.rotation, 3);
    } else if (name == "size") {
        numbers(values, display.size, 3);
    } else if (name == "resolution") {
        display.resolution[0] = int(values.number());
        display.resolution[1] = int(values.number());
    } else if (name == "surface") {
        const QString surface = values.string().trimmed();
        display.surface = surface.compare("paper", Qt::CaseInsensitive) == 0 ? Display::Surface::Paper
                : surface.compare("glass", Qt::CaseInsensitive) == 0 ? Display::Surface::Glass
                : Display::Surface::Shape;
    } else if (name == "refreshrate") {
        display.refreshRate = values.number();
    } else if (name == "input") {
        const QString input = values.string().trimmed().toLower();
        display.input = input == "clock" ? Display::Input::Clock
                : input == "platform" ? Display::Input::Platform
                : input == "station" ? Display::Input::Station : Display::Input::None;
    }
}

// A square needs its size; a texture its image. Whether it is a display.
bool finish(Display &display) {
    display.square = display.square && display.image.isEmpty() && display.size[0] > 0.0f && display.size[1] > 0.0f;
    return !display.document.isEmpty() && (display.square || !display.image.isEmpty());
}

}

bool readBlock(FileBuffer *data, Display &display) {
    display = Display();
    Tokens tokens{data};
    QString sh;
    while (!((sh = ParserX::NextTokenInside(data).toLower()) == "")) {
        field(display, sh, tokens);
        ParserX::SkipToken(data);
    }
    return finish(display);
}

QVector<Display> read(const SFileDetail::Node &shape) {
    QVector<Display> displays;
    for (const SFileDetail::Node block : shape.children("esd_tsredisplay")) {
        Display display;
        for (const SFileDetail::Node value : block.children()) {
            Scalars scalars{value};
            field(display, value.name().toLower(), scalars);
        }
        if (finish(display))
            displays.push_back(display);
    }
    return displays;
}

void applySurface(RenderItem &item, Display::Surface surface) {
    if (surface == Display::Surface::Shape)
        return;
    const bool glass = surface == Display::Surface::Glass;
    if (!item.pbr.enabled) {
        MstsMaterials::applyLight(item, glass ? -8 : -5);
        return;
    }
    const RenderItem::Pbr &own = item.pbr;
    const int base = RenderItem::Pbr::MAP_BASE_COLOR;
    RenderItem::Pbr preset;
    preset.enabled = true;
    // As painted (KHR_materials_unlit: auto exposure undone).
    preset.unlit = glass;
    preset.metallic = 0.0f;
    preset.roughness = 1.0f;
    preset.alphaCutoff = own.alphaCutoff;
    preset.blend = own.blend;
    preset.texCoords[base] = own.texCoords[base];
    preset.uvTransforms[base] = own.uvTransforms[base];
    preset.wrap[base][0] = own.wrap[base][0];
    preset.wrap[base][1] = own.wrap[base][1];
    item.pbr = preset;
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
