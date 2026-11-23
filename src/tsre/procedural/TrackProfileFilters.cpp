/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine.
 *  Licensed under GNU General Public License 3.0 or later.
 */

#include <tsre/procedural/TrackProfileFilters.h>

#include <QDebug>
#include <QDir>
#include <QTextStream>
#include <cmath>
#include <tsre/fileFunctions/RouteFiles.h>
#include <tsre/procedural/OrtsStf.h>
#include <tsre/procedural/TrackShapeAnalysis.h>
#include <tsre/tdb/TrackShape.h>
#include <tsre/units/UnitConversion.h>

QVector<TrackProfileFilters::Filter> TrackProfileFilters::loaded;
QHash<int, int> TrackProfileFilters::kinds;

namespace {

QString at(int line) {
    return line > 0 ? QString("line %1: ").arg(line) : QString();
}

// Part names into flags; unknown ones warned about and left out.
unsigned partsOf(const OrtsStf::Node &node, QStringList &diagnostics) {
    unsigned parts = 0;
    for(const QString &value : node.values){
        const unsigned part = TrackProfileFilters::partFromName(value);
        if(part == 0)
            diagnostics.append(at(node.line) + node.name + ": unknown part \"" + value + "\" ignored");
        parts |= part;
    }
    return parts;
}

bool length(const QString &token, float &metres) {
    double si = 0;
    Units::FileNumber read;
    if(!Units::fromFileToken(token, Units::Quantity::Length, si, {}, &read)
            || !read.whole || !read.unitKnown)
        return false;
    metres = float(si);
    return true;
}

bool degrees(const QString &token, float &value) {
    double si = 0;
    Units::FileNumber read;
    if(!Units::fromFileToken(token, Units::Quantity::Angle, si, u"deg", &read)
            || !read.whole || !read.unitKnown)
        return false;
    value = float(si * 180.0 / 3.14159265358979323846);
    return true;
}

bool filterFrom(const OrtsStf::Node &node, TrackProfileFilters::Filter &filter,
        QStringList &diagnostics) {
    filter.line = node.line;
    bool profileGiven = false;
    bool includedGiven = false;
    bool excludedGiven = false;
    unsigned included = 0;
    unsigned excluded = 0;
    for(const OrtsStf::Node &item : node.children){
        if(item.name.compare("Shapes", Qt::CaseInsensitive) == 0){
            for(const QString &value : item.values){
                const QString pattern = QString(value).replace('\\', '/').trimmed();
                if(pattern.isEmpty())
                    continue;
                filter.shapes.append(pattern);
                filter.patterns.append(OrtsStf::wildcard(pattern));
            }
        } else if(item.name.compare("Kind", Qt::CaseInsensitive) == 0){
            for(const QString &value : item.values){
                const int kind = TrackShapeAnalysis::kindFromName(value);
                if(kind == 0)
                    diagnostics.append(at(item.line) + "Kind: unknown value \"" + value + "\" ignored");
                filter.kinds |= kind;
            }
        } else if(item.name.compare("Profile", Qt::CaseInsensitive) == 0){
            const QString profile = item.values.isEmpty() ? QString() : item.values.first().trimmed();
            if(profile.isEmpty())
                continue;
            profileGiven = true;
            filter.disabled = profile.compare("DISABLED", Qt::CaseInsensitive) == 0;
            filter.profile = filter.disabled ? QString() : profile;
        } else if(item.name.compare("IncludedParts", Qt::CaseInsensitive) == 0){
            includedGiven = true;
            included |= partsOf(item, diagnostics);
        } else if(item.name.compare("ExcludedParts", Qt::CaseInsensitive) == 0){
            excludedGiven = true;
            excluded |= partsOf(item, diagnostics);
        } else if(item.name.compare("IncludedTrackShape", Qt::CaseInsensitive) == 0){
            TrackProfileFilters::IncludedTrackShape shape;
            bool ok = item.values.size() == 5;
            if(ok){
                bool isIndex = false;
                const int index = item.values[0].toInt(&isIndex);
                if(isIndex)
                    shape.index = index;
                else
                    shape.shape = item.values[0].trimmed();
                ok = !item.values[0].trimmed().isEmpty()
                        && length(item.values[1], shape.position[0])
                        && length(item.values[2], shape.position[1])
                        && length(item.values[3], shape.position[2])
                        && degrees(item.values[4], shape.rotationDeg);
            }
            if(ok)
                filter.includedTrackShapes.append(shape);
            else
                diagnostics.append(at(item.line) + "IncludedTrackShape needs ( shape x y z rotY ); ignored");
        } else if(item.name.compare("IncludedTrackShapeParts", Qt::CaseInsensitive) == 0){
            filter.includedTrackShapeParts = partsOf(item, diagnostics);
        } else {
            diagnostics.append(at(item.line) + "unknown Filter value " + item.name + " ignored");
        }
    }
    if(!node.values.isEmpty())
        diagnostics.append(at(node.line) + "Filter: values outside a block ignored: " + node.values.join(' '));
    if(filter.shapes.isEmpty() || !profileGiven){
        diagnostics.append(at(node.line) + "Filter without "
                + QString(filter.shapes.isEmpty() ? "Shapes" : "Profile") + " ignored");
        return false;
    }
    filter.parts = (includedGiven ? included : TrackProfileFilters::AllParts)
            & ~(excludedGiven ? excluded : 0u);
    return true;
}

}

