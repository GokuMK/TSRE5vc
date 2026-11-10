/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/maszyna/MaszynaFiz.h>
#include <tsre/maszyna/MaszynaText.h>
#include <QDir>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace {

constexpr double Kmh = 1.0 / 3.6;
constexpr double KiloNewton = 1000.0;
constexpr double KiloWatt = 1000.0;
constexpr double Bar = 100000.0;
// The brake handle's rows (Cntrl.), as MaSzyna holds pressures inside.
constexpr double MegaPascal = 1000000.0;
// kN per km/h in N per m/s.
constexpr double KiloNewtonPerKmh = 1000.0 * 3.6;
constexpr double RevPerSecond = 60.0;
// g/kWh in kg/J.
constexpr double GramPerKwh = 0.001 / 3600000.0;

// The keys whose unit is not SI as written, or whose value is text even
// where it reads as a number (Type=181).
struct Key {
    const char *section;
    const char *key;
    double factor; // 0: text
};

const Key Keys[] = {
    {"Param", "Category", 0}, {"Param", "Type", 0}, {"Param", "Vmax", Kmh}, {"Param", "PWR", KiloWatt},
    {"Param", "HeatingP", KiloWatt},
    {"Wheels", "Axle", 0}, {"Wheels", "BearingType", 0},
    {"Brake", "BrakeValve", 0}, {"Brake", "BM", 0}, {"Brake", "CompressorPower", 0},
    {"Brake", "MBF", KiloNewton}, {"Brake", "TBF", KiloNewton}, {"Brake", "MaxBP", Bar}, {"Brake", "MaxLBP", Bar},
    {"Brake", "TareMaxBP", Bar}, {"Brake", "MedMaxBP", Bar}, {"Brake", "MaxASBP", Bar}, {"Brake", "BCS", KiloNewton},
    {"Brake", "BSA", KiloNewton}, {"Brake", "BVV", 0.001}, {"Brake", "RV", Kmh}, {"Brake", "HiPP", Bar},
    {"Brake", "LoPP", Bar}, {"Brake", "MinCP", Bar}, {"Brake", "MaxCP", Bar}, {"Brake", "MinCP_B", Bar},
    {"Brake", "MaxCP_B", Bar}, {"Brake", "CompressorSpeed", Bar}, {"Brake", "MinEVP", Bar}, {"Brake", "MaxEVP", Bar},
    {"Cntrl", "MaxBPMass", 1000.0},
    {"BuffCoupl", "kB", KiloNewton}, {"BuffCoupl", "kC", KiloNewton}, {"BuffCoupl", "FmaxB", KiloNewton},
    {"BuffCoupl", "FmaxC", KiloNewton},
    {"BuffCoupl1", "kB", KiloNewton}, {"BuffCoupl1", "kC", KiloNewton}, {"BuffCoupl1", "FmaxB", KiloNewton},
    {"BuffCoupl1", "FmaxC", KiloNewton},
    {"BuffCoupl2", "kB", KiloNewton}, {"BuffCoupl2", "kC", KiloNewton}, {"BuffCoupl2", "FmaxB", KiloNewton},
    {"BuffCoupl2", "FmaxC", KiloNewton},
    {"Security", "AwareMinSpeed", Kmh},
    {"Power", "MinPress", Bar}, {"Power", "MaxPress", Bar},
    {"Engine", "Trans", 0}, {"Engine", "Vhyp", Kmh}, {"Engine", "Vadd", Kmh}, {"Engine", "F0", KiloNewton},
    {"Engine", "a1", KiloNewtonPerKmh}, {"Engine", "Pmax", KiloWatt}, {"Engine", "Fh", KiloNewton},
    {"Engine", "Ph", KiloWatt}, {"Engine", "Vh0", Kmh}, {"Engine", "Vh1", Kmh}, {"Engine", "TC_LS", Kmh},
    {"Engine", "TC_ULS", Kmh},
    {"DList", "nMmax", RevPerSecond}, {"DList", "nmax", RevPerSecond}, {"DList", "NomFuelConsRate", GramPerKwh},
};

// Table columns not in SI as written; columns past the list as written.
struct Columns {
    const char *table;
    double factor[8];
    int count;
};

const Columns TableColumns[] = {
    {"Cntrl", {1.0, MegaPascal, MegaPascal, 1.0}, 4},
    {"WWList", {1.0, KiloWatt, 1.0, 1.0, 1.0, 1.0, KiloWatt}, 7},
    {"PmaxList", {Kmh, KiloWatt}, 2},
    {"V2NList", {Kmh, 1.0}, 2},
};

