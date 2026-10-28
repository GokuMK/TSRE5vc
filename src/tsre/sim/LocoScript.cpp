#include <tsre/sim/LocoScript.h>
#include <QFileInfo>
#include <cmath>
#include <limits>
#include <tsre/script/LuaApi.h>
#include <tsre/script/ScriptState.h>
#include <tsre/trains/VehicleDefinition.h>
#include <tsre/trains/VehicleKeys.h>

namespace Sim {

namespace {

using Item = SimisDocument::Item;

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

// vehicle.number(path [, index [, default]])
int vehicleNumber(lua_State *L) {
    const VehicleDefinition *d = definitionOf(L);
    const QString path = pathArgument(L, 1);
    const int index = int(luaL_optinteger(L, 2, 1)) - 1;
    const double value = d->number(path, index, std::numeric_limits<double>::quiet_NaN());
    if (std::isnan(value)) {
        if (lua_isnoneornil(L, 3))
            lua_pushnil(L);
        else
            lua_pushvalue(L, 3);
    } else {
        lua_pushnumber(L, value);
    }
    return 1;
}

// vehicle.text(path [, index])
int vehicleText(lua_State *L) {
    const VehicleDefinition *d = definitionOf(L);
    const QString path = pathArgument(L, 1);
    const Item value = d->document->values(d->document->last(path)).value(int(luaL_optinteger(L, 2, 1)) - 1);
    if (value.tree)
        pushText(L, d->document->text(value));
    else
        lua_pushnil(L);
    return 1;
}

// vehicle.values(path): the last such block's values.
int vehicleValues(lua_State *L) {
    const VehicleDefinition *d = definitionOf(L);
    const QString path = pathArgument(L, 1);
    const Item block = d->document->last(path);
    if (!block.tree)
        lua_pushnil(L);
    else
        pushBlockValues(L, *d, path, block);
    return 1;
}

// vehicle.all(path): every such block's values.
int vehicleAll(lua_State *L) {
    const VehicleDefinition *d = definitionOf(L);
    const QString path = pathArgument(L, 1);
    const QVector<Item> blocks = d->document->all(path);
    lua_createtable(L, int(blocks.size()), 0);
    for (int i = 0; i < blocks.size(); ++i) {
        pushBlockValues(L, *d, path, blocks[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

// vehicle.blocks(path): the blocks in the last such block, in order, each
// {name = its name (a number where it reads as one), values = {...}}; the
// curves' form (ORTSMaxTractiveForceCurves ( 0 ( v F ... ) 0.5 ( ... ) )).
int vehicleBlocks(lua_State *L) {
    const VehicleDefinition *d = definitionOf(L);
    const QString path = pathArgument(L, 1);
    const SimisDocument &doc = *d->document;
    const Item block = doc.last(path);
    if (!block.tree) {
        lua_pushnil(L);
        return 1;
    }
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
        pushBlockValues(L, *d, path + u'(' + name.toString().toLower(), item);
        lua_setfield(L, -2, "values");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

// vehicle.has(path)
int vehicleHas(lua_State *L) {
    const VehicleDefinition *d = definitionOf(L);
    lua_pushboolean(L, d->document->last(pathArgument(L, 1)).tree != nullptr);
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

// The `vehicle` table init() gets.
void pushVehicle(lua_State *L, const VehicleDefinition *d) {
    pushProxy(L, d, QString());
    const std::pair<const char *, lua_CFunction> functions[] = {
            {"number", vehicleNumber}, {"text", vehicleText}, {"values", vehicleValues},
            {"all", vehicleAll}, {"blocks", vehicleBlocks}, {"has", vehicleHas}};
    for (const auto &[name, function] : functions) {
        lua_pushlightuserdata(L, const_cast<VehicleDefinition *>(d));
        lua_pushcclosure(L, function, 1);
        lua_setfield(L, -2, name);
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

    void command(const Message &m) override { run(m, false); }
    void fromLine(const Message &m) override { run(m, true); }

    void step(const VehicleInputs &in, VehicleOutputs &out) override {
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
                {"lineVoltage", in.lineVoltage}, {"brakePipe", in.brakePipe},
                {"brakePipeVolume", in.brakePipeVolume}, {"brakeCylinder", in.brakeCylinder}};
        for (const auto &[name, value] : numbers) {
            lua_pushnumber(L, value);
            lua_setfield(L, -2, name);
        }
        lua_pushboolean(L, in.cab);
        lua_setfield(L, -2, "cab");
        lua->push(stateRef);
        lua->push(outRef);
        for (const char *name : {"motiveForce", "brakeForce", "brakePipeAir", "independentBrake", "bailOff"}) {
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
        out.independentBrake = std::max(0.0, numberField(L, "independentBrake"));
        out.bailOff = std::max(0.0, numberField(L, "bailOff"));
        lua_getfield(L, -1, "values");
        if (lua_istable(L, -1)) {
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                MessageValue value;
                if (lua_type(L, -2) == LUA_TSTRING && messageValue(L, -1, value))
                    out.values.insert(QString::fromUtf8(lua_tostring(L, -2)), value);
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
    // Why its script stopped; told once.
    QString problem;
    bool told = false;

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

Script::State &LocoScripts::lua() {
    if (!state) {
        state = std::make_shared<Script::State>(Script::State::Kind::Simulation, Script::Limits(), seed);
        state->setModuleRoots({folder});
    }
    return *state;
}

QString LocoScripts::scriptFor(const VehicleDefinition &definition) const {
    // An electric engine (the editors' kind 4).
    if (folder.isEmpty() || definition.wagonTypeId != 4)
        return QString();
    return folder + "/electric.lua";
}

int LocoScripts::load(const QString &path) {
    const auto found = loaded.constFind(path);
    if (found != loaded.cend())
        return *found;
    Script::State &L = lua();
    const int script = L.loadFile(path);
    if (script >= 0) {
        L.pushEnvironment(script);
        lua_pushlightuserdata(L.lua(), this);
        lua_pushcclosure(L.lua(), send, 1);
        lua_setfield(L.lua(), -2, "send");
        lua_pushlightuserdata(L.lua(), this);
        lua_pushcclosure(L.lua(), line, 1);
        lua_setfield(L.lua(), -2, "line");
        lua_pop(L.lua(), 1);
    } else {
        errors.insert(path, L.lastError());
    }
    loaded.insert(path, script);
    return script;
}

std::unique_ptr<VehicleModel> LocoScripts::make(const std::shared_ptr<const VehicleDefinition> &definition) {
    const QString path = scriptFor(*definition);
    if (path.isEmpty())
        return nullptr;
    const int script = load(path);
    return std::make_unique<LuaLoco>(*this, script, definition, script < 0 ? errors.value(path) : QString());
}

}
