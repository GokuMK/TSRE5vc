/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/script/ScriptState.h>
#include <tsre/script/LuaApi.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#ifndef Q_OS_WIN
#include <cerrno>
#include <csignal>
#include <pthread.h>
#endif

namespace Script {

namespace {

// VM instructions between two looks at the clock, where a hook looks.
const int HookCount = 1000;
// How often the watchdog looks while calls keep starting; asleep otherwise.
const std::chrono::milliseconds WatchdogPoll(50);
#ifndef Q_OS_WIN
// The signal stopping a thread for the watchdog: ignored by default, used
// for nothing else in TSRE.
const int InterruptSignal = SIGURG;
#endif
// Tables nested deeper are not saved.
const int SaveDepth = 200;
const char *ModulesKey = "tsre.modules";

bool isIdentifier(const QByteArray &name) {
    static const char *const reserved[] = {"and", "break", "do", "else", "elseif", "end", "false", "for",
                                           "function", "global", "goto", "if", "in", "local", "nil", "not",
                                           "or", "repeat", "return", "then", "true", "until", "while"};
    if (name.isEmpty() || !(std::isalpha((unsigned char)name[0]) || name[0] == '_'))
        return false;
    for (char c : name)
        if (!(std::isalnum((unsigned char)c) || c == '_'))
            return false;
    for (const char *word : reserved)
        if (name == word)
            return false;
    return true;
}

void writeString(QByteArray &out, const char *text, size_t size) {
    out += '"';
    for (size_t i = 0; i < size; ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c < 32 || c == 127) {
            char escape[8];
            std::snprintf(escape, sizeof(escape), "\\%03d", c);
            out += escape;
        } else {
            out += char(c);
        }
    }
    out += '"';
}

void writeNumber(lua_State *L, int index, QByteArray &out) {
    if (lua_isinteger(L, index)) {
        const lua_Integer value = lua_tointeger(L, index);
        // Its digits would read as a float.
        if (value == LLONG_MIN)
            out += "(-9223372036854775807-1)";
        else
            out += QByteArray::number(qlonglong(value));
        return;
    }
    const double value = lua_tonumber(L, index);
    if (value != value) {
        out += "(0/0)";
    } else if (value == HUGE_VAL || value == -HUGE_VAL) {
        out += value > 0 ? "(1/0)" : "(-1/0)";
    } else if (value == 0) {
        // Its sign kept (Qt writes -0 as 0).
        out += std::signbit(value) ? "-0.0" : "0.0";
    } else {
        // The shortest digits that read back exactly; a point keeps it a
        // float.
        QByteArray digits = QByteArray::number(value, 'g', QLocale::FloatingPointShortest);
        if (!digits.contains('.') && !digits.contains('e'))
            digits += ".0";
        out += digits;
    }
}

// The value at `index` written to `out`; false with the error and the path
// to what cannot be saved.
bool writeValue(lua_State *L, int index, QByteArray &out, const QString &path,
                std::vector<const void *> &open, QString &error) {
    switch (lua_type(L, index)) {
    case LUA_TNIL:
        out += "nil";
        return true;
    case LUA_TBOOLEAN:
        out += lua_toboolean(L, index) ? "true" : "false";
        return true;
    case LUA_TNUMBER:
        writeNumber(L, index, out);
        return true;
    case LUA_TSTRING: {
        size_t size = 0;
        const char *text = lua_tolstring(L, index, &size);
        writeString(out, text, size);
        return true;
    }
    case LUA_TTABLE:
        break;
    default:
        error = QString("%1 is a %2, which cannot be saved").arg(path, luaL_typename(L, index));
        return false;
    }
    const void *table = lua_topointer(L, index);
    if (std::find(open.begin(), open.end(), table) != open.end()) {
        error = QString("%1 holds itself (a cycle), which cannot be saved").arg(path);
        return false;
    }
    if (int(open.size()) >= SaveDepth || !lua_checkstack(L, 4)) {
        error = QString("%1 is nested deeper than %2 tables").arg(path).arg(SaveDepth);
        return false;
    }
    std::vector<lua_Integer> integers;
    std::vector<QByteArray> strings;
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        if (lua_type(L, -2) == LUA_TNUMBER && lua_isinteger(L, -2)) {
            integers.push_back(lua_tointeger(L, -2));
        } else if (lua_type(L, -2) == LUA_TSTRING) {
            size_t size = 0;
            const char *text = lua_tolstring(L, -2, &size);
            strings.push_back(QByteArray(text, int(size)));
        } else {
            error = QString("%1 has a key that is a %2 (only strings and integers can be saved)")
                        .arg(path, luaL_typename(L, -2));
            lua_pop(L, 2);
            return false;
        }
        lua_pop(L, 1);
    }
    std::sort(integers.begin(), integers.end());
    std::sort(strings.begin(), strings.end());
    // 1..n without keys.
    bool sequence = true;
    for (size_t i = 0; i < integers.size() && sequence; ++i)
        sequence = integers[i] == lua_Integer(i + 1);
    open.push_back(table);
    out += '{';
    bool first = true;
    for (lua_Integer key : integers) {
        if (!first)
            out += ',';
        first = false;
        if (!sequence) {
            out += '[';
            if (key == LLONG_MIN)
                out += "(-9223372036854775807-1)";
            else
                out += QByteArray::number(qlonglong(key));
            out += "]=";
        }
        lua_rawgeti(L, index, key);
        const bool written = writeValue(L, lua_gettop(L), out, QString("%1[%2]").arg(path).arg(key), open, error);
        lua_pop(L, 1);
        if (!written)
            return false;
    }
    for (const QByteArray &key : strings) {
        if (!first)
            out += ',';
        first = false;
        QString keyPath;
        if (isIdentifier(key)) {
            out += key;
            keyPath = path + "." + QString::fromUtf8(key);
        } else {
            out += '[';
            writeString(out, key.constData(), size_t(key.size()));
            out += ']';
            keyPath = QString("%1[\"%2\"]").arg(path, QString::fromUtf8(key));
        }
        out += '=';
        lua_pushlstring(L, key.constData(), size_t(key.size()));
        lua_rawget(L, index);
        const bool written = writeValue(L, lua_gettop(L), out, keyPath, open, error);
        lua_pop(L, 1);
        if (!written)
            return false;
    }
    out += '}';
    open.pop_back();
    return true;
}