// MaSzyna's sections in its order (Mover.cpp, LoadFIZ_Parse).
const char *const Headers[] = {
    "Param.", "Load:", "Dimensions:", "Wheels:", "Brake:", "Doors:", "BuffCoupl.", "BuffCoupl1.", "BuffCoupl2.",
    "TurboPos:", "Cntrl.", "Headlights:", "Blending:", "DCEMUED:", "SpringBrake:", "Light:", "Security:", "Clima:",
    "Power:", "SpeedControl:", "Engine:", "Switches:", "MotorParamTable:", "MotorParamTable0:", "Circuit:", "AI:",
    "RList:", "UCList:", "DList:", "DMList:", "HTCList:", "PmaxList:", "V2NList:", "ffList:", "ffBrakeList:",
    "WWList:", "WiperList:", "DimmerList:", "LightsList:", "CompressorList:",
};

// The tables, in the order a row goes to the first open one.
const char *const Tables[] = {
    "Cntrl", "MotorParamTable", "MotorParamTable0", "RList", "UCList", "DList", "DMList", "V2NList", "HTCList",
    "PmaxList", "ffList", "ffBrakeList", "WWList", "WiperList", "DimmerList", "LightsList", "CompressorList",
};
constexpr int TableCount = int(std::size(Tables));

// The END markers and the tables they close.
struct End {
    const char *marker;
    const char *tables[2];
};
const End Ends[] = {
    {"END-MPT", {"MotorParamTable", "MotorParamTable0"}}, {"END-RL", {"RList", nullptr}},
    {"END-UCL", {"UCList", nullptr}}, {"END-DL", {"DList", nullptr}}, {"END-DML", {"DMList", nullptr}},
    {"END-V2NL", {"V2NList", nullptr}}, {"END-HTCL", {"HTCList", nullptr}}, {"END-PML", {"PmaxList", nullptr}},
    {"endff", {"ffList", "ffBrakeList"}}, {"END-WWL", {"WWList", nullptr}}, {"endwl", {"WiperList", nullptr}},
    {"endDimmerList", {"DimmerList", nullptr}}, {"endL", {"LightsList", nullptr}}, {"endCL", {"CompressorList", nullptr}},
};

int tableIndex(QByteArrayView name) {
    for (int i = 0; i < TableCount; ++i)
        if (name == QByteArrayView(Tables[i]))
            return i;
    return -1;
}

const Key *keyOf(const QString &section, const QString &key) {
    for (const Key &k : Keys)
        if (section == QLatin1String(k.section) && key == QLatin1String(k.key))
            return &k;
    return nullptr;
}

double columnFactor(const QString &table, int column) {
    for (const Columns &c : TableColumns)
        if (table == QLatin1String(c.table))
            return column < c.count ? c.factor[column] : 1.0;
    return 1.0;
}

// A number as MaSzyna's stringstream reads it: the longest number at the
// start; `whole` when nothing follows it.
bool leadingNumber(QByteArrayView text, double &out, bool &whole) {
    const QByteArray copy = text.toByteArray();
    const char *start = copy.constData();
    char *end = nullptr;
    out = std::strtod(start, &end);
    if (end == start)
        return false;
    whole = *end == '\0';
    return true;
}

MaszynaFiz::Value valueOf(QByteArrayView text, double factor, bool knownKey) {
    MaszynaFiz::Value v;
    double n = 0.0;
    bool whole = false;
    if (!(knownKey && factor == 0.0) && leadingNumber(text, n, whole) && (whole || knownKey)) {
        v.isNumber = true;
        v.number = n * factor;
    } else {
        v.text = MaszynaText::decode(text);
    }
    return v;
}

MaszynaFiz::Section &sectionNamed(MaszynaFiz &fiz, const QString &name) {
    for (MaszynaFiz::Section &s : fiz.sections)
        if (s.name == name)
            return s;
    fiz.sections.push_back({});
    fiz.sections.back().name = name;
    return fiz.sections.back();
}

void setKey(MaszynaFiz::Section &section, const QString &key, const MaszynaFiz::Value &value) {
    for (auto &kv : section.keys)
        if (kv.first == key) {
            kv.second = value;
            return;
        }
    section.keys.push_back({key, value});
}

