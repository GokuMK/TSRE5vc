#include <tsre/sim/SignalScript.h>

#include <QFile>
#include <QHash>
#include <array>
#include <cmath>
#include <tuple>

namespace Sim {

namespace {

const QHash<QString, ScriptFunction> &functions() {
    static const QHash<QString, ScriptFunction> names = {
        {"block_state", ScriptFunction::BlockState}, {"route_set", ScriptFunction::RouteSet},
        {"next_sig_lr", ScriptFunction::NextSigLr}, {"next_sig_mr", ScriptFunction::NextSigMr},
        {"this_sig_lr", ScriptFunction::ThisSigLr}, {"this_sig_mr", ScriptFunction::ThisSigMr},
        {"opp_sig_lr", ScriptFunction::OppSigLr}, {"opp_sig_mr", ScriptFunction::OppSigMr},
        {"next_nsig_lr", ScriptFunction::NextNsigLr}, {"dist_multi_sig_mr", ScriptFunction::DistMultiSigMr},
        {"sig_feature", ScriptFunction::SigFeature}, {"def_draw_state", ScriptFunction::DefDrawState},
        {"debug_header", ScriptFunction::DebugHeader}, {"debug_out", ScriptFunction::DebugOut},
    };
    return names;
}

// Open Rails' additions: read, reported, not run (task 05, decision 3).
const QStringList &openRailsNames() {
    static const QStringList names = {
        "dist_multi_sig_mr_of_lr", "next_sig_id", "next_nsig_id", "opp_sig_id", "opp_sig_id_trainpath",
        "id_sig_enabled", "id_sig_lr", "allow_clear_to_partial_route", "approach_control_position",
        "approach_control_position_forced", "approach_control_speed", "approach_control_lock_claim",
        "approach_control_next_stop", "activate_timing_trigger", "check_timing_trigger", "trainhascallon",
        "trainhascallon_restricted", "trainhascallon_advanced", "trainhascallon_restricted_advanced",
        "train_requires_next_signal", "find_req_normal_signal", "route_cleared_to_signal",
        "route_cleared_to_signal_callon", "hashead", "increase_signalnumclearahead",
        "decrease_signalnumclearahead", "set_signalnumclearahead", "reset_signalnumclearahead", "store_lvar",
        "this_sig_lvar", "next_sig_lvar", "id_sig_lvar", "this_sig_noupdate", "this_sig_hasnormalsubtype",
        "next_sig_hasnormalsubtype", "id_sig_hasnormalsubtype", "switchstand", "train_has_request_stop",
        "approach_control_req_position", "approach_control_req_speed",
    };
    return names;
}

const QHash<QString, int> &constants() {
    static const QHash<QString, int> names = {
        {"sigasp_stop", 0}, {"sigasp_stop_and_proceed", 1}, {"sigasp_restricting", 2},
        {"sigasp_approach_1", 3}, {"sigasp_approach_2", 4}, {"sigasp_approach_3", 5},
        {"sigasp_clear_1", 6}, {"sigasp_clear_2", 7},
        // As SignalFunction: Normal, Distant, Repeater, Shunting, Info,
        // Speed, Other.
        {"sigfn_normal", 0}, {"sigfn_distance", 1}, {"sigfn_repeater", 2}, {"sigfn_shunting", 3},
        {"sigfn_info", 4}, {"sigfn_speed", 5}, {"sigfn_alert", 6},
        {"block_clear", BlockClear}, {"block_occupied", BlockOccupied},
        {"block_jn_obstructed", BlockJunctionObstructed},
        {"sigfeat_number_plate", 0}, {"sigfeat_gradient_plate", 1}, {"sigfeat_user1", 2},
        {"sigfeat_user2", 3}, {"sigfeat_user3", 4}, {"sigfeat_user4", 5},
    };
    return names;
}

const QHash<QString, ScriptVariable> &variables() {
    static const QHash<QString, ScriptVariable> names = {
        {"state", ScriptVariable::State}, {"draw_state", ScriptVariable::DrawState},
        {"enabled", ScriptVariable::Enabled},
    };
    return names;
}

// The arguments each function takes (-1 any).
int arity(ScriptFunction function) {
    switch (function) {
    case ScriptFunction::BlockState: case ScriptFunction::RouteSet: case ScriptFunction::DebugHeader:
        return 0;
    case ScriptFunction::NextNsigLr: case ScriptFunction::DistMultiSigMr:
        return 2;
    case ScriptFunction::DebugOut: case ScriptFunction::OpenRails:
        return -1;
    default:
        return 1;
    }
}

struct Token {
    enum Kind { Name, Number, Symbol, End } kind = End;
    QString text;
    double number = 0.0;
    int line = 0;
};

struct Line {
    int number;
    QString text;
};

// The text without its comments, line breaks kept.
QString withoutComments(const QString &text) {
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n')
                ++i;
            if (i < text.size())
                out += '\n';
        } else if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            i += 2;
            while (i < text.size() && !(text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/')) {
                if (text[i] == '\n')
                    out += '\n';
                ++i;
            }
            ++i;
        } else {
            out += c;
        }
    }
    return out;
}