int raiseTimeOut(lua_State *L, State *state) {
    return luaL_error(L, "the script ran longer than its limit of %d ms", state->timeLimitMs());
}

// After a protected call returned: past the deadline, the error goes on.
int afterGuardedCall(lua_State *L, int, lua_KContext) {
    State *state = State::of(L);
    if (state->timedOut())
        return raiseTimeOut(L, state);
    return lua_gettop(L);
}

}

// One thread for all states, waiting for the end of the deadlines Lua runs
// under. Past one, it stops the deadline's thread for a moment with a
// signal, whose handler sets there the hook that ends the script: Lua's
// hooks may be set from a signal handler, as Lua's own interpreter stops a
// script, but not from another thread. So Lua runs without a hook until
// then. The handler cannot tell which of a state's threads runs, so the
// coroutines keep a hook looking at the clock.
class Watchdog {
public:
    // Started on first use; null where it cannot stop a thread (Windows):
    // there Lua looks at the clock in a hook.
    static Watchdog *get();

    void add(State *state);
    void remove(State *state);
    // Under the lock: a deadline began or moved to `end`.
    void armed(std::chrono::steady_clock::time_point end);
    static std::thread::native_handle_type currentThread();

    std::mutex lock;

private:
    std::condition_variable wake;
    std::vector<State *> states;
    std::chrono::steady_clock::time_point wakeAt = std::chrono::steady_clock::time_point::max();
    bool started = false;
#ifndef Q_OS_WIN
    static std::atomic<State *> target;
    static std::atomic<bool> hooked;
    static void handler(int);
#endif