// A header line's keys (` Key=value`, the value after any spaces, to the
// next space; a key's first in the line counts).
void readKeys(MaszynaFiz::Section &section, QByteArrayView line) {
    QStringList seen;
    qsizetype at = 0;
    while (at < line.size()) {
        const qsizetype equals = line.indexOf('=', at);
        if (equals < 0)
            break;
        qsizetype start = equals;
        while (start > 0 && line[start - 1] != ' ')
            --start;
        qsizetype from = equals + 1;
        while (from < line.size() && line[from] == ' ')
            ++from;
        qsizetype to = from;
        while (to < line.size() && line[to] != ' ')
            ++to;
        at = std::max(to, equals + 1);
        // A key after a tab or at the line's start is not MaSzyna's (it looks
        // for " Key=").
        if (start == 0 || start == equals)
            continue;
        const QByteArrayView keyText = line.sliced(start, equals - start);
        if (keyText.contains('\t'))
            continue;
        const QString key = QString::fromLatin1(keyText);
        if (seen.contains(key))
            continue;
        seen << key;
        const Key *known = keyOf(section.name, key);
        setKey(section, key, valueOf(line.sliced(from, to - from), known ? known->factor : 1.0, known != nullptr));
    }
}

void readRow(MaszynaFiz::Section &section, QByteArrayView line) {
    QVector<MaszynaFiz::Value> row;
    qsizetype at = 0;
    while (at < line.size()) {
        while (at < line.size() && (line[at] == ' ' || line[at] == '\t'))
            ++at;
        qsizetype end = at;
        while (end < line.size() && line[end] != ' ' && line[end] != '\t')
            ++end;
        if (end > at)
            row.push_back(valueOf(line.sliced(at, end - at), columnFactor(section.name, int(row.size())), false));
        at = end;
    }
    if (!row.isEmpty())
        section.rows.push_back(row);
}

} // namespace

const MaszynaFiz::Value *MaszynaFiz::Section::value(QStringView key) const {
    for (const auto &kv : keys)
        if (kv.first == key)
            return &kv.second;
    return nullptr;
}

double MaszynaFiz::Section::number(QStringView key, double otherwise) const {
    const Value *v = value(key);
    return v != nullptr && v->isNumber ? v->number : otherwise;
}

QString MaszynaFiz::Section::text(QStringView key) const {
    const Value *v = value(key);
    return v == nullptr ? QString() : v->isNumber ? QString::number(v->number) : v->text;
}

const MaszynaFiz::Section *MaszynaFiz::section(QStringView name) const {
    for (const Section &s : sections)
        if (s.name == name)
            return &s;
    return nullptr;
}

double MaszynaFiz::number(QStringView section, QStringView key, double otherwise) const {
    const Section *s = this->section(section);
    return s != nullptr ? s->number(key, otherwise) : otherwise;
}

QString MaszynaFiz::text(QStringView section, QStringView key) const {
    const Section *s = this->section(section);
    return s != nullptr ? s->text(key) : QString();
}

MaszynaFiz MaszynaFiz::read(const QString &folder, const QString &name) {
    MaszynaFiz fiz;
    MaszynaText text(folder, name);
    if (!text.opened()) {
        fiz.problems << QDir::cleanPath(folder + QLatin1Char('/') + name);
        return fiz;
    }
    bool open[TableCount] = {};
    QByteArrayView line;
    while (text.nextLine(line)) {
        if (line.contains('#'))
            continue;
        bool ended = false;
        for (const End &e : Ends)
            if (line.startsWith(QByteArrayView(e.marker))) {
                for (const char *table : e.tables)
                    if (table != nullptr)
                        open[tableIndex(table)] = false;
                ended = true;
                break;
            }
        if (ended)
            continue;
        const char *header = nullptr;
        for (const char *h : Headers)
            if (line.startsWith(QByteArrayView(h))) {
                header = h;
                break;
            }
        if (header != nullptr) {
            const qsizetype length = qsizetype(std::strlen(header));
            const QByteArrayView nameText(header, length - 1);
            Section &section = sectionNamed(fiz, QString::fromLatin1(nameText));
            // Another header ends Cntrl.'s rows; a table's header opens its.
            open[0] = false;
            const int table = tableIndex(nameText);
            if (table >= 0)
                open[table] = true;
            readKeys(section, line.sliced(length));
            continue;
        }
        for (int i = 0; i < TableCount; ++i)
            if (open[i]) {
                readRow(sectionNamed(fiz, QString::fromLatin1(Tables[i])), line);
                break;
            }
    }
    fiz.files = text.files();
    fiz.problems = text.problems();
    return fiz;
}
