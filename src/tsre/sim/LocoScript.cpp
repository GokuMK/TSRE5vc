#include <tsre/sim/LocoScript.h>
#include <QFileInfo>
#include <cmath>
#include <cstring>
#include <limits>
#include <tsre/script/LuaApi.h>
#include <tsre/script/ScriptState.h>
#include <tsre/maszyna/MaszynaVehicle.h>
#include <tsre/sim/steam/SteamModule.h>
#include <tsre/maszyna/MaszynaDiesel.h>
#include <tsre/trains/CabControls.h>
#include <tsre/trains/VehicleDefinition.h>
#include <tsre/trains/VehicleKeys.h>

namespace Sim {

namespace {

using Item = SimisDocument::Item;

// The run's LocoScripts in the Lua registry, for native modules.
const char *HostKey = "tsre.locoscripts";

void pushText(lua_State *L, QStringView text) {
    const QByteArray utf8 = text.toUtf8();
    lua_pushlstring(L, utf8.constData(), size_t(utf8.size()));
}

void pushMessageValue(lua_State *L, const MessageValue &value) {
    if (const bool *b = std::get_if<bool>(&value))
        lua_pushboolean(L, *b);
    else if (const double *d = std::get_if<double>(&value))
        lua_pushnumber(L, *d);
    else
        pushText(L, std::get<QString>(value));
}

// A Lua value as a message's value: numbers, flags and text; false for
// anything else.
bool messageValue(lua_State *L, int index, MessageValue &value) {
    switch (lua_type(L, index)) {
    case LUA_TNUMBER: value = double(lua_tonumber(L, index)); return true;
    case LUA_TBOOLEAN: value = bool(lua_toboolean(L, index)); return true;
    case LUA_TSTRING: value = QString::fromUtf8(lua_tostring(L, index)); return true;
    default: return false;
    }
}

// Pushes a value of a vehicle's file at `path` (its `index`th) as the key
// catalogue reads it: numbers in SI, integers, flags, text; a key it does
// not know as a number where it reads as one.
void pushFileValue(lua_State *L, const VehicleDefinition &d, QAnyStringView path, int index, Item item) {
    const SimisDocument &doc = *d.document;
    const QStringView text = doc.text(item);
    const VehicleKeys::Value key = VehicleKeys::value(path, index);
    double si = 0.0;
    switch (key.kind) {
    case VehicleKeys::Value::Kind::Number:
        if (doc.number(item, key.quantity, si, key.defaultUnit)) {
            lua_pushnumber(L, si);
            return;
        }
        break;
    case VehicleKeys::Value::Kind::Integer: {
        bool ok = false;
        const int n = text.toInt(&ok);
        if (ok) {
            lua_pushinteger(L, n);
            return;
        }
        break;
    }
    case VehicleKeys::Value::Kind::Bool:
        if (text.compare(u"true", Qt::CaseInsensitive) == 0 || text == u"1") {
            lua_pushboolean(L, 1);
            return;
        }
        if (text.compare(u"false", Qt::CaseInsensitive) == 0 || text == u"0") {
            lua_pushboolean(L, 0);
            return;
        }
        break;
    case VehicleKeys::Value::Kind::Text: {
        // A key the catalogue lists no values for (unknown, or a block read
        // by a parser of its own): a number where it reads as one, its unit
        // giving its quantity.
        const VehicleKeys::Key *known = VehicleKeys::find(path);
        if ((!known || known->values.isEmpty()) && doc.number(item, Units::Quantity::Any, si)) {
            lua_pushnumber(L, si);
            return;
        }
        break;
    }
    }
    pushText(L, text);
}

// Pushes a block's values as a table, in order.
void pushBlockValues(lua_State *L, const VehicleDefinition &d, QAnyStringView path, Item block) {
    const QVector<Item> values = d.document->values(block);
    lua_createtable(L, int(values.size()), 0);
    for (int i = 0; i < values.size(); ++i) {
        pushFileValue(L, d, path, i, values[i]);
        lua_rawseti(L, -2, i + 1);
    }
}

const VehicleDefinition *definitionOf(lua_State *L) {
    return static_cast<const VehicleDefinition *>(lua_touserdata(L, lua_upvalueindex(1)));
}

QString pathArgument(lua_State *L, int index) {
    return QString::fromUtf8(luaL_checkstring(L, index)).toLower();
}

// Where a reader's paths start: the file (`vehicle`), or a block of it
// (a reader of vehicle.each), its path kept for the key catalogue.
struct Scope {
    const VehicleDefinition *d = nullptr;
    Item block;
    QString path;