    void run();
    void interrupt(State *state);
};

#ifdef Q_OS_WIN
Watchdog *Watchdog::get() {
    return nullptr;
}

std::thread::native_handle_type Watchdog::currentThread() {
    return {};
}

void Watchdog::interrupt(State *) {
}
#else
std::atomic<State *> Watchdog::target{nullptr};
std::atomic<bool> Watchdog::hooked{false};

Watchdog *Watchdog::get() {
    static Watchdog *const watchdog = []() -> Watchdog * {
        struct sigaction previous {};
        if (sigaction(InterruptSignal, nullptr, &previous) != 0 || (previous.sa_flags & SA_SIGINFO) != 0
            || (previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN)) {
            qWarning() << "Lua: the watchdog's signal is taken, scripts look at the clock as they run";
            return nullptr;
        }
        struct sigaction action {};
        action.sa_handler = handler;
        action.sa_flags = SA_RESTART;
        sigemptyset(&action.sa_mask);
        if (sigaction(InterruptSignal, &action, nullptr) != 0)
            return nullptr;
        // Kept to the end: states may outlive any owner of it.
        Watchdog *made = new Watchdog;
        std::thread(&Watchdog::run, made).detach();
        return made;
    }();
    return watchdog;
}

std::thread::native_handle_type Watchdog::currentThread() {
    return pthread_self();
}

void Watchdog::handler(int) {
    const int savedErrno = errno;
    State *state = target.load();
    // Only in the thread it was sent to: a late signal finds none.
    if (state != nullptr && pthread_equal(state->deadlineThread, pthread_self())) {
        lua_sethook(state->L, State::hook, LUA_MASKCOUNT, 1);
        hooked.store(true);
    }
    errno = savedErrno;
}

// Under the lock: the deadline's thread is inside its Deadline (taking the
// lock at its end), so it lives.
void Watchdog::interrupt(State *state) {
    hooked.store(false);
    target.store(state);
    if (pthread_kill(state->deadlineThread, InterruptSignal) == 0) {
        const auto giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (!hooked.load() && std::chrono::steady_clock::now() < giveUp)
            std::this_thread::yield();
    }
    target.store(nullptr);
    if (!hooked.load())
        qWarning() << "Lua: the watchdog could not stop a script past its time";
}
#endif

void Watchdog::add(State *state) {
    std::lock_guard<std::mutex> guard(lock);
    states.push_back(state);
}

void Watchdog::remove(State *state) {
    std::lock_guard<std::mutex> guard(lock);
    states.erase(std::remove(states.begin(), states.end(), state), states.end());
}

void Watchdog::armed(std::chrono::steady_clock::time_point end) {
    started = true;
    if (end < wakeAt)
        wake.notify_one();
}

// Wakes at the earliest end, and while calls keep starting every poll, so
// that a call's start does not wake it (a deadline ending before the next
// look does).
void Watchdog::run() {
    std::unique_lock<std::mutex> guard(lock);
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        auto next = std::chrono::steady_clock::time_point::max();
        for (State *state : states) {
            if (!state->deadlineActive || state->interrupted)
                continue;
            if (state->deadline <= now) {
                interrupt(state);
                state->interrupted = true;
            } else {
                next = std::min(next, state->deadline);
            }
        }
        if (started)
            next = std::min(next, now + WatchdogPoll);
        started = false;
        wakeAt = next;
        if (next == std::chrono::steady_clock::time_point::max())
            wake.wait(guard);
        else
            wake.wait_until(guard, next);
    }
}

State::State(Kind kind, Limits limits, unsigned seed)
    : stateKind(kind), stateLimits(limits), owner(std::this_thread::get_id()) {
    printer = [](const QString &text) { qInfo().noquote() << text; };
    L = lua_newstate(allocate, this, seed);
    *static_cast<State **>(lua_getextraspace(L)) = this;
    watchdog = Watchdog::get();
    if (watchdog != nullptr)
        watchdog->add(this);
    else
        lua_sethook(L, hook, LUA_MASKCOUNT, HookCount);
    openLibraries(seed);
}

