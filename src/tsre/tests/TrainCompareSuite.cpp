/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tests/TrainCompareSuite.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopedValueRollback>
#include <QString>
#include <algorithm>
#include <cmath>
#include <map>
#include <tsre/Game.h>
#include <tsre/maszyna/MaszynaCabControls.h>
#include <tsre/maszyna/MaszynaVehicle.h>
#include <tsre/sim/SimulationCore.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/sim/TrackGeometry.h>
#include <tsre/sim/TrainRoute.h>
#include <tsre/sim/VehicleModel.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tests/TokenTestSupport.h>
#include <tsre/tests/TrackLayoutSupport.h>
#include <tsre/trains/PathTrack.h>
#include <tsre/trains/VehicleDefinition.h>

namespace {

// A command at a time: a cab control's label and its value (MaSzyna's
// positions), as maszyna.cab takes the mouse's.
struct Timed {
    double at = 0.0;
    QString label;
    double value = 0.0;
};

// The scenario's commands, ramps one position at a time, in time order.
std::vector<Timed> commandsOf(const QJsonArray &list) {
    static const std::map<QString, QString> labels = {{"direction", "dirkey"},
                                                      {"mainController", "mainctrl"},
                                                      {"secondController", "scndctrl"},
                                                      {"trainBrakePosition", "brakectrl"},
                                                      {"localBrakePosition", "localbrake"}};
    std::map<QString, double> at;
    std::vector<Timed> out;
    for (const QJsonValue &item : list) {
        const QJsonObject o = item.toObject();
        const double t = o.value("t").toDouble();
        for (const auto &[key, label] : labels) {
            if (!o.contains(key))
                continue;
            const QJsonValue v = o.value(key);
            if (v.isObject()) {
                // { to, every }: from where it is, a position each `every`.
                const double to = v.toObject().value("to").toDouble();
                const double every = std::max(0.01, v.toObject().value("every").toDouble(1.0));
                double now = at[label];
                double when = t;
                while (std::fabs(to - now) > 0.5) {
                    now += to > now ? 1.0 : -1.0;
                    out.push_back({when, label, now});
                    when += every;
                }
                at[label] = to;
            } else {
                out.push_back({t, label, v.toDouble()});
                at[label] = v.toDouble();
            }
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Timed &a, const Timed &b) { return a.at < b.at; });
    return out;
}

}

