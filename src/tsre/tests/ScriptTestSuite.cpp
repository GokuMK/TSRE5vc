#include "ScriptTestSuite.h"
#include "TokenTestSupport.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <thread>
#include <tsre/script/LuaApi.h>
#include <tsre/script/ScriptState.h>

namespace {

using Script::State;

// The script's function called with string arguments: its result as text,
// "missing" when it has no such function, "error: ..." after an error.
QString callText(State &state, int script, const char *function, const QStringList &arguments = {}) {
    if (!state.prepare(script, function))
        return "missing";
    for (const QString &argument : arguments)
        lua_pushstring(state.lua(), argument.toUtf8().constData());
    if (!state.call(int(arguments.size()), 1))
        return "error: " + state.lastError();
    size_t size = 0;
    const char *text = luaL_tolstring(state.lua(), -1, &size);
    const QString result = QString::fromUtf8(text, int(size));
    lua_pop(state.lua(), 2);
    return result;
}

const char *Probe = R"(
local function has(path)
  local t = _G
  for part in path:gmatch("[^.]+") do
    if type(t) ~= "table" then return false end
    t = t[part]
  end
  return t ~= nil
end
function present(list)
  local found = {}
  for path in list:gmatch("[^,]+") do
    if has(path) then found[#found + 1] = path end
  end
  return table.concat(found, ",")
end
function binary()
  local chunk, message = load(string.dump(function() end))
  return tostring(chunk) .. ": " .. tostring(message)
end
function numbers()
  return tostring(0.5) .. " " .. string.format("%.1f", 2.5)
end
)";

void checkSandbox(TokenTest::Suite &test) {
    State ui(State::Kind::Ui);
    const int probe = ui.loadText(Probe, "probe.lua");
    const QString closed = "io,package,debug,dofile,loadfile,os.execute,os.getenv,os.remove,os.rename,"
                           "os.exit,os.tmpname,os.setlocale";
    test.check(callText(ui, probe, "present", {closed}).isEmpty(),
               "UI: no files, processes, C modules or debug library");
    test.check(callText(ui, probe, "present", {"os.clock,os.date,os.time,os.difftime,string,table,math,utf8,coroutine"})
                   == "os.clock,os.date,os.time,os.difftime,string,table,math,utf8,coroutine",
               "UI: the clock and dates, and the language's libraries");
    test.check(callText(ui, probe, "binary").contains("attempt to load a binary chunk"), "load takes text only");
    test.check(callText(ui, probe, "numbers") == "0.5 2.5", "numbers written with a point");
    test.check(!ui.prepare(probe, "print"), "a library function is not the script's own");

    State simulation(State::Kind::Simulation);
    const int simulationProbe = simulation.loadText(Probe, "probe.lua");
    test.check(callText(simulation, simulationProbe, "present", {"os," + closed}).isEmpty(),
               "simulation: no wall clock either");
}

void checkGlobals(TokenTest::Suite &test) {
    State state(State::Kind::Simulation);
    const int first = state.loadText(R"(
x = 1
_G.y = 2
function get() return tostring(x) .. tostring(y) end
function viaLoad()
  local chunk = load("z = 1")
  return select(2, pcall(chunk))
end
)", "first.lua");
    const int second = state.loadText("function get() return tostring(x) .. tostring(y) end", "second.lua");
    test.check(callText(state, first, "get") == "12" && callText(state, second, "get") == "nilnil",
               "each script keeps its own globals");
    test.check(callText(state, first, "viaLoad").contains("cannot be set in the shared libraries"),
               "simulation: a chunk loaded without an environment cannot set shared globals");
    State ui(State::Kind::Ui);
    const int document = ui.loadText(R"(
function viaLoad()
  local chunk = load("shared = 1")
  chunk()
  return tostring(_ENV.shared) .. " " .. tostring(rawget(getmetatable(_ENV).__index, "shared"))
end
)", "document.lua");
    test.check(callText(ui, document, "viaLoad") == "1 1",
               "UI: the global environment takes globals (RmlUi's documents keep theirs there)");
}

void checkErrors(TokenTest::Suite &test) {
    State state(State::Kind::Ui);
    const int script = state.loadText("function boom()\n  local a = nil\n  return a.b\nend\nfunction fine() return 1 end",
                                      "boom.lua");
    const QString message = callText(state, script, "boom");
    test.check(message.contains("boom.lua:3:") && message.contains("stack traceback"),
               "an error with its file, line and traceback");
    test.check(state.failed(script) && callText(state, script, "fine") == "missing",
               "a script is not called again after an error");
    test.check(state.loadText("function (", "bad.lua") < 0 && state.lastError().contains("bad.lua:1:"),
               "a script that does not compile: its file and line");
}

void checkTimeLimit(TokenTest::Suite &test) {
    State::Limits limits;
    limits.callMs = 50;
    State state(State::Kind::Ui, limits);
    const char *loops = R"(
function spin() while true do end end
function catch() while true do pcall(function() while true do end end) end end
function resume()
  while true do coroutine.resume(coroutine.create(function() while true do end end)) end
end
function wrapped()
  while true do pcall(coroutine.wrap(function() while true do end end)) end
end
function sorted() table.sort({3, 1, 2, 5, 4}, function(a, b) while true do end end) end
)";
    bool stopped = true;
    qint64 longest = 0;
    for (const char *function : {"spin", "catch", "resume", "wrapped", "sorted"}) {
        const int script = state.loadText(loops, "loops.lua");
        QElapsedTimer timer;
        timer.start();
        const QString message = callText(state, script, function);
        longest = std::max(longest, timer.elapsed());
        stopped = stopped && message.contains("ran longer than its limit of 50 ms");
    }
    test.check(stopped && longest < 1000, QString("endless loops stopped at the limit, also when caught (%1 ms)").arg(longest));
    const int after = state.loadText(R"(
function yields()
  local co = coroutine.wrap(function() pcall(function() coroutine.yield(1) end) return 2 end)
  return co() + co()
end
)", "after.lua");
    test.check(callText(state, after, "yields") == "3", "the state goes on; a coroutine yields inside pcall");