State::~State() {
    if (watchdog != nullptr)
        watchdog->remove(this);
    lua_close(L);
}

State *State::of(lua_State *L) {
    return *static_cast<State **>(lua_getextraspace(L));
}

void State::checkThread() const {
    Q_ASSERT_X(std::this_thread::get_id() == owner, "Script::State", "used by a thread other than its own");
}

void *State::allocate(void *ud, void *ptr, size_t oldSize, size_t newSize) {
    State *state = static_cast<State *>(ud);
    // Without a block, oldSize is the kind of object made.
    const size_t old = ptr != nullptr ? oldSize : 0;
    if (newSize == 0) {
        std::free(ptr);
        state->used -= old;
        return nullptr;
    }
    if (newSize > old && state->deadlineActive && state->used - old + newSize > state->stateLimits.memory)
        return nullptr;
    void *block = std::realloc(ptr, newSize);
    if (block != nullptr)
        state->used = state->used - old + newSize;
    return block;
}

void State::hook(lua_State *L, lua_Debug *) {
    State *state = of(L);
    if (!state->deadlineActive)
        return;
    if (state->expired || std::chrono::steady_clock::now() >= state->deadline) {
        state->expired = true;
        raiseTimeOut(L, state);
    }
}

void State::openLibraries(unsigned seed) {
    int libraries = LUA_GLIBK | LUA_COLIBK | LUA_MATHLIBK | LUA_STRLIBK | LUA_TABLIBK | LUA_UTF8LIBK;
    if (stateKind == Kind::Ui)
        libraries |= LUA_OSLIBK;
    luaL_openselectedlibs(L, libraries, 0);

    lua_pushglobaltable(L);
    const int globals = lua_gettop(L);
    // No files.
    lua_pushnil(L);
    lua_setfield(L, globals, "dofile");
    lua_pushnil(L);
    lua_setfield(L, globals, "loadfile");
    // A library function replaced by ours, given the original.
    auto wrap = [this](int table, const char *name, lua_CFunction function) {
        lua_getfield(L, table, name);
        lua_pushcclosure(L, function, 1);
        lua_setfield(L, table, name);
    };
    wrap(globals, "load", load);
    wrap(globals, "collectgarbage", collectGarbage);
    wrap(globals, "pcall", guardedCall);
    wrap(globals, "xpcall", guardedCall);
    lua_getfield(L, globals, "coroutine");
    wrap(lua_gettop(L), "resume", guardedCall);
    if (watchdog != nullptr) {
        wrap(lua_gettop(L), "create", newCoroutine);
        wrap(lua_gettop(L), "wrap", newCoroutine);
    }
    lua_pop(L, 1);
    lua_pushcfunction(L, print);
    lua_setfield(L, globals, "print");
    lua_pushcfunction(L, require);
    lua_setfield(L, globals, "require");

    if (stateKind == Kind::Ui) {
        // The clock and dates only: no files, processes or environment.
        lua_getfield(L, globals, "os");
        const int os = lua_gettop(L);
        lua_createtable(L, 0, 4);
        for (const char *name : {"clock", "date", "time", "difftime"}) {
            lua_getfield(L, os, name);
            lua_setfield(L, -2, name);
        }
        lua_setfield(L, globals, "os");
        lua_pop(L, 1);
    } else {
        // Random numbers from the run's seed.
        lua_getfield(L, globals, "math");
        lua_getfield(L, -1, "randomseed");
        lua_pushinteger(L, lua_Integer(seed));
        lua_call(L, 1, 0);
        lua_pop(L, 1);
    }

    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, ModulesKey);

    // The simulation's libraries take no new globals: each script keeps its
    // own. The UI's documents share theirs, as RmlUi's Lua plugin has it
    // (their scripts and handlers run in the global environment).
    if (stateKind == Kind::Simulation) {
        lua_createtable(L, 0, 2);
        lua_pushcfunction(L, newGlobal);
        lua_setfield(L, -2, "__newindex");
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "__metatable");
        lua_setmetatable(L, globals);
    }
    lua_pop(L, 1);
}