int TsreTests::runTrainCompareSuite(bool verbose) {
    TokenTest::Suite test{"[tests:train-compare]", verbose};
    const QString scenarioPath = qEnvironmentVariable("TSRE_TRAIN_SCENARIO");
    const QString csvPath = qEnvironmentVariable("TSRE_TRAIN_CSV");
    if (scenarioPath.isEmpty() || csvPath.isEmpty()) {
        qInfo() << "[tests:train-compare] SKIP: TSRE_TRAIN_SCENARIO and TSRE_TRAIN_CSV not set";
        return 0;
    }
    QFile file(scenarioPath);
    const QJsonObject scenario = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object()
                                                                : QJsonObject();
    test.check(!scenario.isEmpty(), "the scenario read: " + scenarioPath);
    if (scenario.isEmpty())
        return test.finish();

    // The train, front to back.
    const QString dynamic = QDir::cleanPath(Game::root) + "/dynamic/";
    std::vector<Sim::Vehicle> vehicles;
    QStringList missing;
    for (const QJsonValue &item : scenario.value("consist").toArray()) {
        const QJsonObject o = item.toObject();
        const auto definition = MaszynaVehicles::definition(dynamic + o.value("folder").toString(),
                                                            o.value("tsre").toString());
        if (definition == nullptr) {
            missing << o.value("folder").toString() + "/" + o.value("tsre").toString();
            continue;
        }
        Sim::Vehicle v;
        v.definition = definition;
        v.flip = o.value("flipped").toBool(false);
        for (int i = 0; i < std::max(1, o.value("count").toInt(1)); ++i)
            vehicles.push_back(v);
    }
    test.check(missing.isEmpty() && !vehicles.empty(), "its vehicles found " + missing.join(", "));
    if (vehicles.empty() || !missing.isEmpty())
        return test.finish();

    // Straight level track, the scenario's length in 100 m pieces and the
    // train's length again behind its start.
    double length = 0.0;
    for (const Sim::Vehicle &v : vehicles)
        length += v.length();
    if (scenario.value("track").toObject().value("grade").toDouble(0.0) != 0.0)
        qInfo() << "[tests:train-compare] the grade is not supported yet: level track";
    QScopedValueRollback<TDB *> restoreTrack(Game::trackDB);
    TestTrack::Layout straight;
    const double trackLength = scenario.value("track").toObject().value("length").toDouble(20000.0) + length + 200.0;
    straight.end(1, 10);
    straight.track(10, 1, 2, int(std::ceil(trackLength / 100.0)), true);
    straight.end(2, 10);
    TDB *line = &straight.db;
    Game::trackDB = line;
    const PathTrack::Result run = PathTrack::startAt(line, {10, 100.0f});
    const Sim::TrainRoute route = Sim::BuildRoute(line, run.nodes, PathTrack::legs(line, run.nodes), 60.0);
    const int t0 = 8 * 3600;
    Sim::TrainPlan plan;
    plan.vehicles = vehicles;
    plan.length = float(length);
    plan.maxSpeed = 60.0f;
    Sim::SetRates(plan);
    plan.startTime = t0;
    plan.route = route;
    plan.startFront = route.pathStart + plan.length + 1.0;
    plan.startSpeed = float(scenario.value("startSpeed").toDouble(0.0) / 3.6);
    auto input = std::make_shared<Sim::SimulationInput>();
    input->startTime = t0;
    input->geometry = std::make_shared<const Sim::TrackGeometry>(line);
    input->trains.push_back(plan);
    input->step = Sim::SimulationCore::DrivenStep;
    input->driven = 0;
    input->locoScripts = QDir::current().filePath(QString("appdata/") + Game::AppDataVersion + "/scripts/loco");
    input->lineVoltage = scenario.value("lineVoltage").toDouble(3000.0);
    Sim::SimulationCore core(input);

    // The front cab's controls by label (maszyna.cab's).
    const std::shared_ptr<const CabControls> controls = vehicles.front().definition->cab3DControlsAt(false);
    const auto *maszyna = dynamic_cast<const MaszynaCabControls *>(controls.get());
    std::map<QString, int> index;
    if (maszyna != nullptr)
        for (int i = 0; i < maszyna->end().controls.size(); ++i)
            if (!index.count(maszyna->end().controls[i].label))
                index[maszyna->end().controls[i].label] = i;
    const std::vector<Timed> commands = commandsOf(scenario.value("commands").toArray());
    QStringList unknown;
    for (const Timed &c : commands)
        if (!index.count(c.label) && !unknown.contains(c.label))
            unknown << c.label;
    test.check(maszyna != nullptr && unknown.isEmpty(), "its cab's controls: " + unknown.join(", "));

    QFile csv(csvPath);
    QDir().mkpath(QFileInfo(csvPath).absolutePath());
    if (!csv.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        test.check(false, "the CSV opened: " + csvPath);
        return test.finish();
    }
    csv.write("t,v,s,main,actual,second,im,force,brakeForce,pipe,pipeLast,cyl,cylFirst,cylLast,mr,er\n");
    const double duration = scenario.value("duration").toDouble(300.0);
    const double sample = std::max(0.01, scenario.value("sample").toDouble(0.5));
    const double dt = 0.01;
    const int n = int(vehicles.size());
    const auto number = [&](int v, const char *name, double otherwise = 0.0) {
        const Sim::VehicleOutputs *out = core.outputsOf(0, v);
        if (out == nullptr || !out->values.contains(name))
            return otherwise;
        return Sim::Message{QString(), out->values.value(name)}.number(otherwise);
    };
    const auto text = [&](int v, const char *name) {
        const Sim::VehicleOutputs *out = core.outputsOf(0, v);
        return out ? Sim::Message{QString(), out->values.value(name, QString())}.text() : QString();
    };
    const auto bar = [](double pa) { return QString::number(pa / 1e5, 'f', 3); };
    // The driver acknowledging the vigilance device (maszyna.security's
    // alerter): a press, a release a fifth of a second on.
    const double acknowledge = scenario.value("acknowledge").toDouble(20.0);
    double nextPress = acknowledge > 0.0 ? acknowledge : -1.0, release = -1.0;
    size_t next = 0;
    double nextSample = 0.0;
    int rows = 0;
    core.step(dt);
    for (double t = 0.0; t <= duration + 1e-9; t += dt) {
        while (next < commands.size() && commands[next].at <= t + 1e-9) {
            const Timed &c = commands[next++];
            if (index.count(c.label))
                core.command(0, -1, {QString("cab.%1").arg(index[c.label]), c.value});
        }
        if (nextPress >= 0.0 && t + 1e-9 >= nextPress) {
            core.command(0, -1, {QStringLiteral("alerter"), QStringLiteral("on")});
            release = t + 0.2;
            nextPress += acknowledge;
        }
        if (release >= 0.0 && t + 1e-9 >= release) {
            core.command(0, -1, {QStringLiteral("alerter"), QStringLiteral("off")});
            release = -1.0;
        }
        if (t + 1e-9 >= nextSample) {
            nextSample += sample;
            const Sim::TrainState &state = core.trains()[0];
            double brakeForce = 0.0;
            for (int v = 0; v < n; ++v)
                brakeForce += number(v, "airBrakeForce") + std::max(0.0, number(v, "dynamicBrakeForce"));
            const QString notch = text(0, "throttleNotch");
            const QString main = notch.isEmpty() ? QString() : notch.section('/', 0, 0).trimmed();
            QStringList row = {QString::number(t, 'f', 2),
                               QString::number(state.speed * 3.6, 'f', 3),
                               QString::number(state.travelled, 'f', 2),
                               main,
                               QString::number(number(0, "controllerPosition"), 'f', 0),
                               QString::number(number(0, "shuntFieldPosition"), 'f', 0),
                               QString::number(number(0, "motorCurrent"), 'f', 1),
                               QString::number(number(0, "force") / 1000.0, 'f', 2),
                               QString::number(brakeForce / 1000.0, 'f', 2),
                               bar(number(0, "brakePipe")),
                               bar(number(n - 1, "brakePipe")),
                               bar(number(0, "brakeCylinder")),
                               n > 1 ? bar(number(1, "brakeCylinder")) : QString(),
                               bar(number(n - 1, "brakeCylinder")),
                               bar(number(0, "mainReservoir")),
                               bar(number(0, "equalisingReservoir"))};
            csv.write((row.join(',') + "\n").toUtf8());
            ++rows;
        }
        core.step(dt);
    }
    for (const auto &[vehicle, m] : core.takeMessages(0))
        if (m.name == "scriptError")
            test.check(false, QString("a script error (vehicle %1): %2").arg(vehicle).arg(m.text()));
    test.check(rows > 0 && next == commands.size(),
               QString("%1: %2 rows to %3, %4 commands sent").arg(scenario.value("name").toString()).arg(rows)
                       .arg(csvPath).arg(next));
    return test.finish();
}
