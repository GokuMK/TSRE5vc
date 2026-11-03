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
#include <tsre/ui/UiDocumentFunctions.h>
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

// A train's data (`row.extra`, `train.extra`).
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

// A train's fields (TrainFields), as rows and trains have them.
struct ModelFields {
    Rml::String category, number, name, operatorName, line, origin, destination;
};

// A car of the data model's train or row, and the data model's car (task
// scripting 03).
struct ModelCar {
    int index = 0;
    Rml::String number, tags, kind, name;
};

// The data model's row.
struct ModelRow : ModelFields {
    Rml::String time, id, platform, delay, arrives, departs;
    ModelExtra extra;
    std::vector<ModelCar> cars;
};

// A stop of the data model's train.
struct ModelStop {
    Rml::String station, platform, arrives, departs;
    bool passed = false;
};

// The data model's train (a car's display).
struct ModelTrain : ModelFields {
    Rml::String id, state, at, next, previous, platform, departs, arrives, distance, delay;
    std::vector<ModelStop> stops;
    ModelExtra extra;
    std::vector<ModelCar> cars;
};

ModelExtra modelExtra(const QMap<QString, QString> &extra) {
    ModelExtra out;
    for (auto it = extra.cbegin(); it != extra.cend(); ++it)
        out.values[it.key().toStdString()] = it.value().toStdString();
    return out;
}

void modelFields(ModelFields &out, const TrainFields &fields) {
    out.category = fields[TrainFields::Category].toStdString();
    out.number = fields[TrainFields::Number].toStdString();
    out.name = fields[TrainFields::Name].toStdString();
    out.operatorName = fields[TrainFields::Operator].toStdString();
    out.line = fields[TrainFields::Line].toStdString();
    out.origin = fields[TrainFields::Origin].toStdString();
    out.destination = fields[TrainFields::Destination].toStdString();
}

// The fields' names in a struct of them (`row.category`, `train.number`;
// RmlUi takes the struct's own member pointers).
template <typename T>
void registerFields(Rml::StructHandle<T> &handle) {
    using Text = Rml::String T::*;
    handle.RegisterMember("category", static_cast<Text>(&ModelFields::category));
    handle.RegisterMember("number", static_cast<Text>(&ModelFields::number));
    handle.RegisterMember("name", static_cast<Text>(&ModelFields::name));
    handle.RegisterMember("operator", static_cast<Text>(&ModelFields::operatorName));
    handle.RegisterMember("line", static_cast<Text>(&ModelFields::line));
    handle.RegisterMember("origin", static_cast<Text>(&ModelFields::origin));
    handle.RegisterMember("destination", static_cast<Text>(&ModelFields::destination));
}

ModelCar modelCar(const DisplayDocument::Car &car) {
    return {car.index, car.number.toStdString(), car.tags.toStdString(), car.kind.toStdString(),
            car.name.toStdString()};
}

ModelRow modelRow(const DisplayDocument::Row &row) {
    ModelRow out;
    modelFields(out, row.fields);
    out.time = row.time.toStdString();
    out.id = row.id.toStdString();
    out.platform = row.platform.toStdString();
    out.delay = row.delay.toStdString();
    out.arrives = row.arrives.toStdString();
    out.departs = row.departs.toStdString();
    out.extra = modelExtra(row.extra);
    for (const DisplayDocument::Car &car : row.cars)
        out.cars.push_back(modelCar(car));
    return out;
}

ModelTrain modelTrain(const DisplayDocument::Train &train) {
    ModelTrain out;
    modelFields(out, train.fields);
    out.id = train.id.toStdString();
    out.state = train.state.toStdString();
    out.at = train.at.toStdString();
    out.next = train.next.toStdString();
    out.previous = train.previous.toStdString();
    out.platform = train.platform.toStdString();
    out.departs = train.departs.toStdString();
    out.arrives = train.arrives.toStdString();
    out.distance = train.distance.toStdString();
    out.delay = train.delay.toStdString();
    for (const DisplayDocument::Stop &stop : train.stops)
        out.stops.push_back({stop.station.toStdString(), stop.platform.toStdString(), stop.arrives.toStdString(),
                             stop.departs.toStdString(), stop.passed});
    out.extra = modelExtra(train.extra);
    for (const DisplayDocument::Car &car : train.cars)
        out.cars.push_back(modelCar(car));
    return out;
}

}