void State::setModuleRoots(const QStringList &roots) {
    moduleRoots = roots;
}

void State::addNativeModule(const QString &name, int (*open)(lua_State *L)) {
    nativeModules.insert(name, open);
}

void State::setPrinter(std::function<void(const QString &)> function) {
    printer = std::move(function);
}

void State::pushNewEnvironment() {
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "_G");
    lua_createtable(L, 0, 1);
    lua_pushglobaltable(L);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
}

bool State::protectedRun(int arguments, int results) {
    const int base = lua_gettop(L) - arguments;
    lua_pushcfunction(L, traceback);
    lua_insert(L, base);
    int status;
    {
        Deadline deadline(*this);
        status = lua_pcall(L, arguments, results, base);
    }
    lua_remove(L, base);
    if (status == LUA_OK)
        return true;
    const char *message = lua_tostring(L, -1);
    error = message != nullptr ? QString::fromUtf8(message) : QString("an error without a message");
    lua_pop(L, 1);
    return false;
}

bool State::runChunk(const QByteArray &text, const QString &name) {
    const QByteArray chunkName = name.toUtf8();
    if (luaL_loadbufferx(L, text.constData(), size_t(text.size()), chunkName.constData(), "t") != LUA_OK) {
        error = QString::fromUtf8(lua_tostring(L, -1));
        lua_pop(L, 2);
        return false;
    }
    // The environment as the chunk's _ENV.
    lua_insert(L, -2);
    lua_setupvalue(L, -2, 1);
    return protectedRun(0, 1);
}

int State::addScript(const QString &name) {
    Entry entry;
    entry.name = name;
    scripts.push_back(entry);
    return int(scripts.size()) - 1;
}

int State::loadFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QString("%1: cannot be read").arg(path);
        qWarning().noquote() << "[script]" << error;
        return -1;
    }
    return loadText(file.readAll(), path);
}

int State::loadText(const QByteArray &text, const QString &name) {
    checkThread();
    pushNewEnvironment();
    lua_pushvalue(L, -1);
    if (!runChunk(text, "@" + name)) {
        lua_pop(L, 1);
        qWarning().noquote() << "[script]" << error;
        return -1;
    }
    lua_pop(L, 1);
    const int script = addScript(name);
    scripts[size_t(script)].environment = luaL_ref(L, LUA_REGISTRYINDEX);
    return script;
}

void State::unload(int script) {
    checkThread();
    if (script < 0 || script >= int(scripts.size()))
        return;
    Entry &entry = scripts[size_t(script)];
    if (entry.environment >= 0)
        luaL_unref(L, LUA_REGISTRYINDEX, entry.environment);
    entry.environment = -1;
    entry.failed = true;
}

bool State::failed(int script) const {
    return script < 0 || script >= int(scripts.size()) || scripts[size_t(script)].failed;
}

QString State::scriptName(int script) const {
    return script < 0 || script >= int(scripts.size()) ? QString() : scripts[size_t(script)].name;
}

void State::pushEnvironment(int script) const {
    if (script < 0 || script >= int(scripts.size()) || scripts[size_t(script)].environment < 0)
        lua_pushnil(L);
    else
        lua_rawgeti(L, LUA_REGISTRYINDEX, scripts[size_t(script)].environment);
}

bool State::prepare(int script, const char *function) {
    checkThread();
    if (failed(script))
        return false;
    lua_rawgeti(L, LUA_REGISTRYINDEX, scripts[size_t(script)].environment);
    // The script's own function, not a library's of that name.
    lua_pushstring(L, function);
    lua_rawget(L, -2);
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    preparedScript = script;
    return true;
}

bool State::call(int arguments, int results) {
    checkThread();
    const int script = preparedScript;
    preparedScript = -1;
    if (protectedRun(arguments, results))
        return true;
    if (script >= 0)
        scripts[size_t(script)].failed = true;
    qWarning().noquote() << "[script]" << error;
    return false;
}

