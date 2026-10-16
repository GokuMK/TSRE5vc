#pragma once
#include <QString>
#include <QStringList>
#include <vector>

namespace Sim {

// The legacy signal script language of `sigscr.dat` (task simulation 05),
// read and run as Open Rails runs it: case-insensitive; `SCRIPT name`
// sections (`REM SCRIPT` ones skipped); `extern float` and `float`
// declarations; assignments, `if`/`else`, blocks, calls, `return`;
// integers throughout (division truncates, by zero gives zero); `&&` and
// `||` (`AND`, `OR`) of equal precedence and arithmetic operators of
// equal precedence, each taken left to right as Open Rails takes them; a
// comparison with `#` (`==#`) as the plain one.

// The functions a script may call. Open Rails' additions are read and
// reported (the script is then not used): OpenRails.
enum class ScriptFunction : unsigned char {
    BlockState, RouteSet, NextSigLr, NextSigMr, ThisSigLr, ThisSigMr, OppSigLr, OppSigMr, NextNsigLr,
    DistMultiSigMr, SigFeature, DefDrawState, DebugHeader, DebugOut, OpenRails
};

// The head's externals: written by the script (state, draw_state), or
// read only (enabled, block_state).
enum class ScriptVariable : unsigned char { State, DrawState, Enabled, BlockState };

// Values of the language's constants.
enum ScriptBlock { BlockClear = 0, BlockOccupied = 1, BlockJunctionObstructed = 2 };

struct ScriptNode {
    enum Kind : unsigned char {
        Number, Local, Variable, Call, Not, Negate, Arithmetic, Compare, Logic,
        Assign, If, Block, Return, Nothing
    };
    enum Operator : unsigned char {
        Add, Subtract, Multiply, Divide, Modulo,
        Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
        And, Or
    };
    Kind kind = Nothing;
    unsigned char op = 0;
    // Number: its value; Local: its slot; Variable: ScriptVariable; Call:
    // ScriptFunction; Assign: the target's kind (Local or Variable).
    int value = 0;
    // Children: Not, Negate: a; Arithmetic, Compare, Logic: a, b; Assign:
    // a (the value), the target in `target`; If: a (condition), b (then),
    // c (else, or -1); Call, Block: `first`, `count` in the script's
    // `children`.
    int a = -1, b = -1, c = -1;
    int target = 0;
    int first = 0, count = 0;
    int line = 0;
};

struct SignalScript {
    // Lower case, as types are named.
    QString name;
    QString file;
    int line = 0;
    std::vector<ScriptNode> nodes;
    std::vector<int> children;
    // The statements run (a Block).
    int root = -1;
    int locals = 0;
    // "sigscr.dat line 47: ..." each.
    QStringList problems;
    // Open Rails' functions it calls (lower case).
    QStringList openRails;
    bool usable() const { return root >= 0 && problems.isEmpty() && openRails.isEmpty(); }
};

// A file's scripts; problems outside any script in `problems`.
std::vector<SignalScript> ReadSignalScripts(const QString &text, const QString &file, QStringList *problems = nullptr);
// The text of a script file: UTF-16 with its byte order mark, else 8-bit.
bool ReadSignalScriptText(const QString &path, QString &text);

// What a script asks of the simulation for the head it runs for (aspects
// as SignalType's, functions as SignalFunction's).
class SignalScriptHost {
public:
    virtual ~SignalScriptHost() = default;
    virtual int enabled() = 0;
    virtual int blockState() = 0;
    virtual int routeSet() = 0;
    // The n-th next signal with heads of `function` (n from 1): its least
    // (most) restrictive of them; STOP when there is none.
    virtual int nextSignal(int function, bool least, int nth) = 0;
    virtual int thisSignal(int function, bool least) = 0;
    virtual int oppositeSignal(int function, bool least) = 0;
    virtual int distantMulti(int function1, int function2) = 0;
    virtual int feature(int feature) = 0;
    virtual int drawStateOf(int aspect) = 0;
};

// Runs a usable script: `state` and `drawState` in and out (a head keeps
// them between runs), its locals cleared.
void RunSignalScript(const SignalScript &script, SignalScriptHost &host, int &state, int &drawState);

}
