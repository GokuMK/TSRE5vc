/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/ui/UiLocoControls.h>
#include <tsre/ui/UiRuntime.h>
#include <tsre/ui/UiScreen.h>
#include <tsre/script/LuaApi.h>
#include <tsre/script/ScriptState.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/units/Units.h>

#include <RmlUi/Core.h>
#include <QDebug>
#include <QElapsedTimer>

namespace Ui {

struct LocoControls::Document {
    Rml::ElementDocument *document = nullptr;
};

namespace {

// The values TSRE shows in the user's units.
Units::Quantity quantityOf(const QString &name) {
    static const QHash<QString, Units::Quantity> known = {
            {"speed", Units::Quantity::Speed},
            {"distance", Units::Quantity::Distance},
            {"lineVoltage", Units::Quantity::Voltage},
            {"filterVoltage", Units::Quantity::Voltage},
            {"mainReservoir", Units::Quantity::Pressure},
            {"equalisingReservoir", Units::Quantity::Pressure},
            {"brakePipe", Units::Quantity::Pressure},
            {"brakeCylinder", Units::Quantity::Pressure},
            {"engineBrakeLine", Units::Quantity::Pressure},
            {"force", Units::Quantity::Force},
            {"tractionForce", Units::Quantity::Force},
            {"dynamicBrakeForce", Units::Quantity::Force},
            {"ammeter", Units::Quantity::Current},
            {"fuel", Units::Quantity::Volume},
            {"oilPressure", Units::Quantity::Pressure},
            {"engineTemperature", Units::Quantity::Temperature},
            {"enginePower", Units::Quantity::Power},
            {"availablePower", Units::Quantity::Power},
            // The steam heating's (task driving 06).
            {"steamHeatPressure", Units::Quantity::Pressure},
            {"heatingEnd", Units::Quantity::Pressure},
            {"heatingPipe", Units::Quantity::Pressure},
            {"heatingBoilerPressure", Units::Quantity::Pressure},
            {"heatingBoilerWater", Units::Quantity::Mass},
            {"heatingBoilerFuel", Units::Quantity::Volume},
            {"airTemperature", Units::Quantity::Temperature},
            {"coldestCompartment", Units::Quantity::Temperature},
            {"compartmentTemperature", Units::Quantity::Temperature},
    };
    return known.value(name, Units::Quantity::Any);
}

QString textOf(const QString &name, const Sim::MessageValue &value) {
    const Units::Quantity quantity = quantityOf(name);
    if (const double *d = std::get_if<double>(&value)) {
        // The speed and distance as the simple HUD shows them.
        if (quantity != Units::Quantity::Any)
            return Units::format(*d, quantity, name == "speed" ? 0 : name == "distance" ? 1 : -1);
        return QString::number(*d, 'g', 4);
    }
    return Sim::Message{name, value}.text();
}

void pushText(lua_State *L, const QString &text) {
    const QByteArray utf8 = text.toUtf8();
    lua_pushlstring(L, utf8.constData(), size_t(utf8.size()));
}

void pushValue(lua_State *L, const Sim::MessageValue &value) {
    if (const bool *b = std::get_if<bool>(&value))
        lua_pushboolean(L, *b);
    else if (const double *d = std::get_if<double>(&value))
        lua_pushnumber(L, *d);
    else
        pushText(L, std::get<QString>(value));
}

} // namespace

LocoControls::LocoControls(Screen &screen) : screen(screen) {
    Sim::SimulationController *simulation = Sim::SimulationController::instance();
    sink.command = [simulation](const QString &name, const Sim::MessageValue &value) {
        simulation->command(name, value);
    };
    sink.leave = [simulation] { simulation->leave(); };
}

LocoControls::~LocoControls() {
    if (!doc || Runtime::get() == nullptr)
        return;
    if (doc->document != nullptr)
        doc->document->Close();
    // Its functions gone with it.
    lua_State *L = Runtime::get()->lua().lua();
    lua_pushnil(L);
    lua_setglobal(L, "train");
}

int LocoControls::command(lua_State *L) {
    auto *self = static_cast<LocoControls *>(lua_touserdata(L, lua_upvalueindex(1)));
    const QString name = QString::fromUtf8(luaL_checkstring(L, 1));
    Sim::MessageValue value = true;
    switch (lua_type(L, 2)) {
    case LUA_TNUMBER: value = double(lua_tonumber(L, 2)); break;
    case LUA_TBOOLEAN: value = bool(lua_toboolean(L, 2)); break;
    case LUA_TSTRING: value = QString::fromUtf8(lua_tostring(L, 2)); break;
    default: break;
    }
    if (self->sink.command)
        self->sink.command(name, value);
    return 0;
}

int LocoControls::leave(lua_State *L) {
    auto *self = static_cast<LocoControls *>(lua_touserdata(L, lua_upvalueindex(1)));
    if (self->sink.leave)
        self->sink.leave();
    return 0;
}

bool LocoControls::load() {
    Rml::Context *context = screen.context();
    if (context == nullptr || Runtime::get() == nullptr)
        return false;
    doc.reset(new Document);
    // The `train` table its Lua reads.
    lua_State *L = Runtime::get()->lua().lua();
    lua_createtable(L, 0, 4);
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &LocoControls::command, 1);
    lua_setfield(L, -2, "command");
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &LocoControls::leave, 1);
    lua_setfield(L, -2, "leave");
    lua_newtable(L);
    lua_setfield(L, -2, "values");
    lua_newtable(L);
    lua_setfield(L, -2, "texts");
    lua_setglobal(L, "train");
    const QString path = Runtime::get()->folder() + "/loco.rml";
    Script::State::Deadline deadline(Runtime::get()->lua());
    doc->document = context->LoadDocument(path.toStdString());
    if (doc->document == nullptr) {
        qWarning().noquote() << "[ui] the loco controls did not load:" << path;
        return false;
    }
    return true;
}