int State::keep(int index) {
    checkThread();
    lua_pushvalue(L, index);
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

void State::push(int kept) const {
    lua_rawgeti(L, LUA_REGISTRYINDEX, kept);
}

void State::release(int kept) {
    checkThread();
    luaL_unref(L, LUA_REGISTRYINDEX, kept);
}

bool State::save(int index, QByteArray &text) {
    checkThread();
    index = lua_absindex(L, index);
    QByteArray out("return ");
    std::vector<const void *> open;
    if (!writeValue(L, index, out, "value", open, error))
        return false;
    text = out;
    return true;
}

bool State::restore(const QByteArray &text) {
    checkThread();
    // An empty environment: the text can only build values.
    lua_newtable(L);
    if (!runChunk(text, "=saved"))
        return false;
    QByteArray check;
    if (!save(-1, check)) {
        error = "the saved text gives what cannot be saved: " + error;
        lua_pop(L, 1);
        return false;
    }
    return true;
}

State::Deadline::Deadline(State &state, int milliseconds)
    : state(state), outer(!state.deadlineActive), previous(state.deadline), previousMs(state.deadlineMs) {
    const int ms = milliseconds < 0 ? state.stateLimits.callMs : milliseconds;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    std::unique_lock<std::mutex> guard;
    if (state.watchdog != nullptr)
        guard = std::unique_lock<std::mutex>(state.watchdog->lock);
    if (outer) {
        state.deadlineActive = true;
        state.expired = false;
        state.deadline = end;
        state.deadlineMs = ms;
        if (state.watchdog != nullptr) {
            state.deadlineThread = Watchdog::currentThread();
            state.watchdog->armed(end);
        }
    } else if (end < state.deadline) {
        state.deadline = end;
        state.deadlineMs = ms;
        if (state.watchdog != nullptr)
            state.watchdog->armed(end);
    }
}

State::Deadline::~Deadline() {
    std::unique_lock<std::mutex> guard;
    if (state.watchdog != nullptr)
        guard = std::unique_lock<std::mutex>(state.watchdog->lock);
    if (outer) {
        state.deadlineActive = false;
    } else {
        state.deadline = previous;
        state.deadlineMs = previousMs;
    }
    // An inner deadline's end is not the outer one's.
    state.expired = false;
    // The watchdog's hook taken off: an outer deadline is watched again.
    if (state.interrupted) {
        lua_sethook(state.L, nullptr, 0, 0);
        state.interrupted = false;
        if (!outer)
            state.watchdog->armed(state.deadline);
    }
}

int State::print(lua_State *L) {
    State *state = of(L);
    QString text;
    const int count = lua_gettop(L);
    for (int i = 1; i <= count; ++i) {
        size_t size = 0;
        const char *part = luaL_tolstring(L, i, &size);
        if (i > 1)
            text += '\t';
        text += QString::fromUtf8(part, int(size));
        lua_pop(L, 1);
    }
    lua_Debug ar;
    QString where;
    if (lua_getstack(L, 1, &ar) && lua_getinfo(L, "Sl", &ar))
        where = QString("%1:%2: ").arg(QString::fromUtf8(ar.short_src)).arg(ar.currentline);
    state->printer("[script] " + where + text);
    return 0;
}

int State::require(lua_State *L) {
    State *state = of(L);
    const QString name = QString::fromUtf8(luaL_checkstring(L, 1));
    static const QRegularExpression dotted("^[A-Za-z_][A-Za-z0-9_]*(\\.[A-Za-z_][A-Za-z0-9_]*)*$");
    if (!dotted.match(name).hasMatch())
        return luaL_error(L, "module name '%s' is not a dotted name", name.toUtf8().constData());
    // TSRE's own, built in: opened once, kept beside the files'.
    const auto native = state->nativeModules.constFind(name);
    if (native != state->nativeModules.cend()) {
        const QByteArray key = "native:" + name.toUtf8();
        lua_getfield(L, LUA_REGISTRYINDEX, ModulesKey);
        const int modules = lua_gettop(L);
        if (lua_getfield(L, modules, key.constData()) != LUA_TNIL)
            return 1;
        lua_pop(L, 1);
        lua_pushcfunction(L, *native);
        lua_call(L, 0, 1);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_pushboolean(L, 1);
        }
        lua_pushvalue(L, -1);
        lua_setfield(L, modules, key.constData());
        return 1;
    }
    const QString relative = QString(name).replace('.', '/') + ".lua";
    QString path;
    for (const QString &root : state->moduleRoots) {
        const QString candidate = QDir(root).filePath(relative);
        if (QFileInfo(candidate).isFile()) {
            path = candidate;
            break;
        }
    }
    if (path.isEmpty())
        return luaL_error(L, "module '%s' is not in the module folders", name.toUtf8().constData());
    const QByteArray key = QDir::cleanPath(path).toUtf8();

    lua_getfield(L, LUA_REGISTRYINDEX, ModulesKey);
    const int modules = lua_gettop(L);
    lua_getfield(L, modules, key.constData());
    if (lua_islightuserdata(L, -1))
        return luaL_error(L, "module '%s' requires itself", name.toUtf8().constData());
    if (!lua_isnil(L, -1))
        return 1;
    lua_pop(L, 1);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return luaL_error(L, "module '%s' cannot be read", name.toUtf8().constData());
    const QByteArray text = file.readAll();
    const QByteArray chunkName = "@" + key;
    if (luaL_loadbufferx(L, text.constData(), size_t(text.size()), chunkName.constData(), "t") != LUA_OK)
        return lua_error(L);
    // Loading: required again before it ends, it requires itself.
    lua_pushlightuserdata(L, state);
    lua_setfield(L, modules, key.constData());
    state->pushNewEnvironment();
    lua_setupvalue(L, -2, 1);
    lua_call(L, 0, 1);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, modules, key.constData());
    return 1;
}

