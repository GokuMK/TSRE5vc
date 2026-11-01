/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/ui/UiShapeDisplays.h>
#include <tsre/ui/UiDisplay.h>
#include <tsre/shape/ComplexShape.h>
#include <tsre/shape/ShapeDisplays.h>
#include <tsre/texture/TexLib.h>

#include <RmlUi/Core.h>
#include <QDir>

namespace Ui {

namespace {

// The data model's row.
struct ModelRow {
    Rml::String time, train, destination, origin, platform, delay;
};

ModelRow modelRow(const ShapeDisplays::Row &row) {
    return {row.time.toStdString(), row.train.toStdString(), row.destination.toStdString(),
            row.origin.toStdString(), row.platform.toStdString(), row.delay.toStdString()};
}

}

struct ShapeDisplays::Entry {
    ::ShapeDisplays::Display definition;
    std::unique_ptr<Display> display;
    bool failed = false;
    bool drawn = false;
    QDateTime last;
    // The data model `display`.
    Rml::DataModelHandle handle;
    Rml::String time, date, station, platform;
    std::vector<ModelRow> departures, arrivals;
};

// The data model's values: the clock's, and the departures and arrivals
// for a platform's or a station's display.
void ShapeDisplays::fill(Entry &e, const QDateTime &now, const Data &data) {
    e.time = now.toString("HH:mm").toStdString();
    e.date = now.toString("yyyy-MM-dd").toStdString();
    e.station = data.station.toStdString();
    e.platform = data.platform.toStdString();
    e.departures.clear();
    e.arrivals.clear();
    if (e.definition.input == ::ShapeDisplays::Display::Input::Platform
            || e.definition.input == ::ShapeDisplays::Display::Input::Station) {
        for (const Row &row : data.departures)
            e.departures.push_back(modelRow(row));
        for (const Row &row : data.arrivals)
            e.arrivals.push_back(modelRow(row));
    }
}

ShapeDisplays::ShapeDisplays(const QString &shapePath) : folder(::ShapeDisplays::documentsFolder(shapePath)) {
    for (const ::ShapeDisplays::Display &definition : ::ShapeDisplays::read(shapePath)) {
        entries.push_back(std::make_unique<Entry>());
        entries.back()->definition = definition;
    }
}

ShapeDisplays::~ShapeDisplays() = default;

bool ShapeDisplays::empty() const {
    return entries.empty();
}

void ShapeDisplays::update(Renderer &renderer, ComplexShape &shape, unsigned int stateId, const QDateTime &now,
                           const Data &data) {
    for (const std::unique_ptr<Entry> &entry : entries) {
        Entry &e = *entry;
        if (e.failed)
            continue;
        const ::ShapeDisplays::Display &d = e.definition;
        if (!e.display) {
            // Its size: the resolution, else the replaced texture's.
            QSize size(d.resolution[0], d.resolution[1]);
            if ((size.width() < 1 || size.height() < 1) && !d.image.isEmpty())
                size = TexLib::imageSize(QDir(shape.getTexPath()).filePath(d.image));
            if (size.width() < 1 || size.height() < 1)
                size = QSize(512, 256);
            e.display = std::make_unique<Display>(size.width(), size.height());
            Rml::Context *context = e.display->context();
            Rml::DataModelConstructor model = context != nullptr ? context->CreateDataModel("display")
                                                                 : Rml::DataModelConstructor();
            if (!model) {
                e.failed = true;
                continue;
            }
            if (auto row = model.RegisterStruct<ModelRow>()) {
                row.RegisterMember("time", &ModelRow::time);
                row.RegisterMember("train", &ModelRow::train);
                row.RegisterMember("destination", &ModelRow::destination);
                row.RegisterMember("origin", &ModelRow::origin);
                row.RegisterMember("platform", &ModelRow::platform);
                row.RegisterMember("delay", &ModelRow::delay);
            }
            model.RegisterArray<std::vector<ModelRow>>();
            model.Bind("time", &e.time);
            model.Bind("date", &e.date);
            model.Bind("station", &e.station);
            model.Bind("platform", &e.platform);
            model.Bind("departures", &e.departures);
            model.Bind("arrivals", &e.arrivals);
            e.handle = model.GetModelHandle();
            fill(e, now, data);
            if (folder.isEmpty() || !e.display->load(QDir(folder).filePath(d.document))) {
                e.failed = true;
                continue;
            }
        }
        // Due: once, or at its refresh rate.
        const bool due = !e.drawn
                || (d.refreshRate > 0.0f && e.last.msecsTo(now) >= qint64(1000.0f / d.refreshRate));
        if (!due)
            continue;
        fill(e, now, data);
        e.handle.DirtyAllVariables();
        e.display->update();
        const unsigned int texture = e.display->draw(renderer);
        if (!d.image.isEmpty())
            shape.setTextureOverride(stateId, d.image, texture);
        e.drawn = true;
        e.last = now;
    }
}

void ShapeDisplays::release(Renderer &renderer, ComplexShape *shape, unsigned int stateId) {
    for (const std::unique_ptr<Entry> &entry : entries) {
        if (shape != nullptr && !entry->definition.image.isEmpty())
            shape->setTextureOverride(stateId, entry->definition.image, 0);
        if (entry->display)
            entry->display->release(renderer);
        entry->drawn = false;
    }
}

}
