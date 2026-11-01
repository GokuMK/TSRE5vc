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
#include <tsre/ui/UiRuntime.h>
#include <tsre/script/ScriptState.h>
#include <tsre/ErrorMessage.h>
#include <tsre/ErrorMessagesLib.h>

#include <QSet>
#include <RmlUi/Core.h>
#include <limits>
#include <map>
#include <vector>

namespace Ui {

void displayMessage(bool error, const QString &text, const QString &action) {
    static QSet<QString> told;
    if (told.contains(text))
        return;
    told.insert(text);
    ErrorMessagesLib::PushErrorMessage(new ErrorMessage(error ? ErrorMessage::Type_Error : ErrorMessage::Type_Warning,
                                                        ErrorMessage::Source_World, text, action));
}

namespace {

// A display drawn before its pictures loaded: drawn again after this (ms).
constexpr qint64 PicturesRetry = 250;

// A row's train's data (`row.extra`).
struct ModelExtra {
    std::map<Rml::String, Rml::String> values;
};

// `row.extra.<key>`: its value, empty text for a key it has not (RmlUi binds
// structs by their members, none by key).
class ExtraDefinition final : public Rml::VariableDefinition {
public:
    explicit ExtraDefinition(Rml::VariableDefinition *text)
        : Rml::VariableDefinition(Rml::DataVariableType::Struct), text(text) {}
    Rml::DataVariable Child(void *ptr, const Rml::DataAddressEntry &address) override {
        std::map<Rml::String, Rml::String> &values = static_cast<ModelExtra *>(ptr)->values;
        const auto found = values.find(address.name);
        if (found != values.end())
            return Rml::DataVariable(text, &found->second);
        none.clear();
        return Rml::DataVariable(text, &none);
    }

private:
    Rml::VariableDefinition *text;
    Rml::String none;
};

// The data model's row.
struct ModelRow {
    Rml::String time, train, destination, origin, platform, delay;
    ModelExtra extra;
};

// The data model's train (a car's display).
struct ModelTrain {
    Rml::String name, origin, destination, state, at, next, previous, platform, departs, arrives, distance, delay;
    ModelExtra extra;
};

ModelExtra modelExtra(const QMap<QString, QString> &extra) {
    ModelExtra out;
    for (auto it = extra.cbegin(); it != extra.cend(); ++it)
        out.values[it.key().toStdString()] = it.value().toStdString();
    return out;
}

ModelRow modelRow(const DisplayDocument::Row &row) {
    return {row.time.toStdString(), row.train.toStdString(), row.destination.toStdString(),
            row.origin.toStdString(), row.platform.toStdString(), row.delay.toStdString(), modelExtra(row.extra)};
}

ModelTrain modelTrain(const DisplayDocument::Train &train) {
    return {train.name.toStdString(), train.origin.toStdString(), train.destination.toStdString(),
            train.state.toStdString(), train.at.toStdString(), train.next.toStdString(),
            train.previous.toStdString(), train.platform.toStdString(), train.departs.toStdString(),
            train.arrives.toStdString(), train.distance.toStdString(), train.delay.toStdString(),
            modelExtra(train.extra)};
}

}

// The data model `display`.
struct DisplayDocument::Model {
    Rml::DataModelHandle handle;
    Rml::String time, date, station, platform;
    int hours = 0, minutes = 0, seconds = 0;
    std::vector<ModelRow> departures, arrivals;
    ModelTrain train;
};

DisplayDocument::DisplayDocument(const QString &path, QSize size, float refreshRate,
                                 ::ShapeDisplays::Display::Input input)
    : file(path), pixels(size), rate(refreshRate), takes(input) {
}

DisplayDocument::~DisplayDocument() = default;

bool DisplayDocument::due() const {
    if (broken)
        return false;
    return !drawn || (rate > 0.0f && last.elapsed() >= qint64(1000.0f / rate))
            || (picturesPending && last.elapsed() >= PicturesRetry);
}

qint64 DisplayDocument::waiting() const {
    if (!due())
        return 0;
    if (!drawn)
        return std::numeric_limits<qint64>::max() / 2;
    const qint64 interval = rate > 0.0f ? qint64(1000.0f / rate) : PicturesRetry;
    return last.elapsed() - (picturesPending ? std::min(interval, PicturesRetry) : interval);
}

// The data model's values: the clock's, the departures and arrivals for a
// platform's or a station's display, the train for a car's.
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
    m.train = takes == ::ShapeDisplays::Display::Input::Train ? modelTrain(data.train) : ModelTrain();
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
        Rml::VariableDefinition *text = constructor.GetDataTypeRegister()->GetDefinition<Rml::String>();
        constructor.RegisterCustomDataVariableDefinition<ModelExtra>(Rml::MakeUnique<ExtraDefinition>(text));
        if (auto row = constructor.RegisterStruct<ModelRow>()) {
            row.RegisterMember("time", &ModelRow::time);
            row.RegisterMember("train", &ModelRow::train);
            row.RegisterMember("destination", &ModelRow::destination);
            row.RegisterMember("origin", &ModelRow::origin);
            row.RegisterMember("platform", &ModelRow::platform);
            row.RegisterMember("delay", &ModelRow::delay);
            row.RegisterMember("extra", &ModelRow::extra);
        }
        constructor.RegisterArray<std::vector<ModelRow>>();
        if (auto train = constructor.RegisterStruct<ModelTrain>()) {
            train.RegisterMember("name", &ModelTrain::name);
            train.RegisterMember("origin", &ModelTrain::origin);
            train.RegisterMember("destination", &ModelTrain::destination);
            train.RegisterMember("state", &ModelTrain::state);
            train.RegisterMember("at", &ModelTrain::at);
            train.RegisterMember("next", &ModelTrain::next);
            train.RegisterMember("previous", &ModelTrain::previous);
            train.RegisterMember("platform", &ModelTrain::platform);
            train.RegisterMember("departs", &ModelTrain::departs);
            train.RegisterMember("arrives", &ModelTrain::arrives);
            train.RegisterMember("distance", &ModelTrain::distance);
            train.RegisterMember("delay", &ModelTrain::delay);
            train.RegisterMember("extra", &ModelTrain::extra);
        }
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
        constructor.Bind("train", &m.train);
        m.handle = constructor.GetModelHandle();
        // Its lists filled before it loads: its loops find their rows.
        fill(now, data);
        const Runtime::Reporting reporting(file);
        if (!display->load(file)) {
            broken = true;
            //% "The display document %1 did not load (its errors are in the log)."
            displayMessage(true, qtTrId("displays.document.load").arg(file));
            return;
        }
    }
    const Runtime::Reporting reporting(file);
    fill(now, data);
    model->handle.DirtyAllVariables();
    if (Rml::ElementDocument *document = display->document()) {
        // Its Lua, under a deadline of its own.
        Script::State::Deadline deadline(Runtime::get()->lua());
        Rml::Dictionary parameters;
        parameters["time"] = double(now.time().msecsSinceStartOfDay()) / 1000.0;
        document->DispatchEvent("displayupdate", parameters);
    }
    display->update();
    display->draw(renderer);
    picturesPending = display->picturesPending();
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