int State::load(lua_State *L) {
    // Text only; an environment given stays given.
    if (lua_gettop(L) < 3)
        lua_settop(L, 3);
    lua_pushliteral(L, "t");
    lua_replace(L, 3);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}

int State::collectGarbage(lua_State *L) {
    const char *option = luaL_optstring(L, 1, "collect");
    if (std::strcmp(option, "collect") != 0 && std::strcmp(option, "count") != 0
            && std::strcmp(option, "step") != 0 && std::strcmp(option, "isrunning") != 0)
        return luaL_error(L, "collectgarbage(\"%s\") is not allowed", option);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}

int State::guardedCall(lua_State *L) {
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_callk(L, lua_gettop(L) - 1, LUA_MULTRET, 0, afterGuardedCall);
    return afterGuardedCall(L, LUA_OK, 0);
}

// coroutine.create and coroutine.wrap where a watchdog stops Lua: the new
// thread looks at the clock in a hook (the watchdog's stops a state's main
// thread only).
int State::newCoroutine(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_settop(L, 1);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, 1, 1);
    lua_State *thread = lua_tothread(L, -1);
    // wrap's function holds its thread as its upvalue.
    if (thread == nullptr && lua_getupvalue(L, -1, 1) != nullptr) {
        thread = lua_tothread(L, -1);
        lua_pop(L, 1);
    }
    Q_ASSERT(thread != nullptr);
    if (thread != nullptr)
        lua_sethook(thread, hook, LUA_MASKCOUNT, HookCount);
    return 1;
}

int State::newGlobal(lua_State *L) {
    const char *name = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : luaL_typename(L, 2);
    return luaL_error(L, "'%s' cannot be set in the shared libraries: each script keeps its own globals", name);
}

int State::traceback(lua_State *L) {
    const char *message = lua_tostring(L, 1);
    if (message == nullptr) {
        if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING)
            return 1;
        message = lua_pushfstring(L, "(an error object that is a %s)", luaL_typename(L, 1));
    }
    luaL_traceback(L, L, message, 1);
    return 1;
}

}