bool LocoControls::update(const Values &values, double speed, double distance,
                          const std::vector<std::pair<int, Sim::Message>> &messages) {
    if (failed)
        return false;
    if (!doc && !load()) {
        failed = true;
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    if (!doc->document->IsVisible())
        doc->document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    lua_State *L = Runtime::get()->lua().lua();
    lua_getglobal(L, "train");
    if (lua_istable(L, -1)) {
        // The values and their texts, new tables each frame: a value the
        // model stopped publishing goes.
        lua_createtable(L, 0, int(values.size()) + 1);
        lua_createtable(L, 0, int(values.size()) + 1);
        const auto set = [&](const QString &name, const Sim::MessageValue &value) {
            const QByteArray key = name.toUtf8();
            pushValue(L, value);
            lua_setfield(L, -3, key.constData());
            pushText(L, textOf(name, value));
            lua_setfield(L, -2, key.constData());
        };
        for (auto i = values.cbegin(); i != values.cend(); ++i)
            set(i.key(), i.value());
        set("speed", std::abs(speed));
        set("distance", distance);
        lua_setfield(L, -3, "texts");
        lua_setfield(L, -2, "values");
    }
    lua_pop(L, 1);
    {
        Script::State::Deadline deadline(Runtime::get()->lua());
        doc->document->DispatchEvent("trainupdate", Rml::Dictionary());
        for (const auto &[vehicle, m] : messages) {
            Rml::Dictionary parameters;
            parameters["name"] = m.name.toStdString();
            parameters["value"] = m.text().toStdString();
            parameters["vehicle"] = vehicle;
            doc->document->DispatchEvent("trainmessage", parameters);
        }
    }
    screen.measured(Screen::Part::Event, timer.nsecsElapsed());
    return true;
}

void LocoControls::hide() {
    if (doc && doc->document != nullptr && doc->document->IsVisible())
        doc->document->Hide();
}

bool LocoControls::visible() const {
    return doc && doc->document != nullptr && doc->document->IsVisible();
}

}
