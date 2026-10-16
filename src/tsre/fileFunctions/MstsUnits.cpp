#include <tsre/fileFunctions/MstsUnits.h>

#include <QHash>
#include <QRegularExpression>

namespace MstsUnits {

namespace {

constexpr double Pound = 0.45359237;
constexpr double PoundForce = 4.44822162;
constexpr double Mile = 0.44704;

double factor(Kind kind, const QString &unit) {
    if (unit.isEmpty())
        return 1.0;
    static const QHash<QString, double> mass = {{"kg", 1.0}, {"t", 1000.0}, {"t-uk", 1016.0469088},
                                                {"t-us", 907.18474}, {"lb", Pound}};
    static const QHash<QString, double> force = {{"n", 1.0}, {"kn", 1000.0}, {"lbf", PoundForce},
                                                 {"klbf", 1000.0 * PoundForce}};
    static const QHash<QString, double> power = {{"w", 1.0}, {"kw", 1000.0}, {"hp", 745.699872}};
    static const QHash<QString, double> speed = {{"m/s", 1.0}, {"mps", 1.0}, {"km/h", 1.0 / 3.6},
                                                 {"kph", 1.0 / 3.6}, {"kmh", 1.0 / 3.6}, {"kmph", 1.0 / 3.6},
                                                 {"mph", Mile}};
    static const QHash<QString, double> resistance = {{"n/m/s", 1.0}, {"n/(m/s)", 1.0}, {"ns/m", 1.0},
                                                      {"lbf/mph", PoundForce / Mile}};
    static const QHash<QString, double> squared = {{"n/(m/s)^2", 1.0}, {"n/m/s^2", 1.0}, {"ns^2/m^2", 1.0},
                                                   {"n/(m/s)2", 1.0}, {"lbf/mph^2", PoundForce / (Mile * Mile)},
                                                   {"lbf/mph2", PoundForce / (Mile * Mile)}};
    const QHash<QString, double> *table = nullptr;
    switch (kind) {
    case Kind::Mass: table = &mass; break;
    case Kind::Force: table = &force; break;
    case Kind::Power: table = &power; break;
    case Kind::Speed: table = &speed; break;
    case Kind::Resistance: table = &resistance; break;
    case Kind::ResistanceSquared: table = &squared; break;
    case Kind::None: return 0.0;
    }
    return table->value(unit.toLower(), 0.0);
}

}

double value(const QString &token, Kind kind, bool *ok) {
    static const QRegularExpression number(R"(^\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)\s*(.*?)\s*$)");
    // Some files quote their values ("39.19t", "117.57t #24.298t empty"):
    // the token is the first word.
    QString bare = token.trimmed();
    if (bare.startsWith('"'))
        bare.remove(0, 1);
    if (bare.endsWith('"'))
        bare.chop(1);
    // A comment written on to it ("166.9t#368000lbs").
    bare = bare.section('#', 0, 0);
    const QRegularExpressionMatch match = number.match(bare);
    double scale = match.hasMatch() ? factor(kind, match.captured(2)) : 0.0;
    if (ok != nullptr)
        *ok = scale != 0.0;
    return scale != 0.0 ? match.captured(1).toDouble() * scale : 0.0;
}

}
