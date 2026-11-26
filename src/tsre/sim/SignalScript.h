#pragma once
#include <QString>
#include <QStringList>
#include <vector>

namespace Sim {

// The legacy signal script language of `sigscr.dat` (task simulation 05),
// read and run as MSTS's own parser defines it (its Bison grammar, token and
// rule names in train.exe): C's expressions and statements on floating point
// values. Case-insensitive; `SCRIPT name` sections (`REM SCRIPT` ones
// skipped); `extern float` and `float` declarations; expressions with C's
// precedence (`||`, `&&`, equality, relational, `#`, additive,
// multiplicative, unary, postfix), `?:`, assignments (`=`, `+=`, `-=`, `*=`,
// `/=`, `%=`), `++`, `--`; `if`/`else`, `for`, `while`, `do`, `break`,
// `continue`, `return`, blocks. `#` takes a value's integer part (`#x`), and
// comparisons and division written with it (`==#`, `<#`, `/#`) compare and
// divide integer parts. Open Rails' spellings `AND`, `OR`, `NOT` read too.
// Not recovered from MSTS, chosen: `&&` and `||` evaluate their right side
// only when needed (as C), division or remainder by zero gives zero, a value
// written to `state` or `draw_state` keeps its integer part.

// The functions a script may call. Open Rails' additions are read and
// reported (the script is then not used): OpenRails; run (task 10): Open
// Rails' approach control (APPROACH_CONTROL_POSITION, _FORCED,
// APPROACH_CONTROL_SPEED); run (task 11): the signal idents
// (NEXT_SIG_ID, NEXT_NSIG_ID, OPP_SIG_ID, OPP_SIG_ID_TRAINPATH,
// ID_SIG_ENABLED, ID_SIG_LR), DIST_MULTI_SIG_MR_OF_LR, the local variables
// (STORE_LVAR, THIS/NEXT/ID_SIG_LVAR), the normal subtypes
// (THIS/NEXT/ID_SIG_HASNORMALSUBTYPE), THIS_SIG_NOUPDATE, SWITCHSTAND, the
// timing trigger (ACTIVATE_, CHECK_TIMING_TRIGGER), the four
// SIGNALNUMCLEARAHEAD ones, TRAIN_HAS_REQUEST_STOP and the route checks
// (TRAIN_REQUIRES_NEXT_SIGNAL, FIND_REQ_NORMAL_SIGNAL,
// ROUTE_CLEARED_TO_SIGNAL, _CALLON). TSRE's own (task 10): SHUNTING_PATH()
// (Open Rails, not knowing it, logs it and takes it as 0).
enum class ScriptFunction : unsigned char {
    BlockState, RouteSet, NextSigLr, NextSigMr, ThisSigLr, ThisSigMr, OppSigLr, OppSigMr, NextNsigLr,
    DistMultiSigMr, SigFeature, DefDrawState, DebugHeader, DebugOut, ShuntingPath, ApproachPosition,
    ApproachPositionForced, ApproachSpeed, NextSigId, NextNsigId, OppSigId, OppSigIdTrainpath, IdSigEnabled,
    IdSigLr, DistMultiSigMrOfLr, StoreLvar, ThisSigLvar, NextSigLvar, IdSigLvar, ThisSigHasNormalSubtype,
    NextSigHasNormalSubtype, IdSigHasNormalSubtype, ThisSigNoUpdate, Switchstand, ActivateTimingTrigger,
    CheckTimingTrigger, IncreaseClearAhead, DecreaseClearAhead, SetClearAhead, ResetClearAhead,
    TrainHasRequestStop, TrainRequiresNextSignal, FindReqNormalSignal, RouteClearedToSignal,
    RouteClearedToSignalCallOn, OpenRails
};

// The head's externals: written by the script (state, draw_state), or
// read only (enabled, block_state; task 11, Open Rails' approach control
// settings from sigcfg.dat: approach_control_req_position,
// approach_control_req_speed).
enum class ScriptVariable : unsigned char {
    State, DrawState, Enabled, BlockState, ApproachReqPosition, ApproachReqSpeed
};

// Values of the language's constants.
enum ScriptBlock { BlockClear = 0, BlockOccupied = 1, BlockJunctionObstructed = 2 };

struct ScriptNode {
    enum Kind : unsigned char {
        Number, Local, Variable, Call, Not, Negate, Integer, Arithmetic, Compare, Logic, Conditional,
        Assign, Step, If, Loop, Block, Return, Break, Continue, Nothing
    };
    enum Operator : unsigned char {
        Add, Subtract, Multiply, Divide, IntegerDivide, Modulo,
        Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
        And, Or, Set
    };
    Kind kind = Nothing;
    unsigned char op = 0;
    // Compare: on the values' integer parts (`==#`).
    bool integer = false;
    // Step: the value before it (x++); Loop: the condition after the body
    // (do).
    bool after = false;
    // Local: its slot; Variable: ScriptVariable; Call: ScriptFunction;
    // Assign, Step: the target's kind (Local or Variable).
    int value = 0;
    // Number: its value.
    double number = 0.0;
    // Children: Not, Negate, Integer, Return: a; Arithmetic, Compare, Logic:
    // a, b; Conditional: a ? b : c; Assign: a (the value), the target in
    // `target`; Step: the target in `target`; If: a (condition), b (then), c
    // (else, or -1); Loop: d (start), a (condition, -1 none), b (body), c
    // (step); Call, Block: `first`, `count` in the script's `children`.
    int a = -1, b = -1, c = -1, d = -1;
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

// A file's scripts; problems outside any script in `problems`. `subtypes`:
// sigcfg.dat's ORTSNormalSubtypes (upper case), named in scripts as Open
// Rails names them (ORSUBTYPE_<name>: its index in the list).
std::vector<SignalScript> ReadSignalScripts(const QString &text, const QString &file, QStringList *problems = nullptr,
                                            const QStringList &subtypes = {});
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
    // DIST_MULTI_SIG_MR (least: Open Rails' DIST_MULTI_SIG_MR_OF_LR, each
    // signal's least restrictive `function1` head counted).
    virtual int distantMulti(int function1, int function2, bool least) = 0;
    virtual int feature(int feature) = 0;
    virtual int drawStateOf(int aspect) = 0;
    // Task 10: 1 while the signal is cleared for a train on a shunting move.
    virtual int shuntingPath() { return 0; }
    // Task 10, Open Rails' approach control (SignalObject.
    // ApproachControlPosition, ApproachControlSpeed): 1 when the signal is
    // cleared for a train that is within `metres` of it (Open Rails' whole
    // metres) and, unless `forced`, has it as its next signal; with a speed
    // (m/s, -1 none) also slower than that (0: standing); once 1, 1 until
    // the signal is cleared again.
    virtual int approachControl(int metres, int speed, bool forced) {
        (void)metres;
        (void)speed;
        (void)forced;
        return 0;
    }
    // Task 11, Open Rails' additions. Signals by ident: their index among
    // the simulation's signals (Open Rails' are its own), -1 none.
    virtual int thisSignalId() { return -1; }
    // The n-th next signal with heads of `function` (n from 1), as
    // next_sig_lr finds the first; not stopping at one at STOP (Open Rails'
    // NEXT_SIG_ID, NEXT_NSIG_ID).
    virtual int nextSignalId(int function, int nth) { (void)function; (void)nth; return -1; }
    // The first signal with heads of `function` behind this one facing the
    // other way: over the track held for the train it is cleared for (Open
    // Rails' OPP_SIG_ID, and opp_sig_lr/mr's), or back along that train's
    // path (OPP_SIG_ID_TRAINPATH); none while it is not cleared.
    virtual int oppositeSignalId(int function, bool trainPath) { (void)function; (void)trainPath; return -1; }
    // ID_SIG_ENABLED: cleared for a train; ID_SIG_LR: its heads of
    // `function`, the least restrictive (none: CLEAR_2 for NORMAL, else
    // STOP; STOP for no signal).
    virtual int signalEnabled(int id) { (void)id; return 0; }
    virtual int signalAspect(int id, int function) { (void)id; (void)function; return 0; }
    // STORE_LVAR, *_SIG_LVAR: a signal's whole-number variables, kept from
    // run to run, its heads writing them, every head reading (0 unset).
    virtual void storeLocal(int index, int value) { (void)index; (void)value; }
    virtual int local(int id, int index) { (void)id; (void)index; return 0; }
    // *_SIG_HASNORMALSUBTYPE: 1 when the signal has a NORMAL head of that
    // ORTSNormalSubtype (its index in ORTSNormalSubtypes).
    virtual int hasNormalSubtype(int id, int subtype) { (void)id; (void)subtype; return 0; }
    // THIS_SIG_NOUPDATE: its signal not run again for what it reads (only
    // when it is cleared or given back, and for its switchstand's switch).
    virtual void noUpdate() {}
    // SWITCHSTAND: the first aspect while the first facing switch beyond
    // the signal is set to its first route (Open Rails' route 0), else the
    // second; the first for none.
    virtual int switchstand(int route0, int route1) { (void)route1; return route0; }
    // ACTIVATE_TIMING_TRIGGER: the signal's trigger at the time now;
    // CHECK_TIMING_TRIGGER: 1 once it was set more than `seconds` ago
    // (Open Rails' whole seconds).
    virtual void activateTimingTrigger() {}
    virtual int checkTimingTrigger(int seconds) { (void)seconds; return 0; }
    // The SIGNALNUMCLEARAHEAD ones: how many signals a train clears ahead
    // from this one, from its type's value (Open Rails mode only).
    enum class ClearAhead { Increase, Decrease, Set, Reset };
    virtual void clearAhead(ClearAhead change, int value) { (void)change; (void)value; }
    // TRAIN_HAS_REQUEST_STOP: 1 when the train coming has its next stop at
    // the platform beside the signal, a request stop it picks up at.
    virtual int requestStop() { return 0; }
    // TRAIN_REQUIRES_NEXT_SIGNAL: 1 when the path of the train this signal
    // is cleared for goes up to signal `id` (position 0) or on past it (1).
    virtual int requiresNextSignal(int id, int position) { (void)id; (void)position; return 0; }
    // FIND_REQ_NORMAL_SIGNAL: the first signal on the train's path beyond
    // this one with a NORMAL head of that subtype.
    virtual int findRequiredNormal(int subtype) { (void)subtype; return -1; }
    // ROUTE_CLEARED_TO_SIGNAL(_CALLON): BLOCK_CLEAR when the track from this
    // signal up to signal `id` is held for the train, else (callOn) when
    // the train may call on BLOCK_OCCUPIED, else BLOCK_JN_OBSTRUCTED.
    virtual int routeClearedTo(int id, bool callOn) { (void)id; (void)callOn; return 2; }
    // approach_control_req_position, _speed: the type's ApproachControl-
    // Settings in whole metres and metres per second; -1 none.
    virtual int approachRequired(bool speed) { (void)speed; return -1; }
};

// Runs a usable script: `state` and `drawState` in and out (a head keeps
// them between runs), its locals cleared; a run stops after MaxScriptSteps
// loop rounds.
constexpr int MaxScriptSteps = 100000;
void RunSignalScript(const SignalScript &script, SignalScriptHost &host, int &state, int &drawState);

}
