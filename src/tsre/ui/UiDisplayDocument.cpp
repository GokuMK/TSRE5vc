/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/ui/UiDisplayDocument.h>
#include <tsre/ui/UiDisplay.h>

#include <RmlUi/Core.h>
#include <limits>
#include <vector>

namespace Ui {

namespace {

// The data model's row.
struct ModelRow {
    Rml::String time, train, destination, origin, platform, delay;
};

ModelRow modelRow(const DisplayDocument::Row &row) {
    return {row.time.toStdString(), row.train.toStdString(), row.destination.toStdString(),
            row.origin.toStdString(), row.platform.toStdString(), row.delay.toStdString()};
}

}

// The data model `display`.
struct DisplayDocument::Model {
    Rml::DataModelHandle handle;
    Rml::String time, date, station, platform;
    int hours = 0, minutes = 0, seconds = 0;
    std::vector<ModelRow> departures, arrivals;
};

DisplayDocument::DisplayDocument(const QString &path, QSize size, float refreshRate,
                                 ::ShapeDisplays::Display::Input input)
    : file(path), pixels(size), rate(refreshRate), takes(input) {
}

DisplayDocument::~DisplayDocument() = default;

bool DisplayDocument::due() const {
    if (broken)
        return false;
    return !drawn || (rate > 0.0f && last.elapsed() >= qint64(1000.0f / rate));
}

qint64 DisplayDocument::waiting() const {
    if (!due())
        return 0;
    if (!drawn)
        return std::numeric_limits<qint64>::max() / 2;
    return last.elapsed() - qint64(1000.0f / rate);
}

// The data model's values: the clock's, and the departures and arrivals
// for a platform's or a station's display.
void DisplayDocument::fill(const QDateTime &now, const Data &data) {
    Model &m = *model;
    m.time = now.toString("HH:mm").toStdString();
    m.date = now.toString("yyyy-MM-dd").toStdString();
    m.hours = now.time().hour();
    m.minutes = now.time().minute();
    m.seconds = now.time().second();
    m.station = data.station.toStdString();
    m.platform = data.platform.toStdString();
    m.departures.clear();
    m.arrivals.clear();
    if (takes == ::ShapeDisplays::Display::Input::Platform || takes == ::ShapeDisplays::Display::Input::Station) {
        for (const Row &row : data.departures)
            m.departures.push_back(modelRow(row));
        for (const Row &row : data.arrivals)
            m.arrivals.push_back(modelRow(row));
    }
}

void DisplayDocument::draw(Renderer &renderer, const QDateTime &now, const Data &data) {
    if (broken)
        return;
    if (!display) {
        display = std::make_unique<Display>(pixels.width(), pixels.height());
        model = std::make_unique<Model>();
        Rml::Context *context = display->context();
        Rml::DataModelConstructor constructor = context != nullptr ? context->CreateDataModel("display")
                                                                   : Rml::DataModelConstructor();
        if (!constructor) {
            broken = true;
            return;
        }
        if (auto row = constructor.RegisterStruct<ModelRow>()) {
            row.RegisterMember("time", &ModelRow::time);
            row.RegisterMember("train", &ModelRow::train);
            row.RegisterMember("destination", &ModelRow::destination);
            row.RegisterMember("origin", &ModelRow::origin);
            row.RegisterMember("platform", &ModelRow::platform);
            row.RegisterMember("delay", &ModelRow::delay);
        }
        constructor.RegisterArray<std::vector<ModelRow>>();
        Model &m = *model;
        constructor.Bind("time", &m.time);
        constructor.Bind("date", &m.date);
        constructor.Bind("hours", &m.hours);
        constructor.Bind("minutes", &m.minutes);
        constructor.Bind("seconds", &m.seconds);
        constructor.Bind("station", &m.station);
        constructor.Bind("platform", &m.platform);
        constructor.Bind("departures", &m.departures);
        constructor.Bind("arrivals", &m.arrivals);
        m.handle = constructor.GetModelHandle();
        // Its lists filled before it loads: its loops find their rows.
        fill(now, data);
        if (!display->load(file)) {
            broken = true;
            return;
        }
    }
    fill(now, data);
    model->handle.DirtyAllVariables();
    display->update();
    display->draw(renderer);
    drawn = true;
    last.start();
}

unsigned int DisplayDocument::texture() const {
    return display ? display->texture() : 0;
}

void DisplayDocument::release(Renderer &renderer) {
    if (display)
        display->release(renderer);
    drawn = false;
}

}
