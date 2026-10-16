#include <tsre/sim/SignalScript.h>

#include <QFile>
#include <QHash>
#include <array>
#include <cmath>

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
    int value = 0;
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
    static const char *symbols[] = {"==#", "!=#", ">=#", "<=#", "==", "!=", ">=", "<=", ">#", "<#", "&&", "||",
                                    "!", "=", "<", ">", "+", "-", "*", "/", "%", "(", ")", "{", "}", ";", ","};
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
                // Open Rails' integers: truncated.
                token.value = int(std::trunc(token.text.toDouble()));
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
            if (!expect("(")) {
                recover();
                return -1;
            }
            n.a = logic();
            expect(")");
            n.b = statement();
            if (atName("else")) {
                ++pos;
                n.c = statement();
            }
            return node(n);
        }
        if (atName("return")) {
            ++pos;
            if (atSymbol("(")) {
                ++pos;
                expect(")");
            }
            expect(";");
            ScriptNode n;
            n.kind = ScriptNode::Return;
            n.line = line;
            return node(n);
        }
        if (atName("for") || atName("while") || atName("do") || atName("switch")) {
            // Not in the language: reported, the loop passed over (its
            // parentheses and the statement after them).
            problem(QString("%1 is not in the language").arg(peek().text));
            ++pos;
            if (atSymbol("(")) {
                int depth = 0;
                do {
                    depth += atSymbol("(") ? 1 : atSymbol(")") ? -1 : 0;
                    ++pos;
                } while (depth > 0 && !at(Token::End));
            }
            const size_t problems = size_t(s.problems.size());
            statement();
            while (size_t(s.problems.size()) > problems)
                s.problems.removeLast();
            return -1;
        }
        if (at(Token::Name) && atSymbol("=", 1)) {
            // Assignments, chained (a = b = 1): from the right.
            std::vector<std::pair<int, int>> targets;
            while (at(Token::Name) && atSymbol("=", 1)) {
                const QString name = peek().text;
                std::pair<int, int> target{-1, 0};
                if (locals.contains(name)) {
                    target = {ScriptNode::Local, locals.value(name)};
                } else if (variables().contains(name) && variables().value(name) != ScriptVariable::Enabled) {
                    target = {ScriptNode::Variable, int(variables().value(name))};
                } else {
                    problem(QString("%1 cannot be set").arg(name));
                }
                targets.push_back(target);
                pos += 2;
            }
            int value = logic();
            for (auto it = targets.rbegin(); it != targets.rend(); ++it) {
                if (it->first < 0)
                    continue;
                ScriptNode n;
                n.kind = ScriptNode::Assign;
                n.value = it->first;
                n.target = it->second;
                n.a = value;
                n.line = line;
                value = node(n);
            }
            if (!expect(";"))
                recover();
            return value;
        }
        if (at(Token::Name) && atSymbol("(", 1)) {
            const int call = primary();
            if (!expect(";"))
                recover();
            return call;
        }
        problem(QString("unexpected %1").arg(peek().kind == Token::End ? QString("end") : peek().text));
        recover();
        return -1;
    }

    // && and || (AND, OR) of equal precedence, left to right.
    int logic() {
        int left = comparison();
        while (atSymbol("&&") || atSymbol("||") || atName("and") || atName("or")) {
            ScriptNode n;
            n.kind = ScriptNode::Logic;
            n.op = atSymbol("&&") || atName("and") ? ScriptNode::And : ScriptNode::Or;
            n.line = peek().line;
            ++pos;
            n.a = left;
            n.b = comparison();
            left = node(n);
        }
        return left;
    }

    // A negation takes the comparison after it (as Open Rails negates a
    // condition).
    int comparison() {
        if (atSymbol("!") || atName("not")) {
            ScriptNode n;
            n.kind = ScriptNode::Not;
            n.line = peek().line;
            ++pos;
            n.a = comparison();
            return node(n);
        }
        int left = arithmetic();
        static const std::array<std::pair<const char *, ScriptNode::Operator>, 12> operators = {{
            {"==#", ScriptNode::Equal}, {"!=#", ScriptNode::NotEqual}, {">=#", ScriptNode::GreaterEqual},
            {"<=#", ScriptNode::LessEqual}, {">#", ScriptNode::Greater}, {"<#", ScriptNode::Less},
            {"==", ScriptNode::Equal}, {"!=", ScriptNode::NotEqual}, {">=", ScriptNode::GreaterEqual},
            {"<=", ScriptNode::LessEqual}, {">", ScriptNode::Greater}, {"<", ScriptNode::Less},
        }};
        for (const auto &[symbol, op] : operators)
            if (atSymbol(symbol)) {
                ScriptNode n;
                n.kind = ScriptNode::Compare;
                n.op = op;
                n.line = peek().line;
                ++pos;
                n.a = left;
                n.b = arithmetic();
                return node(n);
            }
        return left;
    }

    // + - * / % of equal precedence, left to right.
    int arithmetic() {
        int left = unary();
        while (atSymbol("+") || atSymbol("-") || atSymbol("*") || atSymbol("/") || atSymbol("%")) {
            ScriptNode n;
            n.kind = ScriptNode::Arithmetic;
            const QString &op = peek().text;
            n.op = op == "+" ? ScriptNode::Add : op == "-" ? ScriptNode::Subtract : op == "*" ? ScriptNode::Multiply
                 : op == "/" ? ScriptNode::Divide : ScriptNode::Modulo;
            n.line = peek().line;
            ++pos;
            n.a = left;
            n.b = unary();
            left = node(n);
        }
        return left;
    }

    int unary() {
        if (atSymbol("-")) {
            ScriptNode n;
            n.kind = ScriptNode::Negate;
            n.line = peek().line;
            ++pos;
            n.a = unary();
            return node(n);
        }
        if (atSymbol("!") || atName("not")) {
            ScriptNode n;
            n.kind = ScriptNode::Not;
            n.line = peek().line;
            ++pos;
            n.a = unary();
            return node(n);
        }
        return primary();
    }

    int number(int value, int line) {
        ScriptNode n;
        n.kind = ScriptNode::Number;
        n.value = value;
        n.line = line;
        return node(n);
    }

    int primary() {
        const Token token = peek();
        if (atSymbol("(")) {
            ++pos;
            const int inner = logic();
            expect(")");
            return inner;
        }
        if (token.kind == Token::Number) {
            ++pos;
            return number(token.value, token.line);
        }
        if (token.kind != Token::Name) {
            problem(QString("unexpected %1").arg(token.kind == Token::End ? QString("end") : token.text));
            if (!at(Token::End))
                ++pos;
            return number(0, token.line);
        }
        ++pos;
        const QString &name = token.text;
        if (atSymbol("(")) {
            ++pos;
            std::vector<int> arguments;
            while (!at(Token::End) && !atSymbol(")")) {
                arguments.push_back(logic());
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
                return number(0, token.line);
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
        if (variables().contains(name)) {
            ScriptNode n;
            n.kind = ScriptNode::Variable;
            n.value = int(variables().value(name));
            n.line = token.line;
            return node(n);
        }
        if (name == "block_state") {
            ScriptNode n;
            n.kind = ScriptNode::Variable;
            n.value = int(ScriptVariable::BlockState);
            n.line = token.line;
            return node(n);
        }
        if (constants().contains(name))
            return number(constants().value(name), token.line);
        if (openRailsNames().contains(name)) {
            if (!s.openRails.contains(name))
                s.openRails << name;
            return number(0, token.line);
        }
        problem(QString("unknown name %1").arg(name), token.line);
        return number(0, token.line);
    }
};

class Runner {
public:
    Runner(const SignalScript &script, SignalScriptHost &host, int &state, int &drawState)
        : s(script), h(host), state(state), draw(drawState) {
        if (s.locals <= int(small.size())) {
            small.fill(0);
            locals = small.data();
        } else {
            large.assign(size_t(s.locals), 0);
            locals = large.data();
        }
    }
    void run() { execute(s.root); }

private:
    const SignalScript &s;
    SignalScriptHost &h;
    int &state;
    int &draw;
    std::array<int, 32> small{};
    std::vector<int> large;
    int *locals = nullptr;
    bool stopped = false;

    void execute(int index) {
        if (index < 0 || stopped)
            return;
        const ScriptNode &n = s.nodes[size_t(index)];
        switch (n.kind) {
        case ScriptNode::Block:
            for (int i = 0; i < n.count && !stopped; ++i)
                execute(s.children[size_t(n.first + i)]);
            break;
        case ScriptNode::If:
            if (value(n.a) != 0)
                execute(n.b);
            else
                execute(n.c);
            break;
        case ScriptNode::Return:
            stopped = true;
            break;
        default:
            value(index);
            break;
        }
    }

    int argument(const ScriptNode &n, int i) {
        return i < n.count ? value(s.children[size_t(n.first + i)]) : 0;
    }

    int value(int index) {
        if (index < 0)
            return 0;
        const ScriptNode &n = s.nodes[size_t(index)];
        switch (n.kind) {
        case ScriptNode::Number:
            return n.value;
        case ScriptNode::Local:
            return locals[n.value];
        case ScriptNode::Variable:
            switch (ScriptVariable(n.value)) {
            case ScriptVariable::State: return state;
            case ScriptVariable::DrawState: return draw;
            case ScriptVariable::Enabled: return h.enabled();
            case ScriptVariable::BlockState: return h.blockState();
            }
            return 0;
        case ScriptNode::Not:
            return value(n.a) == 0 ? 1 : 0;
        case ScriptNode::Negate:
            return -value(n.a);
        case ScriptNode::Arithmetic: {
            const int a = value(n.a), b = value(n.b);
            switch (n.op) {
            case ScriptNode::Add: return a + b;
            case ScriptNode::Subtract: return a - b;
            case ScriptNode::Multiply: return a * b;
            case ScriptNode::Divide: return b == 0 ? 0 : a / b;
            default: return b == 0 ? 0 : a % b;
            }
        }
        case ScriptNode::Compare: {
            const int a = value(n.a), b = value(n.b);
            switch (n.op) {
            case ScriptNode::Equal: return a == b;
            case ScriptNode::NotEqual: return a != b;
            case ScriptNode::Less: return a < b;
            case ScriptNode::LessEqual: return a <= b;
            case ScriptNode::Greater: return a > b;
            default: return a >= b;
            }
        }
        case ScriptNode::Logic: {
            // Both sides, as Open Rails evaluates a condition.
            const bool a = value(n.a) != 0, b = value(n.b) != 0;
            return n.op == ScriptNode::And ? (a && b) : (a || b);
        }
        case ScriptNode::Assign: {
            const int v = value(n.a);
            if (n.value == ScriptNode::Local)
                locals[n.target] = v;
            else if (ScriptVariable(n.target) == ScriptVariable::State)
                state = v;
            else if (ScriptVariable(n.target) == ScriptVariable::DrawState)
                draw = v;
            return v;
        }
        case ScriptNode::Call:
            return call(n);
        default:
            return 0;
        }
    }

    int call(const ScriptNode &n) {
        switch (ScriptFunction(n.value)) {
        case ScriptFunction::BlockState: return h.blockState();
        case ScriptFunction::RouteSet: return h.routeSet();
        case ScriptFunction::NextSigLr: return h.nextSignal(argument(n, 0), true, 1);
        case ScriptFunction::NextSigMr: return h.nextSignal(argument(n, 0), false, 1);
        case ScriptFunction::ThisSigLr: return h.thisSignal(argument(n, 0), true);
        case ScriptFunction::ThisSigMr: return h.thisSignal(argument(n, 0), false);
        case ScriptFunction::OppSigLr: return h.oppositeSignal(argument(n, 0), true);
        case ScriptFunction::OppSigMr: return h.oppositeSignal(argument(n, 0), false);
        case ScriptFunction::NextNsigLr: return h.nextSignal(argument(n, 0), true, argument(n, 1));
        case ScriptFunction::DistMultiSigMr: return h.distantMulti(argument(n, 0), argument(n, 1));
        case ScriptFunction::SigFeature: return h.feature(argument(n, 0));
        case ScriptFunction::DefDrawState: return h.drawStateOf(argument(n, 0));
        case ScriptFunction::DebugOut:
            for (int i = 0; i < n.count; ++i)
                argument(n, i);
            return 0;
        default:
            return 0;
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
