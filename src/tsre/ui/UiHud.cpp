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
#include <tsre/ui/UiRenderInterface.h>
#include <tsre/ui/UiRuntime.h>
#include <tsre/Game.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/script/ScriptState.h>
#include <tsre/units/Units.h>

#include <RmlUi/Core.h>
#include <QDebug>
#include <cmath>

namespace Ui {

// The context, its document and the data model `train`.
struct Hud::Model {
    Rml::Context *context = nullptr;
    Rml::ElementDocument *document = nullptr;
    Rml::DataModelHandle handle;
    Rml::String speed, speedUnit, distance;
    int throttle = 0, brake = 0;
    QString name;
};

namespace {
int nextContext = 1;

// TSRE_UI_LOG: the CPU time of the HUD's parts, every 5 s in the log.
struct Timing {
    // The data and the event (its Lua), the update, RmlUi's rendering
    // into the draw list, the renderer's drawer.
    enum Part { Event, Update, Render, Draw, Parts };
    qint64 total[Parts] = {};
    qint64 most[Parts] = {};
    int frames = 0;
    QElapsedTimer since;
    void add(const qint64 *ns) {
        for (int i = 0; i < Parts; ++i) {
            total[i] += ns[i];
            most[i] = std::max(most[i], ns[i]);
        }
        ++frames;
        if (!since.isValid())
            since.start();
        if (since.elapsed() < 5000)
            return;
        static const char *const names[Parts] = {"event", "update", "render", "draw"};
        QString line = QString("[ui] HUD CPU over %1 frames, mean/max ms:").arg(frames);
        qint64 sum = 0;
        for (int i = 0; i < Parts; ++i) {
            line += QString(" %1 %2/%3").arg(names[i]).arg(total[i] / 1e6 / frames, 0, 'f', 3).arg(most[i] / 1e6, 0, 'f', 3);
            sum += total[i];
        }
        line += QString(", all %1").arg(sum / 1e6 / frames, 0, 'f', 3);
        qInfo().noquote() << line;
        *this = Timing();
        since.start();
    }
};

// Sets a bound value, marking it changed only when it is.
template <typename T>
void set(Rml::DataModelHandle &handle, T &bound, const T &value, const char *name) {
    if (bound == value)
        return;
    bound = value;
    handle.DirtyVariable(name);
}
}

Hud::Hud() : renderInterface(new RenderInterface) {
    clock.start();
}

Hud::~Hud() {
    // RmlUi lets go of the render interface before it goes.
    if (model && model->context != nullptr && Runtime::get() != nullptr) {
        Rml::RemoveContext(model->name.toStdString());
        Rml::ReleaseRenderManagers();
    }
}

bool Hud::load(int width, int height) {
    const QString folder = QString("appdata/") + Game::AppDataVersion + "/ui";
    if (!Runtime::start(folder))
        return false;
    model.reset(new Model);
    model->name = QString("hud%1").arg(nextContext++);
    model->context = Rml::CreateContext(model->name.toStdString(), Rml::Vector2i(width, height), renderInterface.get());
    if (model->context == nullptr)
        return false;
    if (Rml::DataModelConstructor constructor = model->context->CreateDataModel("train")) {
        constructor.Bind("speed", &model->speed);
        constructor.Bind("speedUnit", &model->speedUnit);
        constructor.Bind("distance", &model->distance);
        constructor.Bind("throttle", &model->throttle);
        constructor.Bind("brake", &model->brake);
        model->handle = constructor.GetModelHandle();
    }
    const QString path = Runtime::get()->folder() + "/hud.rml";
    Script::State::Deadline deadline(Runtime::get()->lua());
    model->document = model->context->LoadDocument(path.toStdString());
    if (model->document == nullptr) {
        qWarning().noquote() << "[ui] the HUD document did not load:" << path;
        return false;
    }
    model->document->Show();
    return true;
}

bool Hud::draw(Renderer *renderer, int width, int height, float scale, const Values &values) {
    if (failed || renderer == nullptr || width < 1 || height < 1)
        return false;
    if (!model && !load(width, height)) {
        failed = true;
        return false;
    }
    static const bool logged = qEnvironmentVariableIsSet("TSRE_UI_LOG");
    static Timing timing;
    QElapsedTimer part;
    qint64 ns[Timing::Parts] = {};
    if (logged)
        part.start();
    Rml::Context *context = model->context;
    if (context->GetDimensions() != Rml::Vector2i(width, height))
        context->SetDimensions(Rml::Vector2i(width, height));
    if (std::fabs(context->GetDensityIndependentPixelRatio() - scale) > 1e-3f)
        context->SetDensityIndependentPixelRatio(scale);

    set(model->handle, model->speed, Units::formatNumber(values.speed, Units::Quantity::Speed, 0).toStdString(), "speed");
    set(model->handle, model->speedUnit, Units::unit(Units::Quantity::Speed).toStdString(), "speedUnit");
    set(model->handle, model->distance, Units::format(values.distance, Units::Quantity::Distance, 1).toStdString(),
        "distance");
    set(model->handle, model->throttle, int(std::lround(values.throttle * 100)), "throttle");
    set(model->handle, model->brake, int(std::lround(values.brake * 100)), "brake");
    {
        // The document's Lua: one deadline for the event, one for the
        // update (a handler past its time fails only its own).
        Script::State &lua = Runtime::get()->lua();
        {
            Script::State::Deadline deadline(lua);
            Rml::Dictionary parameters;
            parameters["speed"] = values.speed;
            parameters["distance"] = values.distance;
            parameters["throttle"] = values.throttle;
            parameters["brake"] = values.brake;
            parameters["time"] = double(clock.nsecsElapsed()) * 1e-9;
            model->document->DispatchEvent("trainupdate", parameters);
        }
        if (logged)
            ns[Timing::Event] = part.nsecsElapsed();
        Script::State::Deadline deadline(lua);
        context->Update();
        if (logged)
            ns[Timing::Update] = part.nsecsElapsed() - ns[Timing::Event];
    }
    renderInterface->beginFrame(width, height);
    context->Render();
    if (logged)
        ns[Timing::Render] = part.nsecsElapsed() - ns[Timing::Event] - ns[Timing::Update];
    renderer->drawUi(renderInterface->list());
    if (logged) {
        ns[Timing::Draw] = part.nsecsElapsed() - ns[Timing::Event] - ns[Timing::Update] - ns[Timing::Render];
        timing.add(ns);
    }
    return true;
}

}
