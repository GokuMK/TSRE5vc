/*  This file is part of TSRE5 (task driving 10, step 5).
 *
 *  It is a port of parts of MaSzyna's TMoverParameters (McZapkie/Mover.cpp
 *  at 66a940d4, https://github.com/eu07/maszyna: the brake handles' keys in
 *  LoadFIZ_Brake and LoadFIZ_Cntrl, their creation in
 *  CheckLocomotiveParameters, the driver's part of UpdatePipePressure), and
 *  so is under the Mozilla Public License 2.0, as MaSzyna's code is: this
 *  Source Code Form is subject to the terms of the Mozilla Public License,
 *  v. 2.0. If a copy of the MPL was not distributed with this file, You can
 *  obtain one at http://mozilla.org/MPL/2.0/.
 *
 *  MaSzyna's names, order and units inside (bar, litres, bar·l of air); SI
 *  at the edge. Not MaSzyna's: the handle's air from the supply all from
 *  the main reservoir (MaSzyna takes two thirds from it and a third from the
 *  loco's main pipe while the compressor's reservoir is above the pipe), the
 *  train's mean pipe pressure (EqvtPipePress, which none of these handles
 *  reads) the loco's own, the brake operating modes (BrakeOpModes) as
 *  PN, the speed control's brake intervention left to step 6.
 */

#include <tsre/maszyna/MaszynaHandle.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <new>
#include <utility>
#include <tsre/maszyna/MaszynaFiz.h>
#include <tsre/maszyna/MaszynaHamulce.h>
#include <tsre/maszyna/MaszynaVehicle.h>
#include <tsre/script/LuaApi.h>
#include <tsre/sim/LocoScript.h>
#include <tsre/trains/VehicleDefinition.h>

using namespace MaszynaHamulce;