std::vector<Token> tokens(const std::vector<Line> &lines, const QString &file, QStringList &problems) {
    // Longest first.
    static const char *symbols[] = {"/#=", "==#", "!=#", ">=#", "<=#", "++", "--", "+=", "-=", "*=", "/=", "%=",
                                    "/#", "==", "!=", ">=", "<=", ">#", "<#", "&&", "||", "!", "=", "<", ">", "+",
                                    "-", "*", "/", "%", "(", ")", "{", "}", ";", ",", "?", ":", "#"};
    std::vector<Token> out;
    for (const Line &line : lines) {
        const QString &t = line.text;
        qsizetype i = 0;
        while (i < t.size()) {
            const QChar c = t[i];
            if (c.isSpace()) {
                ++i;
                continue;
            }
            Token token;
            token.line = line.number;
            if (c.isLetter() || c == '_') {
                qsizetype end = i;
                while (end < t.size() && (t[end].isLetterOrNumber() || t[end] == '_'))
                    ++end;
                token.kind = Token::Name;
                token.text = t.mid(i, end - i).toLower();
                i = end;
            } else if (c.isDigit() || (c == '.' && i + 1 < t.size() && t[i + 1].isDigit())) {
                qsizetype end = i;
                while (end < t.size() && (t[end].isDigit() || t[end] == '.'))
                    ++end;
                token.kind = Token::Number;
                token.text = t.mid(i, end - i);
                token.number = token.text.toDouble();
                i = end;
            } else {
                bool found = false;
                for (const char *symbol : symbols) {
                    const QString s = QString::fromLatin1(symbol);
                    if (t.mid(i, s.size()) == s) {
                        token.kind = Token::Symbol;
                        token.text = s;
                        i += s.size();
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    problems << QString("%1 line %2: unexpected character %3").arg(file).arg(line.number).arg(c);
                    ++i;
                    continue;
                }
            }
            out.push_back(token);
        }
    }
    Token end;
    end.line = lines.empty() ? 0 : lines.back().number;
    out.push_back(end);
    return out;
}

class Parser {
public:
    Parser(const std::vector<Token> &tokens, SignalScript &script) : t(tokens), s(script) {}

    void body() {
        std::vector<int> statements;
        while (!at(Token::End)) {
            const size_t before = pos;
            const int statement = item();
            if (statement >= 0)
                statements.push_back(statement);
            if (pos == before)
                ++pos;
        }
        s.root = block(statements, 0);
    }

private:
    const std::vector<Token> &t;
    SignalScript &s;
    size_t pos = 0;
    QHash<QString, int> locals;
    // Loops around the statement being read.
    int loops = 0;

    const Token &peek(size_t ahead = 0) const { return t[std::min(pos + ahead, t.size() - 1)]; }
    bool at(Token::Kind kind) const { return peek().kind == kind; }
    bool atSymbol(const char *symbol, size_t ahead = 0) const {
        return peek(ahead).kind == Token::Symbol && peek(ahead).text == QLatin1String(symbol);
    }
    bool atName(const char *name, size_t ahead = 0) const {
        return peek(ahead).kind == Token::Name && peek(ahead).text == QLatin1String(name);
    }
    void problem(const QString &what, int line = -1) {
        s.problems << QString("%1 line %2: %3").arg(s.file).arg(line >= 0 ? line : peek().line).arg(what);
    }
    bool expect(const char *symbol) {
        if (atSymbol(symbol)) {
            ++pos;
            return true;
        }
        problem(QString("%1 expected").arg(QLatin1String(symbol)));
        return false;
    }
    // After a problem: on past the statement's end.
    void recover() {
        while (!at(Token::End) && !atSymbol(";") && !atSymbol("}"))
            ++pos;
        if (atSymbol(";"))
            ++pos;
    }
    int node(ScriptNode n) {
        s.nodes.push_back(n);
        return int(s.nodes.size()) - 1;
    }
    int block(const std::vector<int> &statements, int line) {
        ScriptNode n;
        n.kind = ScriptNode::Block;
        n.first = int(s.children.size());
        n.count = int(statements.size());
        n.line = line;
        s.children.insert(s.children.end(), statements.begin(), statements.end());
        return node(n);
    }
    int number(double value, int line) {
        ScriptNode n;
        n.kind = ScriptNode::Number;
        n.number = value;
        n.line = line;
        return node(n);
    }

    // A declaration (nothing to run: -1) or a statement.
    int item() {
        if (atName("extern") || atName("float")) {
            const bool external = atName("extern");
            ++pos;
            if (external && atName("float"))
                ++pos;
            do {
                if (!at(Token::Name)) {
                    problem("a name expected");
                    recover();
                    return -1;
                }
                const QString name = peek().text;
                ++pos;
                if (atSymbol("(")) {
                    ++pos;
                    expect(")");
                }
                if (!external && !locals.contains(name)) {
                    locals.insert(name, s.locals++);
                } else if (external && !functions().contains(name) && !variables().contains(name)
                           && name != "block_state") {
                    if (openRailsNames().contains(name)) {
                        if (!s.openRails.contains(name))
                            s.openRails << name;
                    } else {
                        problem(QString("unknown external %1").arg(name));
                    }
                }
            } while (atSymbol(",") && ++pos);
            if (!expect(";"))
                recover();
            return -1;
        }
        return statement();
    }

    // An expression in parentheses (if, while).
    int condition() {
        if (!expect("("))
            return number(0.0, peek().line);
        const int inside = expression();
        expect(")");
        return inside;
    }

    int statement() {
        const int line = peek().line;
        if (atSymbol(";")) {
            ++pos;
            return -1;
        }
        if (atSymbol("{")) {
            ++pos;
            std::vector<int> statements;
            while (!at(Token::End) && !atSymbol("}")) {
                const size_t before = pos;
                const int inner = item();
                if (inner >= 0)
                    statements.push_back(inner);
                if (pos == before)
                    ++pos;
            }
            expect("}");
            return block(statements, line);
        }
        if (atName("if")) {
            ++pos;
            ScriptNode n;
            n.kind = ScriptNode::If;
            n.line = line;
            n.a = condition();
            n.b = statement();
            if (atName("else")) {
                ++pos;
                n.c = statement();
            }
            return node(n);
        }
        if (atName("while") || atName("do") || atName("for")) {
            ScriptNode n;
            n.kind = ScriptNode::Loop;
            n.line = line;
            const QString word = peek().text;
            ++pos;
            ++loops;
            if (word == "while") {
                n.a = condition();
                n.b = statement();
            } else if (word == "do") {
                n.after = true;
                n.b = statement();
                if (atName("while")) {
                    ++pos;
                    n.a = condition();
                } else {
                    problem("while expected");
                }
                expect(";");
            } else {
                expect("(");
                n.d = atSymbol(";") ? -1 : expression();
                expect(";");
                n.a = atSymbol(";") ? -1 : expression();
                expect(";");
                n.c = atSymbol(")") ? -1 : expression();
                expect(")");
                n.b = statement();
            }
            --loops;
            return node(n);
        }
        if (atName("break") || atName("continue")) {
            ScriptNode n;
            n.kind = atName("break") ? ScriptNode::Break : ScriptNode::Continue;
            n.line = line;
            if (loops == 0)
                problem(QString("%1 outside a loop").arg(peek().text));
            ++pos;
            expect(";");
            return node(n);
        }
        if (atName("return")) {
            ++pos;
            ScriptNode n;
            n.kind = ScriptNode::Return;
            n.line = line;
            if (!atSymbol(";"))
                n.a = expression();
            expect(";");
            return node(n);
        }
        if (atName("switch") || atName("case") || atName("default") || atName("goto")) {
            problem(QString("%1 is not in the language").arg(peek().text));
            ++pos;
            recover();
            return -1;
        }
        const int value = expression();
        if (!expect(";"))
            recover();
        return value;
    }

    int expression() { return assignment(); }

    // The target of an assignment, ++ or --: a local, state or draw_state.
    bool target(int index, ScriptNode &n) {
        const ScriptNode &target = s.nodes[size_t(index)];
        if (target.kind == ScriptNode::Local
                || (target.kind == ScriptNode::Variable && (ScriptVariable(target.value) == ScriptVariable::State
                                                            || ScriptVariable(target.value) == ScriptVariable::DrawState))) {
            n.value = target.kind;
            n.target = target.value;
            return true;
        }
        const QString name = target.kind != ScriptNode::Variable ? QString("the expression")
                : ScriptVariable(target.value) == ScriptVariable::Enabled ? QString("enabled") : QString("block_state");
        problem(QString("%1 cannot be set").arg(name), target.line);
        return false;
    }

    // From the right: a = b = 1.
    int assignment() {
        const int left = conditional();
        static const std::array<std::pair<const char *, ScriptNode::Operator>, 7> operators = {{
            {"=", ScriptNode::Set}, {"+=", ScriptNode::Add}, {"-=", ScriptNode::Subtract},
            {"*=", ScriptNode::Multiply}, {"/=", ScriptNode::Divide}, {"/#=", ScriptNode::IntegerDivide},
            {"%=", ScriptNode::Modulo},
        }};
        for (const auto &[symbol, op] : operators)
            if (atSymbol(symbol)) {
                ScriptNode n;
                n.kind = ScriptNode::Assign;
                n.op = op;
                n.line = peek().line;
                ++pos;
                const bool settable = target(left, n);
                n.a = assignment();
                return settable ? node(n) : n.a;
            }
        return left;
    }

    int conditional() {
        const int condition = logicalOr();
        if (!atSymbol("?"))
            return condition;
        ScriptNode n;
        n.kind = ScriptNode::Conditional;
        n.line = peek().line;
        ++pos;
        n.a = condition;
        n.b = expression();
        expect(":");
        n.c = conditional();
        return node(n);
    }

    int logical(int (Parser::*next)(), ScriptNode::Operator op, const char *symbol, const char *word) {
        int left = (this->*next)();
        while (atSymbol(symbol) || atName(word)) {
            ScriptNode n;
            n.kind = ScriptNode::Logic;
            n.op = op;
            n.line = peek().line;
            ++pos;
            n.a = left;
            n.b = (this->*next)();
            left = node(n);
        }
        return left;
    }
    int logicalOr() { return logical(&Parser::logicalAnd, ScriptNode::Or, "||", "or"); }
    int logicalAnd() { return logical(&Parser::equality, ScriptNode::And, "&&", "and"); }

    template <size_t N>
    int binary(int (Parser::*next)(), ScriptNode::Kind kind,
               const std::array<std::tuple<const char *, ScriptNode::Operator, bool>, N> &operators) {
        int left = (this->*next)();
        for (bool more = true; more;) {
            more = false;
            for (const auto &[symbol, op, integer] : operators)
                if (atSymbol(symbol)) {
                    ScriptNode n;
                    n.kind = kind;
                    n.op = op;
                    n.integer = integer;
                    n.line = peek().line;
                    ++pos;
                    n.a = left;
                    n.b = (this->*next)();
                    left = node(n);
                    more = true;
                    break;
                }
        }
        return left;
    }
    int equality() {
        static const std::array<std::tuple<const char *, ScriptNode::Operator, bool>, 4> operators = {{
            {"==#", ScriptNode::Equal, true}, {"!=#", ScriptNode::NotEqual, true},
            {"==", ScriptNode::Equal, false}, {"!=", ScriptNode::NotEqual, false},
        }};
        return binary(&Parser::relational, ScriptNode::Compare, operators);
    }
    int relational() {
        static const std::array<std::tuple<const char *, ScriptNode::Operator, bool>, 8> operators = {{
            {">=#", ScriptNode::GreaterEqual, true}, {"<=#", ScriptNode::LessEqual, true},
            {">#", ScriptNode::Greater, true}, {"<#", ScriptNode::Less, true},
            {">=", ScriptNode::GreaterEqual, false}, {"<=", ScriptNode::LessEqual, false},
            {">", ScriptNode::Greater, false}, {"<", ScriptNode::Less, false},
        }};
        return binary(&Parser::hash, ScriptNode::Compare, operators);
    }
    // #x: x's integer part.
    int hash() {
        if (!atSymbol("#"))
            return additive();
        ScriptNode n;
        n.kind = ScriptNode::Integer;
        n.line = peek().line;
        ++pos;
        n.a = hash();
        return node(n);
    }
    int additive() {
        static const std::array<std::tuple<const char *, ScriptNode::Operator, bool>, 2> operators = {{
            {"+", ScriptNode::Add, false}, {"-", ScriptNode::Subtract, false},
        }};
        return binary(&Parser::multiplicative, ScriptNode::Arithmetic, operators);
    }
    int multiplicative() {
        static const std::array<std::tuple<const char *, ScriptNode::Operator, bool>, 4> operators = {{
            {"*", ScriptNode::Multiply, false}, {"/#", ScriptNode::IntegerDivide, false},
            {"/", ScriptNode::Divide, false}, {"%", ScriptNode::Modulo, false},
        }};
        return binary(&Parser::unary, ScriptNode::Arithmetic, operators);
    }

    int step(int target, bool increment, bool after, int line) {
        ScriptNode n;
        n.kind = ScriptNode::Step;
        n.op = increment ? ScriptNode::Add : ScriptNode::Subtract;
        n.after = after;
        n.line = line;
        return this->target(target, n) ? node(n) : target;
    }

    int unary() {
        const int line = peek().line;
        if (atSymbol("-") || atSymbol("!") || atName("not")) {
            ScriptNode n;
            n.kind = atSymbol("-") ? ScriptNode::Negate : ScriptNode::Not;
            n.line = line;
            ++pos;
            n.a = unary();
            return node(n);
        }
        if (atSymbol("+")) {
            ++pos;
            return unary();
        }
        if (atSymbol("++") || atSymbol("--")) {
            const bool increment = atSymbol("++");
            ++pos;
            return step(unary(), increment, false, line);
        }
        int value = primary();
        while (atSymbol("++") || atSymbol("--")) {
            const bool increment = atSymbol("++");
            ++pos;
            value = step(value, increment, true, line);
        }
        return value;
    }

    int primary() {
        const Token token = peek();
        if (atSymbol("(")) {
            ++pos;
            const int inner = expression();
            expect(")");
            return inner;
        }
        if (token.kind == Token::Number) {
            ++pos;
            return number(token.number, token.line);
        }
        if (token.kind != Token::Name) {
            problem(QString("unexpected %1").arg(token.kind == Token::End ? QString("end") : token.text));
            if (!at(Token::End) && !atSymbol(";"))
                ++pos;
            return number(0.0, token.line);
        }
        ++pos;
        const QString &name = token.text;
        if (atSymbol("(")) {
            ++pos;
            std::vector<int> arguments;
            while (!at(Token::End) && !atSymbol(")")) {
                arguments.push_back(assignment());
                if (!atSymbol(","))
                    break;
                ++pos;
            }
            expect(")");
            ScriptNode n;
            n.kind = ScriptNode::Call;
            n.line = token.line;
            if (functions().contains(name)) {
                n.value = int(functions().value(name));
            } else if (openRailsNames().contains(name)) {
                n.value = int(ScriptFunction::OpenRails);
                if (!s.openRails.contains(name))
                    s.openRails << name;
            } else {
                problem(QString("unknown function %1").arg(name), token.line);
                return number(0.0, token.line);
            }
            const int wanted = arity(ScriptFunction(n.value));
            if (wanted >= 0 && int(arguments.size()) != wanted)
                problem(QString("%1 takes %2 arguments, given %3").arg(name).arg(wanted).arg(arguments.size()),
                        token.line);
            n.first = int(s.children.size());
            n.count = int(arguments.size());
            s.children.insert(s.children.end(), arguments.begin(), arguments.end());
            return node(n);
        }
        if (locals.contains(name)) {
            ScriptNode n;
            n.kind = ScriptNode::Local;
            n.value = locals.value(name);
            n.line = token.line;
            return node(n);
        }
        if (variables().contains(name) || name == "block_state") {
            ScriptNode n;
            n.kind = ScriptNode::Variable;
            n.value = int(name == "block_state" ? ScriptVariable::BlockState : variables().value(name));
            n.line = token.line;
            return node(n);
        }
        if (constants().contains(name))
            return number(constants().value(name), token.line);
        if (openRailsNames().contains(name)) {
            if (!s.openRails.contains(name))
                s.openRails << name;
            return number(0.0, token.line);
        }
        problem(QString("unknown name %1").arg(name), token.line);
        return number(0.0, token.line);
    }
};

class Runner {
public:
    Runner(const SignalScript &script, SignalScriptHost &host, int &state, int &drawState)
        : s(script), h(host), state(state), draw(drawState) {
        if (s.locals <= int(small.size())) {
            small.fill(0.0);
            locals = small.data();
        } else {
            large.assign(size_t(s.locals), 0.0);
            locals = large.data();
        }
    }
    void run() { execute(s.root); }

private:
    enum class Flow { Next, Break, Continue, Return };
    const SignalScript &s;
    SignalScriptHost &h;
    int &state;
    int &draw;
    std::array<double, 32> small{};
    std::vector<double> large;
    double *locals = nullptr;
    Flow flow = Flow::Next;
    int steps = 0;

    static bool truth(double v) { return v != 0.0; }

    void execute(int index) {
        if (index < 0 || flow != Flow::Next)
            return;
        const ScriptNode &n = s.nodes[size_t(index)];
        switch (n.kind) {
        case ScriptNode::Block:
            for (int i = 0; i < n.count && flow == Flow::Next; ++i)
                execute(s.children[size_t(n.first + i)]);
            break;
        case ScriptNode::If:
            if (truth(value(n.a)))
                execute(n.b);
            else
                execute(n.c);
            break;
        case ScriptNode::Loop:
            if (n.d >= 0)
                value(n.d);
            for (;;) {
                if (!n.after && n.a >= 0 && !truth(value(n.a)))
                    break;
                execute(n.b);
                if (flow == Flow::Return)
                    return;
                const bool broken = flow == Flow::Break;
                flow = Flow::Next;
                if (broken)
                    break;
                if (n.c >= 0)
                    value(n.c);
                if (n.after && n.a >= 0 && !truth(value(n.a)))
                    break;
                if (++steps > MaxScriptSteps) {
                    flow = Flow::Return;
                    return;
                }
            }
            break;
        case ScriptNode::Return:
            if (n.a >= 0)
                value(n.a);
            flow = Flow::Return;
            break;
        case ScriptNode::Break:
            flow = Flow::Break;
            break;
        case ScriptNode::Continue:
            flow = Flow::Continue;
            break;
        default:
            value(index);
            break;
        }
    }

    double argument(const ScriptNode &n, int i) {
        return i < n.count ? value(s.children[size_t(n.first + i)]) : 0.0;
    }
    int whole(const ScriptNode &n, int i) { return int(std::trunc(argument(n, i))); }

    double read(const ScriptNode &n) {
        if (n.value == ScriptNode::Local)
            return locals[n.target];
        return ScriptVariable(n.target) == ScriptVariable::State ? state : draw;
    }
    double write(const ScriptNode &n, double v) {
        if (n.value == ScriptNode::Local)
            return locals[n.target] = v;
        int &variable = ScriptVariable(n.target) == ScriptVariable::State ? state : draw;
        variable = int(std::trunc(v));
        return variable;
    }

    static double arithmetic(unsigned char op, double a, double b) {
        switch (op) {
        case ScriptNode::Add: return a + b;
        case ScriptNode::Subtract: return a - b;
        case ScriptNode::Multiply: return a * b;
        case ScriptNode::Divide: return b == 0.0 ? 0.0 : a / b;
        case ScriptNode::IntegerDivide: {
            const double whole = std::trunc(b);
            return whole == 0.0 ? 0.0 : std::trunc(std::trunc(a) / whole);
        }
        case ScriptNode::Modulo: return b == 0.0 ? 0.0 : std::fmod(a, b);
        default: return b;
        }
    }

    double value(int index) {
        if (index < 0)
            return 0.0;
        const ScriptNode &n = s.nodes[size_t(index)];
        switch (n.kind) {
        case ScriptNode::Number:
            return n.number;
        case ScriptNode::Local:
            return locals[n.value];
        case ScriptNode::Variable:
            switch (ScriptVariable(n.value)) {
            case ScriptVariable::State: return state;
            case ScriptVariable::DrawState: return draw;
            case ScriptVariable::Enabled: return h.enabled();
            case ScriptVariable::BlockState: return h.blockState();
            }
            return 0.0;
        case ScriptNode::Not:
            return truth(value(n.a)) ? 0.0 : 1.0;
        case ScriptNode::Negate:
            return -value(n.a);
        case ScriptNode::Integer:
            return std::trunc(value(n.a));
        case ScriptNode::Arithmetic: {
            const double a = value(n.a);
            return arithmetic(n.op, a, value(n.b));
        }
        case ScriptNode::Compare: {
            double a = value(n.a), b = value(n.b);
            if (n.integer) {
                a = std::trunc(a);
                b = std::trunc(b);
            }
            switch (n.op) {
            case ScriptNode::Equal: return a == b;
            case ScriptNode::NotEqual: return a != b;
            case ScriptNode::Less: return a < b;
            case ScriptNode::LessEqual: return a <= b;
            case ScriptNode::Greater: return a > b;
            default: return a >= b;
            }
        }
        case ScriptNode::Logic:
            // As C: the right side only when it decides.
            if (n.op == ScriptNode::And)
                return truth(value(n.a)) && truth(value(n.b));
            return truth(value(n.a)) || truth(value(n.b));
        case ScriptNode::Conditional:
            return truth(value(n.a)) ? value(n.b) : value(n.c);
        case ScriptNode::Assign: {
            const double v = value(n.a);
            return write(n, n.op == ScriptNode::Set ? v : arithmetic(n.op, read(n), v));
        }
        case ScriptNode::Step: {
            const double before = read(n);
            const double now = write(n, before + (n.op == ScriptNode::Add ? 1.0 : -1.0));
            return n.after ? before : now;
        }
        case ScriptNode::Call:
            return call(n);
        default:
            return 0.0;
        }
    }

    double call(const ScriptNode &n) {
        switch (ScriptFunction(n.value)) {
        case ScriptFunction::BlockState: return h.blockState();
        case ScriptFunction::RouteSet: return h.routeSet();
        case ScriptFunction::NextSigLr: return h.nextSignal(whole(n, 0), true, 1);
        case ScriptFunction::NextSigMr: return h.nextSignal(whole(n, 0), false, 1);
        case ScriptFunction::ThisSigLr: return h.thisSignal(whole(n, 0), true);
        case ScriptFunction::ThisSigMr: return h.thisSignal(whole(n, 0), false);
        case ScriptFunction::OppSigLr: return h.oppositeSignal(whole(n, 0), true);
        case ScriptFunction::OppSigMr: return h.oppositeSignal(whole(n, 0), false);
        case ScriptFunction::NextNsigLr: return h.nextSignal(whole(n, 0), true, whole(n, 1));
        case ScriptFunction::DistMultiSigMr: return h.distantMulti(whole(n, 0), whole(n, 1));
        case ScriptFunction::SigFeature: return h.feature(whole(n, 0));
        case ScriptFunction::DefDrawState: return h.drawStateOf(whole(n, 0));
        case ScriptFunction::DebugOut:
            for (int i = 0; i < n.count; ++i)
                argument(n, i);
            return 0.0;
        default:
            return 0.0;
        }
    }
};

}