    QString full(const QString &relative) const { return path.isEmpty() ? relative : path + u'(' + relative; }
    // A definition without a file of keys (MaSzyna's) reads as one without
    // any.
    const SimisDocument *document() const { return d != nullptr ? d->document.get() : nullptr; }
    QVector<Item> all(const QString &relative) const {
        return document() != nullptr ? document()->all(relative, block) : QVector<Item>();
    }
    Item last(const QString &relative) const {
        const QVector<Item> list = all(relative);
        return list.isEmpty() ? Item() : list.last();
    }
};

// A reader's function: the definition its first upvalue; a scoped one's
// block (its tree and node) and path the next three.
Scope scopeOf(lua_State *L) {
    Scope scope;
    scope.d = definitionOf(L);
    if (lua_islightuserdata(L, lua_upvalueindex(2))) {
        scope.block.tree = static_cast<const SimisTree *>(lua_touserdata(L, lua_upvalueindex(2)));
        scope.block.node = int(lua_tointeger(L, lua_upvalueindex(3)));
        scope.path = QString::fromUtf8(lua_tostring(L, lua_upvalueindex(4)));
    }
    return scope;
}

// reader.number(path [, index [, default]])
int vehicleNumber(lua_State *L) {
    const Scope scope = scopeOf(L);
    const QString path = pathArgument(L, 1);
    const int index = int(luaL_optinteger(L, 2, 1)) - 1;
    const QString full = scope.full(path);
    const Item block = scope.last(path);
    const Item value = block.tree ? scope.document()->values(block).value(index) : Item();
    const VehicleKeys::Value key = VehicleKeys::value(full, index);
    const Units::Quantity quantity = key.kind == VehicleKeys::Value::Kind::Number ? key.quantity : Units::Quantity::Any;
    double si = 0.0;
    if (value.tree && scope.document()->number(value, quantity, si, key.defaultUnit))
        lua_pushnumber(L, si);
    else if (lua_isnoneornil(L, 3))
        lua_pushnil(L);
    else
        lua_pushvalue(L, 3);
    return 1;
}

// reader.text(path [, index])
int vehicleText(lua_State *L) {
    const Scope scope = scopeOf(L);
    const QString path = pathArgument(L, 1);
    const Item block = scope.last(path);
    const Item value = block.tree ? scope.document()->values(block).value(int(luaL_optinteger(L, 2, 1)) - 1) : Item();
    if (value.tree)
        pushText(L, scope.document()->text(value));
    else
        lua_pushnil(L);
    return 1;
}

// reader.values(path): the last such block's values.
int vehicleValues(lua_State *L) {
    const Scope scope = scopeOf(L);
    const QString path = pathArgument(L, 1);
    const Item block = scope.last(path);
    if (!block.tree)
        lua_pushnil(L);
    else
        pushBlockValues(L, *scope.d, scope.full(path), block);
    return 1;
}

// reader.all(path): every such block's values.
int vehicleAll(lua_State *L) {
    const Scope scope = scopeOf(L);
    const QString path = pathArgument(L, 1);
    const QVector<Item> blocks = scope.all(path);
    lua_createtable(L, int(blocks.size()), 0);
    for (int i = 0; i < blocks.size(); ++i) {
        pushBlockValues(L, *scope.d, scope.full(path), blocks[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

// reader.blocks(path): the blocks in the last such block, in order, each
// {name = its name (a number where it reads as one), values = {...}}; the
// curves' form (ORTSMaxTractiveForceCurves ( 0 ( v F ... ) 0.5 ( ... ) )).
int vehicleBlocks(lua_State *L) {
    const Scope scope = scopeOf(L);
    const QString path = pathArgument(L, 1);
    const Item block = scope.last(path);
    if (!block.tree) {
        lua_pushnil(L);
        return 1;
    }
    const SimisDocument &doc = *scope.document();
    lua_newtable(L);
    int n = 0;
    for (const Item &item : doc.items(block)) {
        if (!doc.isBlock(item))
            continue;
        lua_createtable(L, 0, 2);
        const QStringView name = doc.text(item);
        double number = 0.0;
        if (doc.number(item, Units::Quantity::Any, number))
            lua_pushnumber(L, number);
        else
            pushText(L, name);
        lua_setfield(L, -2, "name");
        pushBlockValues(L, *scope.d, scope.full(path) + u'(' + name.toString().toLower(), item);
        lua_setfield(L, -2, "values");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

// reader.has(path)
int vehicleHas(lua_State *L) {
    const Scope scope = scopeOf(L);
    lua_pushboolean(L, scope.last(pathArgument(L, 1)).tree != nullptr);
    return 1;
}

int vehicleEach(lua_State *L);

const std::pair<const char *, lua_CFunction> ReaderFunctions[] = {
        {"number", vehicleNumber}, {"text", vehicleText}, {"values", vehicleValues}, {"all", vehicleAll},
        {"blocks", vehicleBlocks}, {"has", vehicleHas}, {"each", vehicleEach}};

// reader.each(path): a reader for each such block, its paths starting
// there (a loco's engines: vehicle.each("engine(ortsdieselengines(diesel")).
int vehicleEach(lua_State *L) {
    const Scope scope = scopeOf(L);
    const QString path = pathArgument(L, 1);
    const QVector<Item> blocks = scope.all(path);
    const QByteArray full = scope.full(path).toUtf8();
    lua_createtable(L, int(blocks.size()), 0);
    for (int i = 0; i < blocks.size(); ++i) {
        lua_createtable(L, 0, int(std::size(ReaderFunctions)));
        for (const auto &[name, function] : ReaderFunctions) {
            lua_pushlightuserdata(L, const_cast<VehicleDefinition *>(scope.d));
            lua_pushlightuserdata(L, const_cast<SimisTree *>(blocks[i].tree));
            lua_pushinteger(L, blocks[i].node);
            lua_pushlstring(L, full.constData(), size_t(full.size()));
            lua_pushcclosure(L, function, 4);
            lua_setfield(L, -2, name);
        }
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

const char *ProxyMeta = "tsre.vehicle.proxy";

void pushProxy(lua_State *L, const VehicleDefinition *d, const QString &path);

// A key under a proxy: vehicle.engine.maxforce. A key read as values gives
// its first (or all of them, when it has several); a block of blocks a
// proxy to index further; nil where the file has none.
int proxyIndex(lua_State *L) {
    if (lua_type(L, 2) != LUA_TSTRING)
        return 0;
    lua_pushstring(L, "__def");
    lua_rawget(L, 1);
    const auto *d = static_cast<const VehicleDefinition *>(lua_touserdata(L, -1));
    lua_pushstring(L, "__path");
    lua_rawget(L, 1);
    const QString base = QString::fromUtf8(lua_tostring(L, -1));
    lua_pop(L, 2);
    if (!d || !d->document)
        return 0;
    const QString key = QString::fromUtf8(lua_tostring(L, 2)).toLower();
    const QString path = base.isEmpty() ? key : base + u'(' + key;
    const Item block = d->document->last(path);
    if (!block.tree)
        return 0;
    const QVector<Item> values = d->document->values(block);
    bool blocks = false;
    for (const Item &item : d->document->items(block))
        blocks = blocks || d->document->isBlock(item);
    const VehicleKeys::Key *known = VehicleKeys::find(path);
    if (blocks && (!known || known->read != VehicleKeys::Read::Values)) {
        pushProxy(L, d, path);
    } else if (values.size() == 1) {
        pushFileValue(L, *d, path, 0, values[0]);
    } else if (values.isEmpty()) {
        lua_pushboolean(L, 1);
    } else {
        pushBlockValues(L, *d, path, block);
    }
    return 1;
}

void pushProxy(lua_State *L, const VehicleDefinition *d, const QString &path) {
    lua_createtable(L, 0, 2);
    lua_pushlightuserdata(L, const_cast<VehicleDefinition *>(d));
    lua_setfield(L, -2, "__def");
    pushText(L, path);
    lua_setfield(L, -2, "__path");
    if (luaL_newmetatable(L, ProxyMeta)) {
        lua_pushcfunction(L, proxyIndex);
        lua_setfield(L, -2, "__index");
    }
    lua_setmetatable(L, -2);
}

void pushFizValue(lua_State *L, const MaszynaFiz::Value &value) {
    if (value.isNumber)
        lua_pushnumber(L, value.number);
    else
        pushText(L, value.text);
}

// A MaSzyna vehicle's .fiz (task maszyna 03), `vehicle.fiz`: each section a
// table of its keys' values in SI ({ Param = { M = 120000, Vmax = 34.7, ... },
// ... }), and its table's rows as `rows`, each a list of values.
void pushFiz(lua_State *L, const MaszynaFiz &fiz) {
    lua_createtable(L, 0, int(fiz.sections.size()));
    for (const MaszynaFiz::Section &section : fiz.sections) {
        lua_createtable(L, 0, int(section.keys.size()) + 1);
        for (const auto &[key, value] : section.keys) {
            pushFizValue(L, value);
            lua_setfield(L, -2, key.toUtf8().constData());
        }
        if (!section.rows.isEmpty()) {
            lua_createtable(L, int(section.rows.size()), 0);
            for (int r = 0; r < section.rows.size(); ++r) {
                const QVector<MaszynaFiz::Value> &row = section.rows[r];
                lua_createtable(L, int(row.size()), 0);
                for (int c = 0; c < row.size(); ++c) {
                    pushFizValue(L, row[c]);
                    lua_rawseti(L, -2, c + 1);
                }
                lua_rawseti(L, -2, r + 1);
            }
            lua_setfield(L, -2, "rows");
        }
        lua_setfield(L, -2, section.name.toUtf8().constData());
    }
}

// A MaSzyna vehicle's sounds (task maszyna 05), `vehicle.sounds` and
// `vehicle.cabSounds`: each key's factors as MaSzyna holds them after
// loading ({ amplitudeFactor, amplitudeOffset, frequencyFactor,
// frequencyOffset, range, combined, bookends: a begin and an end part }),
// what maszyna.sounds computes with.
void pushSounds(lua_State *L, const std::vector<MaszynaSound::Definition> &list) {
    lua_createtable(L, 0, int(list.size()));
    for (const MaszynaSound::Definition &d : list) {
        lua_createtable(L, 0, 7);
        lua_pushnumber(L, d.amplitudeFactor);
        lua_setfield(L, -2, "amplitudeFactor");
        lua_pushnumber(L, d.amplitudeOffset);
        lua_setfield(L, -2, "amplitudeOffset");
        lua_pushnumber(L, d.frequencyFactor);
        lua_setfield(L, -2, "frequencyFactor");
        lua_pushnumber(L, d.frequencyOffset);
        lua_setfield(L, -2, "frequencyOffset");
        lua_pushnumber(L, d.range);
        lua_setfield(L, -2, "range");
        lua_pushboolean(L, d.combined());
        lua_setfield(L, -2, "combined");
        lua_pushboolean(L, !d.begin.empty() && !d.end.empty());
        lua_setfield(L, -2, "bookends");
        const QByteArray key = d.key.toUtf8();
        lua_setfield(L, -2, key.constData());
    }
}

// The `vehicle` table init() gets.
void pushVehicle(lua_State *L, const VehicleDefinition *d) {
    pushProxy(L, d, QString());
    for (const auto &[name, function] : ReaderFunctions) {
        lua_pushlightuserdata(L, const_cast<VehicleDefinition *>(d));
        lua_pushcclosure(L, function, 1);
        lua_setfield(L, -2, name);
    }
    lua_createtable(L, int(d->exhaustKinds.size()), 0);
    for (int i = 0; i < d->exhaustKinds.size(); ++i) {
        pushText(L, d->exhaustKinds[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "exhaustKinds");
    if (d->maszyna != nullptr) {
        pushFiz(L, d->maszyna->physics());
        lua_setfield(L, -2, "fiz");
        pushSounds(L, d->maszyna->sounds().vehicle);
        lua_setfield(L, -2, "sounds");
        pushSounds(L, d->maszyna->sounds().cab);
        lua_setfield(L, -2, "cabSounds");
    }
}

} // namespace

// A loco whose model is a Lua script.
class LuaLoco : public VehicleModel {
public:
    LuaLoco(LocoScripts &host, int script, std::shared_ptr<const VehicleDefinition> definition, QString loadError)
        : host(host), lua(host.state), script(script), definition(std::move(definition)),
          problem(std::move(loadError)) {
        lua_State *L = lua->lua();
        lua_newtable(L);
        stateRef = lua->keep(-1);
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushlightuserdata(L, const_cast<VehicleDefinition *>(this->definition.get()));
        lua_setfield(L, -2, "vehicle");
        inputsRef = lua->keep(-1);
        lua_pop(L, 1);
        lua_newtable(L);
        lua_newtable(L);
        lua_setfield(L, -2, "values");
        outRef = lua->keep(-1);
        lua_pop(L, 1);
        if (script >= 0 && lua->prepare(script, "init")) {
            host.current = this;
            lua->push(stateRef);
            pushVehicle(L, this->definition.get());
            if (!lua->call(2, 0))
                problem = lua->lastError();
            host.current = nullptr;
        }
    }
    ~LuaLoco() override {
        lua->release(stateRef);
        lua->release(inputsRef);
        lua->release(outRef);
    }
    bool drives() const override { return true; }

    void command(const Message &m) override {
        // A 3D cab's control worked by the mouse, "cab.<control>" (task
        // driving 08): its own commands from its controls' module.
        if (m.name.startsWith(QLatin1String("cab.")) && cabControls() != nullptr) {
            bool number = false;
            const int control = m.name.mid(4).toInt(&number);
            if (number)
                for (const Message &own : cabCommands(control, m.value))
                    run(own, false);
            return;
        }
        run(m, false);
    }
    // Its driven end's cab's controls (task maszyna 04), null for none.
    const CabControls *cabControls() const { return definition->cab3DControlsAt(facing < 0).get(); }
    std::vector<Message> cabCommands(int control, const MessageValue &value) {
        std::vector<Message> commands;
        const CabControls &controls = *cabControls();
        const int caller = host.cabScript(controls.module());
        if (caller < 0 || control < 0 || control >= controls.count() || !lua->prepare(caller, "commands"))
            return commands;
        lua_State *L = lua->lua();
        host.current = this;
        host.pushCabControls(controls);
        lua_pushinteger(L, control + 1);
        pushMessageValue(L, value);
        lua->push(outRef);
        lua_getfield(L, -1, "values");
        lua_remove(L, -2);
        lua->push(stateRef);
        const bool ran = lua->call(5, 1);
        host.current = nullptr;
        if (!ran)
            return commands;
        const lua_Integer n = lua_istable(L, -1) ? lua_Integer(lua_rawlen(L, -1)) : 0;
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(L, -1, i);
            if (lua_istable(L, -1)) {
                lua_rawgeti(L, -1, 1);
                lua_rawgeti(L, -2, 2);
                MessageValue v;
                if (lua_type(L, -2) == LUA_TSTRING && messageValue(L, -1, v))
                    commands.push_back({QString::fromUtf8(lua_tostring(L, -2)), std::move(v)});
                lua_pop(L, 2);
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return commands;
    }

    // Its controls' module's values for the cab's controls, from this loco's
    // last values, state and inputs (task driving 08; the inputs task
    // maszyna 04); the stops it gives them once.
    bool cabValues(const CabControls &controls, double speed, std::vector<double> &values,
                   std::shared_ptr<const CabStops> *stops) override {
        const int caller = host.cabScript(controls.module());
        if (stops != nullptr)
            *stops = cabStopsOf(controls, caller);
        if (caller < 0 || !lua->prepare(caller, "values"))
            return false;
        lua_State *L = lua->lua();
        host.current = this;
        host.pushCabControls(controls);
        lua->push(outRef);
        lua_getfield(L, -1, "values");
        lua_remove(L, -2);
        lua->push(stateRef);
        lua_pushnumber(L, speed);
        lua->push(inputsRef);
        const bool ran = lua->call(5, 1);
        host.current = nullptr;
        if (!ran)
            return false;
        const lua_Integer n = lua_istable(L, -1) ? lua_Integer(lua_rawlen(L, -1)) : 0;
        values.assign(size_t(n), std::numeric_limits<double>::quiet_NaN());
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(L, -1, i);
            if (lua_type(L, -1) == LUA_TNUMBER)
                values[size_t(i - 1)] = lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return true;
    }
    void fromLine(const Message &m) override { run(m, true); }

    // The stops the module's stops() gives the controls: for each, nil (no
    // stops: clicked, or the cab's own) or a list of the values its stops
    // send; asked once for these controls.
    std::shared_ptr<const CabStops> cabStopsOf(const CabControls &controls, int caller) {
        if (stopsOf == &controls)
            return stopsGiven;
        stopsOf = &controls;
        stopsGiven.reset();
        if (caller < 0 || !lua->prepare(caller, "stops"))
            return stopsGiven;
        lua_State *L = lua->lua();
        host.current = this;
        host.pushCabControls(controls);
        lua->push(outRef);
        lua_getfield(L, -1, "values");
        lua_remove(L, -2);
        lua->push(stateRef);
        const bool ran = lua->call(3, 1);
        host.current = nullptr;
        if (!ran)
            return stopsGiven;
        if (lua_istable(L, -1)) {
            auto made = std::make_shared<CabStops>(size_t(controls.count()));
            for (int i = 0; i < controls.count(); ++i) {
                lua_rawgeti(L, -1, i + 1);
                if (lua_istable(L, -1)) {
                    const lua_Integer n = lua_Integer(lua_rawlen(L, -1));
                    for (lua_Integer k = 1; k <= n; ++k) {
                        lua_rawgeti(L, -1, k);
                        if (lua_type(L, -1) == LUA_TNUMBER)
                            (*made)[size_t(i)].push_back(lua_tonumber(L, -1));
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1);
            }
            stopsGiven = std::move(made);
        }
        lua_pop(L, 1);
        return stopsGiven;
    }

    void step(const VehicleInputs &in, VehicleOutputs &out) override {
        facing = in.cabFacing;
        out.motiveForce = 0.0;
        out.brakeForce = 0.0;
        out.brakePipeAir = 0.0;
        out.independentBrake = 0.0;
        out.bailOff = 0.0;
        if (!lua->prepare(script, "update")) {
            failed();
            report(out);
            return;
        }
        lua_State *L = lua->lua();
        host.current = this;
        lua_pushnumber(L, in.dt);
        lua->push(inputsRef);
        const std::pair<const char *, double> numbers[] = {
                {"speed", in.speed}, {"gradient", in.gradient}, {"adhesionLimit", in.adhesionLimit},
                {"lineVoltage", in.lineVoltage}, {"airTemperature", in.airTemperature}, {"brakePipe", in.brakePipe},
                {"brakePipeVolume", in.brakePipeVolume}, {"brakeCylinder", in.brakeCylinder},
                {"mainPipe", in.mainPipe}, {"heatingPipe", in.heatingPipe}, {"heatingSteam", in.heatingSteam}};
        for (const auto &[name, value] : numbers) {
            lua_pushnumber(L, value);
            lua_setfield(L, -2, name);
        }
        lua_pushboolean(L, in.cab);
        lua_setfield(L, -2, "cab");
        lua_pushinteger(L, in.cabFacing);
        lua_setfield(L, -2, "cabFacing");
        lua->push(stateRef);
        lua->push(outRef);
        for (const char *name : {"motiveForce", "brakeForce", "brakePipeAir", "mainPipeAir", "independentBrake", "bailOff",
                                 "heatingSupply"}) {
            lua_pushnumber(L, 0.0);
            lua_setfield(L, -2, name);
        }
        if (!lua->call(4, 0)) {
            problem = lua->lastError();
            host.current = nullptr;
            report(out);
            return;
        }
        host.current = nullptr;
        lua->push(outRef);
        out.motiveForce = numberField(L, "motiveForce");
        out.brakeForce = std::max(0.0, numberField(L, "brakeForce"));
        out.brakePipeAir = numberField(L, "brakePipeAir");
        out.mainPipeAir = numberField(L, "mainPipeAir");
        out.independentBrake = std::max(0.0, numberField(L, "independentBrake"));
        out.bailOff = std::max(0.0, numberField(L, "bailOff"));
        out.heatingSupply = std::max(0.0, numberField(L, "heatingSupply"));
        lua_getfield(L, -1, "values");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                MessageValue value;
                if (lua_type(L, -2) == LUA_TSTRING && messageValue(L, -1, value)) {
                    size_t size = 0;
                    const char *name = lua_tolstring(L, -2, &size);
                    out.values.insert(nameOf(name, size), std::move(value));
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 2);
        report(out);
    }

    bool save(QByteArray &text) override {
        lua_State *L = lua->lua();
        bool ok;
        if (lua->prepare(script, "save")) {
            lua->push(stateRef);
            if (!lua->call(1, 1))
                return false;
            ok = lua->save(-1, text);
        } else {
            lua->push(stateRef);
            ok = lua->save(-1, text);
        }
        lua_pop(L, 1);
        return ok;
    }

    bool restore(const QByteArray &text) override {
        lua_State *L = lua->lua();
        if (!lua->restore(text))
            return false;
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            return false;
        }
        lua->release(stateRef);
        stateRef = lua->keep(-1);
        lua_pop(L, 1);
        return true;
    }

    // What send() and line() gave, until the next step's outputs.
    std::vector<Message> events, line;

private:
    LocoScripts &host;
    std::shared_ptr<Script::State> lua;
    int script;
    std::shared_ptr<const VehicleDefinition> definition;
    int stateRef = -1, inputsRef = -1, outRef = -1;
    // The way its driver faced at its last step (VehicleInputs::cabFacing).
    int facing = 1;
    // The stops its cab's controls' module gave (cabStopsOf), and for which.
    const CabControls *stopsOf = nullptr;
    std::shared_ptr<const CabStops> stopsGiven;
    // Why its script stopped; told once.
    QString problem;
    bool told = false;
    // Its values' names as Qt text, by where Lua keeps the name's bytes
    // (the same while the script keeps the name; checked, as a collected
    // name's place may hold another).
    struct Name {
        QByteArray utf8;
        QString text;
    };
    QHash<const char *, Name> names;

    const QString &nameOf(const char *bytes, size_t size) {
        auto found = names.find(bytes);
        if (found == names.end() || size_t(found->utf8.size()) != size
            || std::memcmp(found->utf8.constData(), bytes, size) != 0) {
            // Names made anew every step do not pile up.
            if (names.size() >= 1024)
                names.clear();
            found = names.insert(bytes, {QByteArray(bytes, qsizetype(size)), QString::fromUtf8(bytes, qsizetype(size))});
        }
        return found->text;
    }

    // command(name, value, state[, "line"]).
    void run(const Message &m, bool fromLine) {
        if (!lua->prepare(script, "command")) {
            failed();
            return;
        }
        lua_State *L = lua->lua();
        host.current = this;
        pushText(L, m.name);
        pushMessageValue(L, m.value);
        lua->push(stateRef);
        if (fromLine)
            lua_pushstring(L, "line");
        if (!lua->call(fromLine ? 4 : 3, 0))
            problem = lua->lastError();
        host.current = nullptr;
    }
    static double numberField(lua_State *L, const char *name) {
        lua_getfield(L, -1, name);
        int ok = 0;
        const double value = lua_tonumberx(L, -1, &ok);
        lua_pop(L, 1);
        return ok && std::isfinite(value) ? value : 0.0;
    }
    void failed() {
        if (problem.isEmpty() && lua->failed(script))
            problem = QString("%1 stopped after an error").arg(lua->scriptName(script));
    }
    // Its events and line messages into its outputs; its script's error
    // told once (the script gives no force after it).
    void report(VehicleOutputs &out) {
        out.events.insert(out.events.end(), events.begin(), events.end());
        out.line.insert(out.line.end(), line.begin(), line.end());
        events.clear();
        line.clear();
        if (!problem.isEmpty() && !told) {
            out.events.push_back({"scriptError", problem});
            told = true;
        }
    }
};

namespace {

// send(name [, value]) and line(name [, value]): the running loco's event,
// its message to the train's other units.
int sendMessage(lua_State *L, bool toLine) {
    auto *host = static_cast<LocoScripts *>(lua_touserdata(L, lua_upvalueindex(1)));
    Message m;
    m.name = QString::fromUtf8(luaL_checkstring(L, 1));
    if (lua_isnoneornil(L, 2))
        m.value = true;
    else if (!messageValue(L, 2, m.value))
        return luaL_argerror(L, 2, "a number, a boolean or a string");
    host->deliver(m, toLine);
    return 0;
}
int send(lua_State *L) { return sendMessage(L, false); }
int line(lua_State *L) { return sendMessage(L, true); }

} // namespace

LocoScripts::LocoScripts(const QString &folder, unsigned seed) : folder(folder), seed(seed) {}

void LocoScripts::deliver(const Message &message, bool toLine) {
    if (current)
        (toLine ? current->line : current->events).push_back(message);
}

LocoScripts::~LocoScripts() = default;

const VehicleDefinition &LocoScripts::definitionAt(lua_State *L, int index) {
    lua_getfield(L, LUA_REGISTRYINDEX, HostKey);
    const auto *host = static_cast<const LocoScripts *>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    const auto *d = static_cast<const VehicleDefinition *>(lua_touserdata(L, index));
    if (!host || !lua_islightuserdata(L, index) || !host->definitions.contains(d))
        luaL_argerror(L, index, "not a loco's inputs.vehicle");
    return *d;
}

Script::State &LocoScripts::lua() {
    if (!state) {
        state = std::make_shared<Script::State>(Script::State::Kind::Simulation, Script::Limits(), seed);
        state->setModuleRoots({folder});
        // TSRE's native modules for its loco scripts.
        state->addNativeModule("tsre.steam", Steam::openModule);
        state->addNativeModule("maszyna.diesel", MaszynaDiesel::openModule);
        lua_pushlightuserdata(state->lua(), this);
        lua_setfield(state->lua(), LUA_REGISTRYINDEX, HostKey);
        // send() and line() beside the libraries: every script and module
        // reads them.
        lua_State *L = state->lua();
        lua_pushglobaltable(L);
        const std::pair<const char *, lua_CFunction> functions[] = {{"send", send}, {"line", line}};
        for (const auto &[name, function] : functions) {
            lua_pushstring(L, name);
            lua_pushlightuserdata(L, this);
            lua_pushcclosure(L, function, 1);
            lua_rawset(L, -3);
        }
        lua_pop(L, 1);
    }
    return *state;
}

QString LocoScripts::scriptFor(const VehicleDefinition &definition) const {
    if (folder.isEmpty())
        return QString();
    // A MaSzyna engine (task maszyna 03): the script of its EngineType; a
    // kind without one, or a script not there, runs the C++ model as any
    // loco missing a script.
    if (definition.maszyna != nullptr) {
        const QString kind = definition.maszyna->physics().text(u"Engine", u"EngineType");
        QString name;
        if (kind == QLatin1String("ElectricSeriesMotor"))
            name = QStringLiteral("electric-resistor-mmd.lua");
        else if (kind == QLatin1String("ElectricInductionMotor"))
            name = QStringLiteral("electric-induction-mmd.lua");
        else if (kind == QLatin1String("DieselElectric") || kind == QLatin1String("DumbDE")
                 || kind == QLatin1String("DieselEngine"))
            name = QStringLiteral("diesel-mmd.lua");
        else if (kind == QLatin1String("Dumb") || kind == QLatin1String("WheelsDriven"))
            name = definition.wagonTypeId == 4 ? QStringLiteral("electric-induction-mmd.lua")
                 : definition.wagonTypeId == 5 ? QStringLiteral("diesel-mmd.lua") : QString();
        else if (kind.isEmpty() && definition.wagonTypeId == 4)
            // A control car: its unit's script, without force (an EMU's
            // resistors, else induction motors).
            name = definition.maszyna->physics().text(u"Param", u"Type").toLower() == QLatin1String("ezt")
                    ? QStringLiteral("electric-resistor-mmd.lua") : QStringLiteral("electric-induction-mmd.lua");
        if (name.isEmpty() || !QFileInfo::exists(folder + QLatin1Char('/') + name))
            return QString();
        return folder + QLatin1Char('/') + name;
    }
    // An electric engine (the editors' kind 4), a diesel (5) or a steam
    // engine (6), read from an ENG: one without has no script, and runs the
    // C++ model as one missing a script.
    if (!definition.document)
        return QString();
    if (definition.wagonTypeId == 4)
        return folder + "/electric.lua";
    if (definition.wagonTypeId == 5)
        return folder + "/diesel.lua";
    if (definition.wagonTypeId == 6)
        return folder + "/steam.lua";
    return QString();
}

// The script calling a cab's controls' module (tsre.cabview, maszyna.cab),
// so a loco script's additions to it (it shares the module) count.
int LocoScripts::cabScript(const QByteArray &module) {
    const auto known = cabModules.constFind(module);
    if (known != cabModules.constEnd())
        return known.value();
    const QByteArray text = "local cab = require \"" + module + "\"\n"
                            "function values(controls, v, state, speed, inputs)\n"
                            "    return cab.values(controls, v, state, speed, inputs)\n"
                            "end\n"
                            "function commands(controls, index, value, v, state)\n"
                            "    return cab.commands(controls, index, value, v, state)\n"
                            "end\n"
                            "function stops(controls, v, state)\n"
                            "    if cab.stops then return cab.stops(controls, v, state) end\n"
                            "end\n";
    const int script = lua().loadText(text, QString::fromUtf8(module));
    if (script < 0)
        qWarning().noquote() << "Loco scripts:" << module << lua().lastError();
    cabModules.insert(module, script);
    return script;
}

// A cab's controls as their module reads them (CabControls::fields: a .cvf's
// {type, kind, units, style, min, max, order}), in their order; made once a
// cab.
void LocoScripts::pushCabControls(const CabControls &controls) {
    Script::State &s = lua();
    const auto known = cabControls.constFind(&controls);
    if (known != cabControls.constEnd()) {
        s.push(known.value());
        return;
    }
    lua_State *L = s.lua();
    lua_createtable(L, controls.count(), 0);
    QVector<QPair<QByteArray, QVariant>> fields;
    for (int i = 0; i < controls.count(); ++i) {
        controls.fields(i, fields);
        lua_createtable(L, 0, int(fields.size()));
        for (const auto &[key, value] : fields) {
            switch (value.typeId()) {
            case QMetaType::Bool:
                lua_pushboolean(L, value.toBool());
                break;
            case QMetaType::Int:
            case QMetaType::LongLong:
                lua_pushinteger(L, lua_Integer(value.toLongLong()));
                break;
            case QMetaType::Double:
            case QMetaType::Float:
                lua_pushnumber(L, value.toDouble());
                break;
            default: {
                const QByteArray bytes = value.toString().toUtf8();
                lua_pushlstring(L, bytes.constData(), size_t(bytes.size()));
            }
            }
            lua_setfield(L, -2, key.constData());
        }
        lua_rawseti(L, -2, lua_Integer(i + 1));
    }
    cabControls.insert(&controls, s.keep(-1));
}

int LocoScripts::load(const QString &path) {
    const auto found = loaded.constFind(path);
    if (found != loaded.cend())
        return *found;
    Script::State &L = lua();
    const int script = L.loadFile(path);
    if (script < 0)
        errors.insert(path, L.lastError());
    loaded.insert(path, script);
    return script;
}

std::unique_ptr<VehicleModel> LocoScripts::make(const std::shared_ptr<const VehicleDefinition> &definition) {
    const QString path = scriptFor(*definition);
    if (path.isEmpty())
        return nullptr;
    definitions.insert(definition.get(), definition);
    const int script = load(path);
    return std::make_unique<LuaLoco>(*this, script, definition, script < 0 ? errors.value(path) : QString());
}

}