namespace MaszynaHandle {

namespace {

constexpr double Bar = 100000.0;
// The longest step of MaSzyna's physics (drivermode.h: 10 ms).
constexpr double MaxStep = 0.01;

// What a loco's handles are, from its .fiz.
struct Config {
    enum class Handle { None, FV4a, MHZ_EN57, FVel6, FVE408, M394, Knorr, St113, MHZ_K5P, MHZ_6P };
    enum class Local { None, FD1, Knorr };
    Handle handle = Handle::None;
    Local local = Local::None;
    QString name, localName;
    int BrakeCtrlPosNo = 0;
    // SetParams.
    bool Handle_AutomaticOverload = false, Handle_ManualOverload = false;
    double Handle_GenericDoubleParameter1 = 0.0, Handle_GenericDoubleParameter2 = 0.0;
    double Handle_OverloadMaxPressure = 1.0, Handle_OverloadPressureDecrease = 0.002;
    // Init: the pipe's ready pressure (CntrlPipePress), the local brake's
    // most (MaxBrakePress[0]).
    double CntrlPipePress = 5.0, MaxLocalBrakePress = 0.0;
    bool ezt = false;
    // The pipe's lock.
    double LockPipeOn = -1.0, LockPipeOff = -1.0, HandleUnlock = -3.0;
    bool EmergencyCutsOffHandle = false;
    bool subsystemLSt = false;
};

bool yes(const MaszynaFiz::Section *s, const char16_t *key) {
    return s != nullptr && s->text(QStringView(key)).trimmed().toLower() == QLatin1String("yes");
}
// The handle Mover.cpp makes for a BrakeHandle name.
Config::Handle handleOf(const QString &name) {
    static const std::pair<const char *, Config::Handle> handles[] = {
        {"FV4a", Config::Handle::FV4a},       {"MHZ_EN57", Config::Handle::MHZ_EN57}, {"MHZ_K8P", Config::Handle::MHZ_EN57},
        {"FVel6", Config::Handle::FVel6},     {"FVE408", Config::Handle::FVE408},     {"M394", Config::Handle::M394},
        {"Knorr", Config::Handle::Knorr},     {"St113", Config::Handle::St113},       {"MHZ_K5P", Config::Handle::MHZ_K5P},
        {"MHZ_6P", Config::Handle::MHZ_6P}};
    for (const auto &[written, kind] : handles)
        if (name == QLatin1String(written))
            return kind;
    return Config::Handle::None;
}

double num(const MaszynaFiz::Section *s, const char16_t *key, double otherwise = 0.0) {
    return s != nullptr ? s->number(QStringView(key), otherwise) : otherwise;
}

std::shared_ptr<const Config> readConfig(const VehicleDefinition &definition) {
    auto c = std::make_shared<Config>();
    if (definition.maszyna == nullptr)
        return c;
    const MaszynaFiz &f = definition.maszyna->physics();
    const MaszynaFiz::Section *brake = f.section(u"Brake"), *cntrl = f.section(u"Cntrl"), *param = f.section(u"Param");
    // LoadFIZ_Cntrl (BrakeSystem other than Individual).
    const QString system = cntrl ? cntrl->text(u"BrakeSystem") : QString();
    if (system != QLatin1String("Pneumatic") && system != QLatin1String("ElectroPneumatic"))
        return c;
    c->BrakeCtrlPosNo = int(num(cntrl, u"BCPN"));
    c->name = cntrl->text(u"BrakeHandle");
    c->handle = handleOf(c->name);
    c->localName = cntrl->text(u"LocBrakeHandle");
    if (c->localName == QLatin1String("FD1"))
        c->local = Config::Local::FD1;
    else if (c->localName == QLatin1String("Knorr"))
        c->local = Config::Local::Knorr;
    c->Handle_AutomaticOverload = yes(cntrl, u"HAO");
    c->Handle_ManualOverload = yes(cntrl, u"HMO");
    c->Handle_GenericDoubleParameter1 = num(cntrl, u"HGDP1");
    c->Handle_GenericDoubleParameter2 = num(cntrl, u"HGDP2");
    c->Handle_OverloadMaxPressure = num(cntrl, u"OMP", 1.0);
    c->Handle_OverloadPressureDecrease = num(cntrl, u"OPD", 0.002);
    // LoadFIZ_Brake.
    const double maxBP = num(brake, u"MaxBP") / Bar;
    if (maxBP > 0.0 && num(brake, u"BCN") > 0) {
        c->MaxLocalBrakePress = num(brake, u"MaxLBP") / Bar;
        if (c->MaxLocalBrakePress < 0.01)
            c->MaxLocalBrakePress = maxBP;
    }
    c->CntrlPipePress = num(brake, u"HiPP", 5.0 * Bar) / Bar;
    c->LockPipeOn = num(brake, u"LPOn", -1.0);
    c->LockPipeOff = num(brake, u"LPOff", -1.0);
    c->HandleUnlock = num(brake, u"HandlePipeUnlockPos", -3.0);
    c->EmergencyCutsOffHandle = yes(brake, u"EmergencyCutsOffHandle");
    // BrakeSubsystemDecode's ss_LSt.
    const QString valve = brake ? brake->text(u"BrakeValve") : QString();
    c->subsystemLSt = valve == QLatin1String("LSt") || valve == QLatin1String("EStED");
    c->ezt = param != nullptr && param->text(u"Type").trimmed().toLower() == QLatin1String("ezt");
    return c;
}

// The handles as CheckLocomotiveParameters makes them, Init.
std::unique_ptr<TDriverHandle> makeHandle(const Config &c) {
    std::unique_ptr<TDriverHandle> h;
    switch (c.handle) {
    case Config::Handle::FV4a: h = std::make_unique<TFV4aM>(); break;
    case Config::Handle::MHZ_EN57: h = std::make_unique<TMHZ_EN57>(); break;
    case Config::Handle::FVel6: h = std::make_unique<TFVel6>(); break;
    case Config::Handle::FVE408: h = std::make_unique<TFVE408>(); break;
    case Config::Handle::M394: h = std::make_unique<TM394>(); break;
    case Config::Handle::Knorr: h = std::make_unique<TH14K1>(); break;
    case Config::Handle::St113: h = std::make_unique<TSt113>(); break;
    case Config::Handle::MHZ_K5P: h = std::make_unique<TMHZ_K5P>(); break;
    case Config::Handle::MHZ_6P: h = std::make_unique<TMHZ_6P>(); break;
    case Config::Handle::None: h = std::make_unique<TDriverHandle>(); break;
    }
    h->SetParams(c.Handle_AutomaticOverload, c.Handle_ManualOverload, c.Handle_GenericDoubleParameter1,
                 c.Handle_GenericDoubleParameter2, c.Handle_OverloadMaxPressure, c.Handle_OverloadPressureDecrease);
    h->Init(c.CntrlPipePress);
    return h;
}

std::unique_ptr<TDriverHandle> makeLocal(const Config &c) {
    switch (c.local) {
    case Config::Local::FD1: {
        auto h = std::make_unique<TFD1>();
        h->Init(c.MaxLocalBrakePress);
        if (c.ezt)
            h->SetSpeed(3.5);
        return h;
    }
    case Config::Local::Knorr: {
        auto h = std::make_unique<TH1405>();
        h->Init(c.MaxLocalBrakePress);
        return h;
    }
    case Config::Local::None:
        break;
    }
    return std::make_unique<TDriverHandle>();
}

const char *ConfigMeta = "maszyna.handle.config";
using ConfigPointer = std::shared_ptr<const Config>;

int releaseConfig(lua_State *L) {
    static_cast<ConfigPointer *>(luaL_checkudata(L, 1, ConfigMeta))->~ConfigPointer();
    return 0;
}

// The config of the definition at index 1, read once per definition and
// kept in the functions' first upvalue (as maszyna.diesel's).
const Config &configAt(lua_State *L) {
    lua_pushvalue(L, 1);
    lua_rawget(L, lua_upvalueindex(1));
    if (lua_type(L, -1) == LUA_TUSERDATA) {
        const Config &c = **static_cast<ConfigPointer *>(lua_touserdata(L, -1));
        lua_pop(L, 1);
        return c;
    }
    lua_pop(L, 1);
    ConfigPointer config = readConfig(Sim::LocoScripts::definitionAt(L, 1));
    auto *slot = static_cast<ConfigPointer *>(lua_newuserdatauv(L, sizeof(ConfigPointer), 0));
    new (slot) ConfigPointer(std::move(config));
    if (luaL_newmetatable(L, ConfigMeta)) {
        lua_pushcfunction(L, releaseConfig);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    lua_pushvalue(L, 1);
    lua_pushvalue(L, -2);
    lua_rawset(L, lua_upvalueindex(1));
    const Config &c = **slot;
    lua_pop(L, 1);
    return c;
}

double number(lua_State *L, int table, const char *name, double otherwise) {
    lua_getfield(L, table, name);
    int ok = 0;
    const double value = lua_tonumberx(L, -1, &ok);
    lua_pop(L, 1);
    return ok && std::isfinite(value) ? value : otherwise;
}

bool flag(lua_State *L, int table, const char *name, bool otherwise = false) {
    lua_getfield(L, table, name);
    const bool value = lua_isnil(L, -1) ? otherwise : lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return value;
}

bool ported(const Config &c) {
    return c.handle != Config::Handle::None && c.BrakeCtrlPosNo > 1;
}

// A handle's positions by function (GetPos), as a table.
void pushPositions(lua_State *L, TDriverHandle &h) {
    lua_createtable(L, 0, 11);
    const char *names[] = {"min", "max", "fs", "rp", "np", "mb", "fb", "eb", "epr", "epn", "epb"};
    for (int i = 0; i <= bh_EPB; ++i) {
        lua_pushnumber(L, h.GetPos(i));
        lua_setfield(L, -2, names[i]);
    }
}

// handle.positions(name): a BrakeHandle's positions, nil for one not
// ported.
int positions(lua_State *L) {
    Config c;
    c.handle = handleOf(QString::fromUtf8(luaL_optstring(L, 1, "")));
    if (c.handle == Config::Handle::None) {
        lua_pushnil(L);
        return 1;
    }
    pushPositions(L, *makeHandle(c));
    return 1;
}

// handle.config(vehicle)
int config(lua_State *L) {
    const Config &c = configAt(L);
    if (!ported(c)) {
        lua_pushnil(L);
        return 1;
    }
    const std::unique_ptr<TDriverHandle> h = makeHandle(c);
    lua_createtable(L, 0, 4);
    lua_pushstring(L, c.name.toUtf8().constData());
    lua_setfield(L, -2, "name");
    if (c.local != Config::Local::None) {
        lua_pushstring(L, c.localName.toUtf8().constData());
        lua_setfield(L, -2, "localName");
    }
    lua_pushboolean(L, c.handle == Config::Handle::FVel6 || c.handle == Config::Handle::FVE408
                               || c.handle == Config::Handle::St113);
    lua_setfield(L, -2, "ep");
    pushPositions(L, *h);
    lua_setfield(L, -2, "positions");
    return 1;
}

// The state: the handle's (Exchange), the local handle's, LockPipe, the
// emergency valve's last flow.
constexpr int Count = 2 * TDriverHandle::StateCount + 2;

// handle.step(vehicle, dt, state, controls)
int step(lua_State *L) {
    const Config &c = configAt(L);
    const double dt = luaL_checknumber(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    luaL_checktype(L, 4, LUA_TTABLE);
    const double BrakeCtrlPosR = number(L, 4, "position", 0.0);
    double PipePress = number(L, 4, "pipe", 0.0) / Bar;
    const double capacity = std::max(number(L, 4, "volume", 0.0) * 1000.0, 0.001);
    const double ScndPipePress = number(L, 4, "supply", 0.0) / Bar;
    const double LocalBrakePosA = std::clamp(number(L, 4, "localPosition", 0.0), 0.0, 1.0);
    const double BrakePress = number(L, 4, "cylinder", 0.0) / Bar;
    const bool releaser = flag(L, 4, "releaser");
    const bool emergency = flag(L, 4, "emergency");
    const double BrakeCtrlPos2 = number(L, 4, "reductor", 0.0);
    const int universal = int(number(L, 4, "universal", 0.0));
    const bool epPowered = flag(L, 4, "epPowered", true);
    // The driver's handle worked (the cab's loco), else the local handle only
    // (TSRE: a loco no driver occupies leaves the pipe to the cab's).
    const bool main = flag(L, 4, "main", true);

    std::unique_ptr<TDriverHandle> Handle = makeHandle(c);
    std::unique_ptr<TDriverHandle> LocHandle = makeLocal(c);
    double s[Count] = {};
    const bool started = int(lua_rawlen(L, 3)) == Count;
    if (started) {
        for (int i = 0; i < Count; ++i) {
            lua_rawgeti(L, 3, i + 1);
            s[i] = lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
        Handle->Exchange(s, false);
        LocHandle->Exchange(s + TDriverHandle::StateCount, false);
    }
    bool LockPipe = s[2 * TDriverHandle::StateCount] != 0.0;
    double EmergencyValveFlow = s[2 * TDriverHandle::StateCount + 1];
    Handle->SetUniversalFlag(universal);
    LocHandle->SetUniversalFlag(universal);
    const bool UnlockPipe = (universal & ub_UnlockPipe) > 0;

    // UpdatePipePressure's driver's part, in MaSzyna's steps.
    double pipeAir = 0.0, supplyAir = 0.0;
    const int steps = std::max(1, int(std::ceil(dt / MaxStep - 1e-9)));
    const double h = dt / steps;
    for (int i = 0; i < steps && ported(c); ++i) {
        LocHandle->GetPF(LocalBrakePosA, BrakePress, ScndPipePress, h, 0);
        if (!main)
            continue;
        LockPipe = PipePress < (LockPipe ? c.LockPipeOff : c.LockPipeOn);
        const bool lock_new = (LockPipe && !UnlockPipe && BrakeCtrlPosR > c.HandleUnlock)
                || (c.EmergencyCutsOffHandle && EmergencyValveFlow > 0);
        const bool lock_old = c.handle == Config::Handle::FV4a && PipePress < 2.75 && !releaser && c.subsystemLSt
                && !c.ezt && !UnlockPipe;
        const double temp = lock_old || lock_new ? PipePress + 0.00001 : ScndPipePress;
        Handle->SetReductor(BrakeCtrlPos2);
        double dpMainValve = Handle->GetPF(BrakeCtrlPosR, PipePress, temp, h, PipePress);
        if (dpMainValve < 0)
            supplyAir -= dpMainValve;
        // ulepszony hamulec bezp.
        EmergencyValveFlow = emergency ? PF(0, PipePress, 0.15) * h : 0.0;
        dpMainValve += EmergencyValveFlow;
        PipePress = std::max(0.0, PipePress - dpMainValve / capacity);
        pipeAir -= dpMainValve;
    }

    Handle->Exchange(s, true);
    LocHandle->Exchange(s + TDriverHandle::StateCount, true);
    s[2 * TDriverHandle::StateCount] = LockPipe ? 1.0 : 0.0;
    s[2 * TDriverHandle::StateCount + 1] = EmergencyValveFlow;
    for (int i = 0; i < Count; ++i) {
        lua_pushnumber(L, std::isfinite(s[i]) ? s[i] : 0.0);
        lua_rawseti(L, 3, i + 1);
    }

    // The EP signal (EpForce): FVel6's and FVE408's with 24 V and a
    // direction; as TSRE's EP wire (MaszynaBrake turns it back into SetEPS):
    // FVel6's +1 apply 1, 0 hold -2, -1 release 0; FVE408's steps (EP1's
    // fraction) their fraction.
    double eps = 0.0, epDemand = -1.0;
    if (c.handle == Config::Handle::FVel6 || c.handle == Config::Handle::FVE408) {
        eps = epPowered ? Handle->GetEP() : 0.0;
        if (c.handle == Config::Handle::FVel6)
            epDemand = eps > 0.0 ? 1.0 : eps < 0.0 ? 0.0 : -2.0;
        else
            epDemand = eps - std::floor(eps);
    }
    double control = Handle->GetCP();
    if (const auto *fv4a = dynamic_cast<const TFV4aM *>(Handle.get()))
        control = fv4a->ControlPressure();
    else if (const auto *en57 = dynamic_cast<const TMHZ_EN57 *>(Handle.get()))
        control = en57->ControlPressure();

    // bar·l to Pa·m³.
    lua_pushnumber(L, std::isfinite(pipeAir) ? pipeAir * 100.0 : 0.0);
    lua_createtable(L, 0, 6);
    const std::pair<const char *, double> values[] = {
            {"supply", supplyAir * 100.0},         {"control", control * Bar},
            {"gauge", Handle->GetCP() * Bar},      {"localPressure", LocHandle->GetCP() * Bar},
            {"eps", eps},                          {"epDemand", epDemand}};
    for (const auto &[name, value] : values) {
        lua_pushnumber(L, std::isfinite(value) ? value : 0.0);
        lua_setfield(L, -2, name);
    }
    return 2;
}

} // namespace

int openModule(lua_State *L) {
    lua_createtable(L, 0, 3);
    // The configs by definition, shared by its functions.
    lua_newtable(L);
    const std::pair<const char *, lua_CFunction> functions[] = {{"positions", positions}, {"config", config}, {"step", step}};
    for (const auto &[name, function] : functions) {
        lua_pushvalue(L, -1);
        lua_pushcclosure(L, function, 1);
        lua_setfield(L, -3, name);
    }
    lua_pop(L, 1);
    return 1;
}

} // namespace MaszynaHandle