std::vector<SignalScript> ReadSignalScripts(const QString &text, const QString &file, QStringList *problems) {
    std::vector<SignalScript> scripts;
    // Sections from each line beginning SCRIPT; REM SCRIPT ones skipped.
    struct Section {
        SignalScript script;
        std::vector<Line> lines;
        bool skipped = false;
    };
    std::vector<Section> sections;
    const QStringList lines = withoutComments(text).split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i];
        line.remove('\r');
        const QString trimmed = line.trimmed();
        const QString lower = trimmed.toLower();
        const bool script = lower.startsWith("script") && (lower.size() == 6 || lower[6].isSpace());
        const bool rem = lower.startsWith("rem") && lower.mid(3).trimmed().startsWith("script");
        if (script || rem) {
            Section section;
            section.skipped = rem;
            section.script.name = (rem ? trimmed.mid(3).trimmed() : trimmed).mid(6).trimmed().toLower();
            section.script.file = file;
            section.script.line = i + 1;
            sections.push_back(section);
            continue;
        }
        if (!sections.empty())
            sections.back().lines.push_back({i + 1, line});
        else if (!trimmed.isEmpty() && problems != nullptr)
            *problems << QString("%1 line %2: text before the first SCRIPT").arg(file).arg(i + 1);
    }
    for (Section &section : sections) {
        if (section.skipped)
            continue;
        SignalScript &script = section.script;
        if (script.name.isEmpty())
            script.problems << QString("%1 line %2: a SCRIPT without a name").arg(file).arg(script.line);
        const std::vector<Token> list = tokens(section.lines, file, script.problems);
        Parser parser(list, script);
        parser.body();
        scripts.push_back(std::move(script));
    }
    return scripts;
}

bool ReadSignalScriptText(const QString &path, QString &text) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray bytes = file.readAll();
    if (bytes.size() >= 2 && uchar(bytes[0]) == 0xff && uchar(bytes[1]) == 0xfe)
        text = QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData() + 2), (bytes.size() - 2) / 2);
    else if (bytes.size() >= 2 && uchar(bytes[0]) == 0xfe && uchar(bytes[1]) == 0xff) {
        QByteArray swapped = bytes.mid(2);
        for (qsizetype i = 0; i + 1 < swapped.size(); i += 2)
            std::swap(swapped[i], swapped[i + 1]);
        text = QString::fromUtf16(reinterpret_cast<const char16_t *>(swapped.constData()), swapped.size() / 2);
    } else {
        text = QString::fromLatin1(bytes);
    }
    return true;
}

void RunSignalScript(const SignalScript &script, SignalScriptHost &host, int &state, int &drawState) {
    if (!script.usable())
        return;
    Runner(script, host, state, drawState).run();
}

}
