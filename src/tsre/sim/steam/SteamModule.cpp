#include <tsre/sim/steam/SteamModule.h>
#include <cmath>
#include <memory>
#include <new>
#include <utility>
#include <vector>
#include <tsre/script/LuaApi.h>
#include <tsre/sim/LocoScript.h>
#include <tsre/sim/steam/SteamCore.h>

namespace Sim::Steam {

namespace {

constexpr double PsiPa = 6894.757293168;
constexpr double LbfN = 1.0 / 0.224808943871;
constexpr double HpW = 745.699872;

const char *ConfigMeta = "tsre.steam.config";

using ConfigPointer = std::shared_ptr<const Config>;

int releaseConfig(lua_State *L) {
    static_cast<ConfigPointer *>(luaL_checkudata(L, 1, ConfigMeta))->~ConfigPointer();
    return 0;
}

// The config of the definition at index 1, read once per definition and
// kept in the functions' first upvalue (configs by definition).
const Config &configAt(lua_State *L) {
    lua_pushvalue(L, 1);
    lua_rawget(L, lua_upvalueindex(1));
    if (lua_type(L, -1) == LUA_TUSERDATA) {
        const Config &c = **static_cast<ConfigPointer *>(lua_touserdata(L, -1));
        lua_pop(L, 1);
        return c;
    }
    lua_pop(L, 1);
    ConfigPointer config = readConfig(LocoScripts::definitionAt(L, 1));
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

// steam.step(vehicle, dt, state, controls, inputs, values)
int step(lua_State *L) {
    const Config &c = configAt(L);
    const double dt = luaL_checknumber(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    luaL_checktype(L, 4, LUA_TTABLE);
    luaL_checktype(L, 5, LUA_TTABLE);
    const int engines = int(c.engines.size());
    const int size = State::LocoCount + engines * State::EngineCount;
    thread_local State s;
    thread_local std::vector<double> numbers;
    numbers.resize(size_t(size));
    // A list of another size (none yet, or another version's) starts anew.
    const int had = int(lua_rawlen(L, 3));
    if (had == size) {
        for (int i = 0; i < size; ++i) {
            lua_rawgeti(L, 3, i + 1);
            numbers[size_t(i)] = lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
        s.read(numbers.data(), engines);
    } else {
        start(c, s);
    }

    Controls k;
    k.regulator = number(L, 4, "regulator", 0.0);
    k.reverser = number(L, 4, "reverser", 0.0);
    k.injector1 = number(L, 4, "injector1", 0.0);
    k.injector2 = number(L, 4, "injector2", 0.0);
    k.blower = number(L, 4, "blower", 0.0);
    k.damper = number(L, 4, "damper", 0.0);
    k.firingRate = number(L, 4, "firingRate", 0.0);
    k.manualFiring = flag(L, 4, "manualFiring");
    k.cylinderCocks = flag(L, 4, "cylinderCocks");
    k.blowdown = flag(L, 4, "blowdown");
    k.compressor = flag(L, 4, "compressor");
    k.smallEjector = flag(L, 4, "smallEjector");
    k.largeEjector = flag(L, 4, "largeEjector");
    k.crankForce = flag(L, 4, "crankForce", true);
    k.injector1Switch = flag(L, 4, "injector1Switch");
    k.injector2Switch = flag(L, 4, "injector2Switch");
    k.shovels = int(number(L, 4, "shovels", 0.0));
    k.fireOn = flag(L, 4, "fireOn");
    k.fireOff = flag(L, 4, "fireOff");
    k.fireReset = flag(L, 4, "fireReset");
    k.refill = flag(L, 4, "refill");
    Inputs in;
    in.dt = dt;
    in.speed = number(L, 5, "speed", 0.0);
    in.gradient = number(L, 5, "gradient", 0.0);
    in.outsideTemperatureC = number(L, 5, "outsideTemperature", 15.0);
    in.tenderCoupled = flag(L, 5, "tenderCoupled", true);
    in.heatingSteam = number(L, 5, "heatingSteam", 0.0) * 2.20462;

    Outputs out;
    Steam::step(c, s, k, in, out);

    s.write(numbers.data());
    for (int i = 0; i < size; ++i) {
        lua_pushnumber(L, numbers[size_t(i)]);
        lua_rawseti(L, 3, i + 1);
    }
    for (int i = had; i > size; --i) {
        lua_pushnil(L);
        lua_rawseti(L, 3, i);
    }

    if (lua_istable(L, 6)) {
        const std::pair<const char *, double> values[] = {
                {"boilerPressure", out.boilerPressure},
                {"boilerPressureMax", c.maxBoilerPressurePsi * PsiPa},
                {"steamChestPressure", out.steamChestPressure},
                {"backPressure", out.backPressure},
                {"meanEffectivePressure", out.meanEffectivePressure},
                {"indicatedPower", out.indicatedPower},
                {"boilerWater", out.waterGlass},
                {"boilerWaterGradient", out.waterGlassGradient},
                {"fire", out.fire},
                {"fireMass", out.fireMass},
                {"burnRate", out.burnRate},
                {"feedRate", out.feedRate},
                {"tenderCoal", out.tenderCoal},
                {"tenderWater", out.tenderWater},
                {"cylinderSteam", out.cylinderSteam},
                {"steamUsage", out.steamUsage},
                {"injector1Flow", out.injector1Flow},
                {"injector2Flow", out.injector2Flow},
                {"superheat", out.superheat},
                {"variable1", out.variable1},
                {"variable2", out.variable2},
                {"variable3", out.variable3}};
        for (const auto &[name, value] : values) {
            lua_pushnumber(L, std::isfinite(value) ? value : 0.0);
            lua_setfield(L, 6, name);
        }
        const std::pair<const char *, bool> flags[] = {{"injector1", out.injector1},
                                                       {"injector2", out.injector2},
                                                       {"safetyValves", out.safetyValves},
                                                       {"fusiblePlug", out.fusiblePlug},
                                                       {"priming", out.priming}};
        for (const auto &[name, value] : flags) {
            lua_pushboolean(L, value);
            lua_setfield(L, 6, name);
        }        // The exhaust (task driving 05), for the kinds of emitters it has, by
        // tsre/exhaust's names (fx.<kind>.<field>; color left 0).
        struct Kind {
            bool has;
            const Outputs::Exhaust &x;
            const char *names[6];
        };
        const Kind kinds[] = {
                {c.stackEmitter, out.stackExhaust,
                 {"fx.stack.flow", "fx.stack.speed", "fx.stack.temperature", "fx.stack.steam", "fx.stack.soot",
                  "fx.stack.beats"}},
                {c.cockEmitter, out.cockExhaust,
                 {"fx.cylinders.flow", "fx.cylinders.speed", "fx.cylinders.temperature", "fx.cylinders.steam",
                  "fx.cylinders.soot", "fx.cylinders.beats"}},
                {c.safetyValveEmitter, out.safetyExhaust,
                 {"fx.safetyvalves.flow", "fx.safetyvalves.speed", "fx.safetyvalves.temperature",
                  "fx.safetyvalves.steam", "fx.safetyvalves.soot", "fx.safetyvalves.beats"}}};
        for (const Kind &kind : kinds) {
            if (!kind.has)
                continue;
            const double fields[6] = {kind.x.flow, kind.x.speed, kind.x.temperature,
                                      kind.x.steam, kind.x.soot, kind.x.beats};
            for (int i = 0; i < 6; ++i) {
                lua_pushnumber(L, std::isfinite(fields[i]) ? fields[i] : 0.0);
                lua_setfield(L, 6, kind.names[i]);
            }
        }
    }

    lua_pushnumber(L, std::isfinite(out.force) ? out.force : 0.0);
    if (out.sounds.empty() && out.messages.empty()) {
        lua_pushnil(L);
    } else {
        lua_createtable(L, int(out.sounds.size() + out.messages.size()), 0);
        int n = 0;
        for (int sound : out.sounds) {
            lua_pushinteger(L, sound);
            lua_rawseti(L, -2, ++n);
        }
        for (const char *message : out.messages) {
            lua_pushstring(L, message);
            lua_rawseti(L, -2, ++n);
        }
    }
    return 2;
}

// steam.info(vehicle)
int info(lua_State *L) {
    const Config &c = configAt(L);
    double maxForce = 0.0, maxPower = 0.0;
    int cylinders = 0;
    for (const EngineConfig &e : c.engines) {
        maxForce += e.maxTractiveEffortLbf * c.cylinderEfficiency * LbfN;
        maxPower += e.maxIndicatedHp * HpW;
        cylinders += e.cylinders;
    }
    lua_createtable(L, 0, 5);
    lua_pushnumber(L, c.maxBoilerPressurePsi * PsiPa);
    lua_setfield(L, -2, "maxBoilerPressure");
    lua_pushnumber(L, maxForce);
    lua_setfield(L, -2, "maxForce");
    lua_pushnumber(L, maxPower);
    lua_setfield(L, -2, "maxPower");
    lua_pushinteger(L, lua_Integer(c.engines.size()));
    lua_setfield(L, -2, "engines");
    lua_pushinteger(L, cylinders);
    lua_setfield(L, -2, "cylinders");
    return 1;
}

}

int openModule(lua_State *L) {
    lua_createtable(L, 0, 2);
    // The configs by definition, shared by its functions.
    lua_newtable(L);
    const std::pair<const char *, lua_CFunction> functions[] = {{"step", step}, {"info", info}};
    for (const auto &[name, function] : functions) {
        lua_pushvalue(L, -1);
        lua_pushcclosure(L, function, 1);
        lua_setfield(L, -3, name);
    }
    lua_pop(L, 1);
    return 1;
}

}
