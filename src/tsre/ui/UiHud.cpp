/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/ui/UiHud.h>
#include <tsre/ui/UiRuntime.h>
#include <tsre/ui/UiScreen.h>
#include <tsre/script/ScriptState.h>
#include <tsre/units/Units.h>

#include <RmlUi/Core.h>
#include <QDebug>
#include <cmath>

namespace Ui {

// Its document and the data model `train`.
struct Hud::Model {
    Rml::ElementDocument *document = nullptr;
    Rml::DataModelHandle handle;
    Rml::String speed, speedUnit, distance;
    int throttle = 0, brake = 0;
};

namespace {
// Sets a bound value, marking it changed only when it is.
template <typename T>
void set(Rml::DataModelHandle &handle, T &bound, const T &value, const char *name) {
    if (bound == value)
        return;
    bound = value;
    handle.DirtyVariable(name);
}
}

Hud::Hud(Screen &screen) : screen(screen) {
    clock.start();
}

Hud::~Hud() {
    Rml::Context *context = screen.context();
    if (!model || context == nullptr || Runtime::get() == nullptr)
        return;
    if (model->document != nullptr)
        model->document->Close();
    context->RemoveDataModel("train");
}

bool Hud::load() {
    Rml::Context *context = screen.context();
    if (context == nullptr)
        return false;
    model.reset(new Model);
    if (Rml::DataModelConstructor constructor = context->CreateDataModel("train")) {
        constructor.Bind("speed", &model->speed);
        constructor.Bind("speedUnit", &model->speedUnit);
        constructor.Bind("distance", &model->distance);
        constructor.Bind("throttle", &model->throttle);
        constructor.Bind("brake", &model->brake);
        model->handle = constructor.GetModelHandle();
    }
    const QString path = Runtime::get()->folder() + "/hud.rml";
    Script::State::Deadline deadline(Runtime::get()->lua());
    model->document = context->LoadDocument(path.toStdString());
    if (model->document == nullptr) {
        qWarning().noquote() << "[ui] the HUD document did not load:" << path;
        return false;
    }
    return true;
}

bool Hud::update(const Values &values) {
    if (failed)
        return false;
    if (!model && !load()) {
        failed = true;
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    if (!model->document->IsVisible())
        model->document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    set(model->handle, model->speed, Units::formatNumber(values.speed, Units::Quantity::Speed, 0).toStdString(),
        "speed");
    set(model->handle, model->speedUnit, Units::unit(Units::Quantity::Speed).toStdString(), "speedUnit");
    set(model->handle, model->distance, Units::format(values.distance, Units::Quantity::Distance, 1).toStdString(),
        "distance");
    set(model->handle, model->throttle, int(std::lround(values.throttle * 100)), "throttle");
    set(model->handle, model->brake, int(std::lround(values.brake * 100)), "brake");
    {
        // The document's Lua, under a deadline of its own.
        Script::State::Deadline deadline(Runtime::get()->lua());
        Rml::Dictionary parameters;
        parameters["speed"] = values.speed;
        parameters["distance"] = values.distance;
        parameters["throttle"] = values.throttle;
        parameters["brake"] = values.brake;
        parameters["time"] = double(clock.nsecsElapsed()) * 1e-9;
        model->document->DispatchEvent("trainupdate", parameters);
    }
    screen.measured(Screen::Part::Event, timer.nsecsElapsed());
    return true;
}

void Hud::hide() {
    if (model && model->document != nullptr && model->document->IsVisible())
        model->document->Hide();
}

}