QString TrackProfileFilters::fileName() {
    return "trackprofiles.dat";
}

bool TrackProfileFilters::Filter::matchesName(const QString &shapeFileName) const {
    const QString name = QString(shapeFileName).replace('\\', '/');
    for(const QRegularExpression &pattern : patterns)
        if(pattern.match(name).hasMatch())
            return true;
    return false;
}

QString TrackProfileFilters::Filter::description() const {
    QStringList quoted;
    for(const QString &shape : shapes)
        quoted.append("\"" + shape + "\"");
    QString text = QString("line %1: Shapes ( %2 )").arg(line).arg(quoted.join(' '));
    if(kinds != 0)
        text += " Kind ( " + TrackShapeAnalysis::kindNames(kinds).replace(",", "") + " )";
    text += " Profile ( " + (disabled ? QString("DISABLED") : profile) + " )";
    return text;
}

QVector<TrackProfileFilters::Filter> TrackProfileFilters::parse(const QString &text,
        QStringList *diagnostics) {
    QStringList local;
    QString normalized = text;
    if(normalized.startsWith(QChar(0xfeff)))
        normalized.remove(0, 1);
    QVector<int> lines;
    const QStringList tokens = OrtsStf::tokenize(normalized, local, &lines);
    QVector<Filter> result;
    bool rootFound = false;
    for(int rootIndex = 0; rootIndex + 1 < tokens.size(); rootIndex++){
        if(tokens[rootIndex].compare("TrackProfileFilters", Qt::CaseInsensitive) != 0
                || tokens[rootIndex + 1] != "(")
            continue;
        rootFound = true;
        int index = rootIndex + 1;
        OrtsStf::Node root;
        if(!OrtsStf::parseNode(tokens, index, tokens[rootIndex], root, local, &lines))
            break;
        rootIndex = index - 1;
        for(const OrtsStf::Node &node : root.children){
            if(node.name.compare("Filter", Qt::CaseInsensitive) != 0){
                local.append(at(node.line) + "unknown block " + node.name + " ignored");
                continue;
            }
            Filter filter;
            if(filterFrom(node, filter, local))
                result.append(filter);
        }
    }
    if(!rootFound)
        local.append("Missing TrackProfileFilters block");
    if(diagnostics != nullptr)
        diagnostics->append(local);
    return result;
}

void TrackProfileFilters::load(const QString &routePath) {
    loaded.clear();
    kinds.clear();
    const QString path = QDir::cleanPath(routePath) + "/" + fileName();
    QByteArray bytes;
    if(!RouteFiles::read(path, bytes))
        return;
    QTextStream stream(bytes);
    stream.setAutoDetectUnicode(true);
    QStringList diagnostics;
    loaded = parse(stream.readAll(), &diagnostics);
    qInfo().noquote() << fileName() + ":" << loaded.size() << "track profile filters";
    if(!diagnostics.isEmpty())
        qWarning().noquote() << fileName() + ": " + diagnostics.join("; ");
}

const QVector<TrackProfileFilters::Filter> &TrackProfileFilters::filters() {
    return loaded;
}

void TrackProfileFilters::set(const QVector<Filter> &filters) {
    loaded = filters;
    kinds.clear();
}

const TrackProfileFilters::Filter *TrackProfileFilters::match(const QString &shapeFileName,
        const std::function<int()> &shapeKinds) {
    if(loaded.isEmpty())
        return nullptr;
    const QString name = QString(shapeFileName).replace('\\', '/');
    int value = -1;
    for(const Filter &filter : loaded){
        if(!filter.matchesName(name))
            continue;
        if(filter.kinds != 0){
            if(value < 0)
                value = shapeKinds ? shapeKinds() : 0;
            if((filter.kinds & value) == 0)
                continue;
        }
        return &filter;
    }
    return nullptr;
}

int TrackProfileFilters::kindsOf(const TrackShape &shape,
        const std::unordered_map<int, TSection*> &sections) {
    const auto found = kinds.constFind(shape.id);
    if(found != kinds.constEnd())
        return found.value();
    const int value = TrackShapeAnalysis::analyse(shape, sections).kinds;
    kinds.insert(shape.id, value);
    return value;
}

unsigned TrackProfileFilters::partFromName(const QString &name) {
    static const struct { const char *name; Part part; } names[] = {
        {"Rail", Rail}, {"Tie", Tie}, {"Fastener", Fastener}, {"Bed", Bed},
        {"ConductorRail", ConductorRail}, {"RackRail", RackRail}, {"Other", Other}
    };
    const QString value = name.trimmed();
    for(const auto &entry : names)
        if(value.compare(QLatin1String(entry.name), Qt::CaseInsensitive) == 0)
            return entry.part;
    return 0;
}

QString TrackProfileFilters::partNames(unsigned parts) {
    static const struct { const char *name; Part part; } names[] = {
        {"Rail", Rail}, {"Tie", Tie}, {"Fastener", Fastener}, {"Bed", Bed},
        {"ConductorRail", ConductorRail}, {"RackRail", RackRail}, {"Other", Other}
    };
    QStringList result;
    for(const auto &entry : names)
        if(parts & entry.part)
            result.append(QLatin1String(entry.name));
    return result.join(' ');
}