// The data model `display`.
struct DisplayDocument::Model {
    Rml::DataModelHandle handle;
    Rml::String time, date, station, platform;
    // Seconds after midnight (the functions' clock: cycle, minutes_until).
    double clock = 0.0;
    int hours = 0, minutes = 0, seconds = 0, year = 0, month = 0, day = 0;
    std::vector<ModelRow> departures, arrivals, trains;
    ModelTrain train;
    ModelCar car;
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

// The data model's values: the clock's, a platform's trains, a station's
// departures and arrivals, the train for a car's display (Train, Car), the
// car for Car.
void DisplayDocument::fill(const QDateTime &now, const Data &data) {
    Model &m = *model;
    m.time = now.toString("HH:mm").toStdString();
    m.date = now.toString("yyyy-MM-dd").toStdString();
    m.hours = now.time().hour();
    m.minutes = now.time().minute();
    m.seconds = now.time().second();
    m.year = now.date().year();
    m.month = now.date().month();
    m.day = now.date().day();
    m.clock = now.time().msecsSinceStartOfDay() / 1000.0;
    m.station = data.station.toStdString();
    m.platform = data.platform.toStdString();
    m.departures.clear();
    m.arrivals.clear();
    m.trains.clear();
    if (takes == ::ShapeDisplays::Display::Input::Station) {
        for (const Row &row : data.departures)
            m.departures.push_back(modelRow(row));
        for (const Row &row : data.arrivals)
            m.arrivals.push_back(modelRow(row));
    } else if (takes == ::ShapeDisplays::Display::Input::Platform) {
        for (const Row &row : data.trains)
            m.trains.push_back(modelRow(row));
    }
    const bool car = takes == ::ShapeDisplays::Display::Input::Car;
    m.train = takes == ::ShapeDisplays::Display::Input::Train || car ? modelTrain(data.train) : ModelTrain();
    m.car = car ? modelCar(data.car) : ModelCar();
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
        if (auto car = constructor.RegisterStruct<ModelCar>()) {
            car.RegisterMember("index", &ModelCar::index);
            car.RegisterMember("number", &ModelCar::number);
            car.RegisterMember("tags", &ModelCar::tags);
            car.RegisterMember("kind", &ModelCar::kind);
            car.RegisterMember("name", &ModelCar::name);
        }
        constructor.RegisterArray<std::vector<ModelCar>>();
        if (auto row = constructor.RegisterStruct<ModelRow>()) {
            row.RegisterMember("time", &ModelRow::time);
            row.RegisterMember("id", &ModelRow::id);
            registerFields(row);
            row.RegisterMember("platform", &ModelRow::platform);
            row.RegisterMember("delay", &ModelRow::delay);
            row.RegisterMember("arrives", &ModelRow::arrives);
            row.RegisterMember("departs", &ModelRow::departs);
            row.RegisterMember("extra", &ModelRow::extra);
            row.RegisterMember("cars", &ModelRow::cars);
        }
        constructor.RegisterArray<std::vector<ModelRow>>();
        if (auto stop = constructor.RegisterStruct<ModelStop>()) {
            stop.RegisterMember("station", &ModelStop::station);
            stop.RegisterMember("platform", &ModelStop::platform);
            stop.RegisterMember("arrives", &ModelStop::arrives);
            stop.RegisterMember("departs", &ModelStop::departs);
            stop.RegisterMember("passed", &ModelStop::passed);
        }
        constructor.RegisterArray<std::vector<ModelStop>>();
        if (auto train = constructor.RegisterStruct<ModelTrain>()) {
            train.RegisterMember("id", &ModelTrain::id);
            registerFields(train);
            train.RegisterMember("state", &ModelTrain::state);
            train.RegisterMember("at", &ModelTrain::at);
            train.RegisterMember("next", &ModelTrain::next);
            train.RegisterMember("previous", &ModelTrain::previous);
            train.RegisterMember("platform", &ModelTrain::platform);
            train.RegisterMember("departs", &ModelTrain::departs);
            train.RegisterMember("arrives", &ModelTrain::arrives);
            train.RegisterMember("distance", &ModelTrain::distance);
            train.RegisterMember("delay", &ModelTrain::delay);
            train.RegisterMember("stops", &ModelTrain::stops);
            train.RegisterMember("extra", &ModelTrain::extra);
            train.RegisterMember("cars", &ModelTrain::cars);
        }
        Model &m = *model;
        registerDocumentFunctions(constructor);
        constructor.Bind("time", &m.time);
        constructor.Bind("clock", &m.clock);
        constructor.Bind("date", &m.date);
        constructor.Bind("hours", &m.hours);
        constructor.Bind("minutes", &m.minutes);
        constructor.Bind("seconds", &m.seconds);
        constructor.Bind("year", &m.year);
        constructor.Bind("month", &m.month);
        constructor.Bind("day", &m.day);
        constructor.Bind("station", &m.station);
        constructor.Bind("platform", &m.platform);
        constructor.Bind("departures", &m.departures);
        constructor.Bind("arrivals", &m.arrivals);
        constructor.Bind("trains", &m.trains);
        constructor.Bind("train", &m.train);
        constructor.Bind("car", &m.car);
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