    // A library's own protected call under a deadline (as RmlUi's plugin).
    luaL_loadstring(state.lua(), "while true do end");
    int status;
    {
        State::Deadline deadline(state, 30);
        status = lua_pcall(state.lua(), 0, 0, 0);
    }
    const QString message = QString::fromUtf8(lua_tostring(state.lua(), -1));
    lua_pop(state.lua(), 1);
    test.check(status != LUA_OK && message.contains("limit of 30 ms"), "a deadline around a library's own call");
#ifndef Q_OS_WIN
    test.check(lua_gethook(state.lua()) == nullptr, "between calls Lua runs without a hook (a watchdog stops it)");
#endif

    // An inner deadline past its end inside an outer one: the outer goes on
    // to its own end.
    lua_State *L = state.lua();
    auto run = [L](const char *code, QString &error) {
        luaL_loadstring(L, code);
        const int result = lua_pcall(L, 0, 0, 0);
        if (result != LUA_OK) {
            error = QString::fromUtf8(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        return result;
    };
    QElapsedTimer timer;
    timer.start();
    QString inner, between, outer;
    bool unhooked = true;
    int betweenStatus;
    {
        State::Deadline deadline(state, 400);
        {
            State::Deadline shorter(state, 30);
            run("while true do end", inner);
        }
#ifndef Q_OS_WIN
        unhooked = lua_gethook(L) == nullptr;
#endif
        betweenStatus = run("local start = os.clock() while os.clock() - start < 0.1 do end", between);
        run("while true do end", outer);
    }
    test.check(inner.contains("limit of 30 ms") && unhooked && betweenStatus == LUA_OK
                   && outer.contains("limit of 400 ms") && timer.elapsed() < 1500,
               QString("an inner deadline ends, the outer goes on to its own end (%1 ms)").arg(timer.elapsed()));

    // Two states past their time at once, on two threads.
    QString elsewhere;
    std::thread worker([&] {
        State other(State::Kind::Simulation, limits);
        elsewhere = callText(other, other.loadText(loops, "loops.lua"), "spin");
    });
    const QString here = callText(state, state.loadText(loops, "loops.lua"), "spin");
    worker.join();
    test.check(here.contains("limit of 50 ms") && elsewhere.contains("limit of 50 ms"),
               "two states on two threads stopped at once");
}

void checkMemoryLimit(TokenTest::Suite &test) {
    State::Limits limits;
    limits.memory = size_t(2) << 20;
    State state(State::Kind::Ui, limits);
    const int greedy = state.loadText("function grow() local t = {} for i = 1, 1e7 do t[i] = {i} end return #t end",
                                      "greedy.lua");
    const QString message = callText(state, greedy, "grow");
    const int modest = state.loadText("function add() return 1 + 1 end", "modest.lua");
    test.check(message.contains("not enough memory") && callText(state, modest, "add") == "2",
               QString("the memory limit stops a script, the state goes on (%1 KB in use)").arg(state.memoryUsed() / 1024));
}

void checkSaving(TokenTest::Suite &test) {
    State state(State::Kind::Simulation);
    lua_State *L = state.lua();
    const int script = state.loadText(R"(
function value()
  return {count = 3, ratio = 0.1, big = 1e300, tiny = 5e-324, negativeZero = -0.0, whole = 3.0,
          nan = 0/0, inf = 1/0, negativeInf = -1/0, min = math.mininteger, max = math.maxinteger,
          text = "a \"quote\"\n\0\1\127 zażółć", ["end"] = true, [""] = false,
          list = {10, 20, {30}}, sparse = {[1] = "a", [3] = "c", [-2] = "n"}, nested = {deep = {deeper = {}}}}
end
function same(a, b)
  if type(a) ~= type(b) then return false end
  if type(a) == "number" then
    if math.type(a) ~= math.type(b) then return false end
    if a ~= a then return b ~= b end
    if a == 0 and b == 0 then return 1 / a == 1 / b end
    return a == b
  end
  if type(a) ~= "table" then return a == b end
  for k, v in pairs(a) do if not same(v, b[k]) then return false end end
  for k in pairs(b) do if a[k] == nil then return false end end
  return true
end
function func() return {f = function() end} end
function cycle() local t = {} t.self = t return t end
function boolKey() return {inner = {[true] = 1}} end
)", "values.lua");

    state.prepare(script, "value");
    state.call(0, 1);
    const int original = state.keep(-1);
    QByteArray text;
    const bool saved = state.save(-1, text);
    lua_pop(L, 1);
    const bool restored = saved && state.restore(text);
    QByteArray again;
    const bool savedAgain = restored && state.save(-1, again);
    const int copy = restored ? state.keep(-1) : -1;
    if (restored)
        lua_pop(L, 1);
    bool same = false;
    if (restored && state.prepare(script, "same")) {
        state.push(original);
        state.push(copy);
        same = state.call(2, 1) && lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    test.check(same && savedAgain && again == text,
               "a value saved and restored exactly (integers, floats, -0, NaN, infinities, bytes), the same text again");
    test.check(text.contains("count=3") && text.contains("whole=3.0") && text.contains("[\"end\"]=true")
                   && text.contains("list={10,20,{30}}") && text.contains("min=(-9223372036854775807-1)"),
               "readable text: integers and floats told apart, keywords quoted, sequences without keys");
    if (test.verbose)
        qInfo().noquote() << "[tests:script] saved:" << text;

    auto refused = [&](const char *function, const char *expected) {
        state.prepare(script, function);
        state.call(0, 1);
        QByteArray out;
        const bool ok = state.save(-1, out);
        lua_pop(L, 1);
        return !ok && state.lastError().contains(expected);
    };
    test.check(refused("func", "value.f is a function") && refused("cycle", "value.self holds itself")
                   && refused("boolKey", "value.inner has a key that is a boolean"),
               "functions, cycles and other keys refused, with the path to them");
    test.check(!state.restore("return {f = function() end}") && state.lastError().contains("cannot be saved"),
               "restoring text that makes a function is refused");
    test.check(!state.restore("while true do end") && state.lastError().contains("ran longer"),
               "restoring text that runs on is stopped");
    const bool empty = state.restore("return os, print");
    test.check(empty && lua_isnil(L, -1), "restored text sees no libraries");
    if (empty)
        lua_pop(L, 1);
    state.release(original);
    if (copy >= 0)
        state.release(copy);
}

void checkRequire(TokenTest::Suite &test) {
    QTemporaryDir root;
    QDir(root.path()).mkpath("lib");
    auto write = [&](const QString &name, const QByteArray &text) {
        QFile file(root.filePath(name));
        if (file.open(QIODevice::WriteOnly))
            file.write(text);
    };
    write("lib/units.lua", "count = (count or 0) + 1\nlocal M = {loads = count}\n"
                           "function M.kmh(ms) return ms * 3.6 end\nreturn M\n");
    write("lib/loop.lua", "return require('lib.loop')\n");
    State state(State::Kind::Ui);
    state.setModuleRoots({root.path()});
    const int script = state.loadText(R"(
local units = require("lib.units")
function use() return units.kmh(10) .. " " .. tostring(require("lib.units") == units) .. " " .. units.loads end
function leak() return tostring(count) end
function failing(name) return select(2, pcall(require, name)) end
)", "uses.lua");
    test.check(callText(state, script, "use") == "36.0 true 1" && callText(state, script, "leak") == "nil",
               "a module loaded once from the module folders, its globals its own");
    test.check(callText(state, script, "failing", {"lib.nothing"}).contains("is not in the module folders")
                   && callText(state, script, "failing", {"../lib/units"}).contains("is not a dotted name")
                   && callText(state, script, "failing", {"lib.loop"}).contains("requires itself"),
               "missing modules, paths and loops refused");
}

void checkPrint(TokenTest::Suite &test) {
    State state(State::Kind::Ui);
    QStringList printed;
    state.setPrinter([&](const QString &text) { printed << text; });
    const int script = state.loadText("function hello() print('hello', 42, nil) end", "hello.lua");
    callText(state, script, "hello");
    test.check(printed == QStringList{"[script] hello.lua:1: hello\t42\tnil"}, "print: the script's file and line");
}

void checkThreads(TokenTest::Suite &test) {
    QString result;
    std::thread worker([&] {
        State state(State::Kind::Simulation);
        const int script = state.loadText("function answer() return 6 * 7 end", "worker.lua");
        result = callText(state, script, "answer");
    });
    worker.join();
    test.check(result == "42", "a state made and used on another thread");

    const char *random = "function draw() local t = {} for i = 1, 5 do t[i] = math.random(1000) end "
                         "return table.concat(t, ',') end";
    State a(State::Kind::Simulation, State::Limits(), 7), b(State::Kind::Simulation, State::Limits(), 7),
        c(State::Kind::Simulation, State::Limits(), 8);
    const QString first = callText(a, a.loadText(random, "a.lua"), "draw");
    const QString second = callText(b, b.loadText(random, "b.lua"), "draw");
    const QString other = callText(c, c.loadText(random, "c.lua"), "draw");
    test.check(first == second && first != other, "simulation: random numbers repeat for the run's seed");
}

void checkKept(TokenTest::Suite &test) {
    State state(State::Kind::Simulation);
    lua_State *L = state.lua();
    const int script = state.loadText(
        "function init(state) state.n = 0 end\nfunction update(state) state.n = state.n + 1 return state.n end",
        "loco.lua");
    lua_newtable(L);
    const int loco = state.keep(-1);
    lua_pop(L, 1);
    state.prepare(script, "init");
    state.push(loco);
    state.call(1, 0);
    lua_Integer n = 0;
    for (int i = 0; i < 2; ++i) {
        state.prepare(script, "update");
        state.push(loco);
        state.call(1, 1);
        n = lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    state.release(loco);
    test.check(n == 2 && lua_gettop(L) == 0, "a state table kept by TSRE between calls; the stack left clean");
}

}

int TsreTests::runScriptSuite(bool verbose) {
    TokenTest::Suite test{"[tests:script]", verbose};
    checkSandbox(test);
    checkGlobals(test);
    checkErrors(test);
    checkTimeLimit(test);
    checkMemoryLimit(test);
    checkSaving(test);
    checkRequire(test);
    checkPrint(test);
    checkThreads(test);
    checkKept(test);
    return test.finish();
}
