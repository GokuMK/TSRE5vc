/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SCRIPTSTATE_H
#define SCRIPTSTATE_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

struct lua_State;
struct lua_Debug;

namespace Script {

class Watchdog;

// How much a state may hold and how long one call may run.
struct Limits {
    // Bytes the state may hold.
    size_t memory = size_t(64) << 20;
    // Milliseconds one call from TSRE may run.
    int callMs = 100;
};

// A Lua state of TSRE's (task scripting 01), used by one thread only: the
// UI's (RmlUi's documents) or the simulation's (loco and signal scripts).
// Sandboxed: base, coroutine, math, string, table and utf8 only; no files,
// processes or C modules; `load` reads text only; `require` finds modules
// in the state's module folders; `print` writes to TSRE's log. Each script
// loaded here runs in an environment of its own, reading the libraries
// through it, so scripts share no globals; the simulation's state takes no
// new global in the libraries' table either (the UI's does: RmlUi's
// documents keep theirs there). While Lua runs under a deadline
// (every call TSRE makes) it stops with an error past its time, and the
// state holds no more memory than its limit; TSRE's own values pushed
// between calls are not refused (Lua would abort on an error there).
// Past its time, a watchdog thread stops it (ScriptState.cpp): Lua runs
// without a hook until then. Coroutines, and every Lua on Windows, look at
// the clock in a hook instead, every thousand instructions.
class State {
public:
    // The UI's state also has os.clock, os.date, os.time and os.difftime.
    // The simulation's has no wall clock, and its random numbers start
    // from the run's seed, so a run repeats.
    enum class Kind { Ui, Simulation };
    using Limits = Script::Limits;

    explicit State(Kind kind, Limits limits = Limits(), unsigned seed = 0);
    ~State();
    State(const State &) = delete;
    State &operator=(const State &) = delete;

    lua_State *lua() const { return L; }
    Kind kind() const { return stateKind; }
    const Limits &limits() const { return stateLimits; }
    size_t memoryUsed() const { return used; }
    // The deadline that Lua is running under has passed, and its time.
    bool timedOut() const { return expired; }
    int timeLimitMs() const { return deadlineMs; }
    // The state a Lua C function runs in.
    static State *of(lua_State *L);

    // Folders `require` looks in, in order: module `a.b` is `a/b.lua` under
    // one of them, named as written (TSRE does no case search).
    void setModuleRoots(const QStringList &roots);
    // Where `print` goes; TSRE's log by default.
    void setPrinter(std::function<void(const QString &)> printer);

    // A script: its chunk run once in its own environment. Returns the
    // script's number, or -1 with lastError().
    int loadFile(const QString &path);
    int loadText(const QByteArray &text, const QString &name);
    void unload(int script);
    // A script is not called after an error, until it is loaded again.
    bool failed(int script) const;
    QString scriptName(int script) const;
    // Pushes the script's environment (its globals).
    void pushEnvironment(int script) const;

    // A call of a script's function: prepare() pushes the function (false,
    // nothing pushed, when the script failed or has no such function), the
    // caller pushes the arguments, call() runs it under the time limit and
    // leaves `results` values; after an error none, the error logged once
    // and the script marked failed.
    bool prepare(int script, const char *function);
    bool call(int arguments, int results);
    QString lastError() const { return error; }

    // Values TSRE keeps between calls (a loco's state table): the value at
    // `index` kept, pushed again, released.
    int keep(int index);
    void push(int kept) const;
    void release(int kept);

    // A value made of nil, booleans, numbers, strings and tables of them
    // (keys strings or integers) as Lua text, and back. Integers stay
    // integers and floats are exact; keys are written in order, so the same
    // value gives the same text. Metatables are not saved; a table reached
    // twice is saved twice. Functions, userdata, threads and cycles are
    // refused with the path to them.
    bool save(int index, QByteArray &text);
    // Pushes the value saved; false, nothing pushed, with lastError().
    bool restore(const QByteArray &text);

    // While one lives, Lua running in this state stops with an error past
    // `milliseconds` (the call limit when negative), whether TSRE calls it
    // or a library does (RmlUi's Lua plugin) under it. Nested deadlines keep
    // the earliest end. Once past it, pcall, xpcall and coroutine.resume
    // pass the error on, so a script cannot catch it and go on.
    class Deadline {
    public:
        explicit Deadline(State &state, int milliseconds = -1);
        ~Deadline();
        Deadline(const Deadline &) = delete;
        Deadline &operator=(const Deadline &) = delete;
    private:
        State &state;
        bool outer;
        std::chrono::steady_clock::time_point previous;
        int previousMs;
    };

private:
    struct Entry {
        int environment = -1;
        QString name;
        bool failed = false;
    };

    lua_State *L = nullptr;
    Kind stateKind;
    Limits stateLimits;
    size_t used = 0;
    std::thread::id owner;
    QStringList moduleRoots;
    std::function<void(const QString &)> printer;
    std::vector<Entry> scripts;
    int preparedScript = -1;
    QString error;

    bool deadlineActive = false;
    bool expired = false;
    std::chrono::steady_clock::time_point deadline;
    int deadlineMs = 0;
    // The watchdog stopping Lua here, null where the hook looks at the
    // clock. Under its lock, with the deadline above: the thread running
    // under the deadline and whether the watchdog has set the hook there.
    Watchdog *watchdog = nullptr;
    std::thread::native_handle_type deadlineThread{};
    bool interrupted = false;
    friend class Watchdog;

    void checkThread() const;
    void openLibraries(unsigned seed);
    // Pushes a new environment reading the libraries.
    void pushNewEnvironment();
    // Loads `text` as a chunk with the environment on the stack's top
    // (popped) and runs it under the deadline; leaves its first result.
    bool runChunk(const QByteArray &text, const QString &name);
    // The function below `arguments` values run under the deadline with a
    // traceback; false with the error.
    bool protectedRun(int arguments, int results);
    int addScript(const QString &name);

    static void *allocate(void *ud, void *ptr, size_t oldSize, size_t newSize);
    static void hook(lua_State *L, lua_Debug *ar);
    static int print(lua_State *L);
    static int require(lua_State *L);
    static int load(lua_State *L);
    static int collectGarbage(lua_State *L);
    static int guardedCall(lua_State *L);
    static int newCoroutine(lua_State *L);
    static int newGlobal(lua_State *L);
    static int traceback(lua_State *L);
};

}

#endif // SCRIPTSTATE_H
