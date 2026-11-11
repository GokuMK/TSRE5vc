/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tests/MaszynaCabTestSuite.h>
#include <tsre/tests/TokenTestSupport.h>
#include <tsre/maszyna/MaszynaCab.h>
#include <tsre/maszyna/MaszynaCabControls.h>
#include <tsre/maszyna/MaszynaVehicle.h>
#include <tsre/shape/E3dShape.h>
#include <tsre/sim/LocoScript.h>
#include <tsre/sim/VehicleModel.h>
#include <tsre/trains/VehicleDefinition.h>
#include <tsre/Game.h>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QMap>
#include <QSet>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

void writeFile(const QString &path, const QByteArray &bytes) {
    QFile out(path);
    if (out.open(QIODevice::WriteOnly))
        out.write(bytes);
}

bool near(float a, float b) {
    return std::fabs(a - b) <= 1e-6f * std::max(1.0f, std::fabs(b));
}

const MaszynaCab::Control *find(const MaszynaCab::End &end, const char *label) {
    for (const MaszynaCab::Control &c : end.controls)
        if (c.label == QLatin1String(label))
            return &c;
    return nullptr;
}

QString keyValue(const MaszynaCab::Control &c, const char *key) {
    for (const auto &k : c.keys)
        if (k.first == QLatin1String(key))
            return k.second;
    return QStringLiteral("(none)");
}

QStringList labels(const MaszynaCab::End &end) {
    QStringList list;
    for (const MaszynaCab::Control &c : end.controls)
        list << c.label;
    return list;
}

}

int TsreTests::runMmdCabSuite(bool verbose) {
    TokenTest::Suite test{"[tests:mmd-cab]", verbose};
    QTemporaryDir dir;
    writeFile(dir.path() + "/a.mmd",
              "models: body.t3d endmodels\n"
              "internaldata:\n"
              "horn1: { soundset: [ a.wav b.wav ] range: 900 }\n"
              "mainctrl: before rot 1 0 0\n"
              "cab1definition:\n"
              "cablight: 1 1 1 1 1 1 1 1 1\n"
              "-1.2 1.5 7.35 1.2 1.5 8.5\n"
              "driver1angle: 10 -5\n"
              "driver1pos: 0.92 3.35 7.3\n"
              "driver1sitpos: -0.76 3.1 7.71\n"
              "scndctrl: early rot 0.1 0 0\n"
              "cab1model: \\veh/Kabina_A.t3d\n"
              "mainctrl: { Nastawnik rot -0.0197 0.0 0.1 sound0: a.wav sound-1: b.wav soundinc: { soundmain: c.wav }\n"
              "            type: push foo: soundproofing: [ 0.2 1 0.2 0.05 0.0025 0.0025 ] }\n"
              "cab2definition:\n"
              "-1.2 1.5 -8.55 1.2 1.5 -7.3\n"
              "driver2sitpos: 1 2 3\n"
              "driver2pos: 4 5 6\n"
              "cab2model: veh/kabina_b.t3d\n"
              "brakectrl: object01 rotvar 0.073 0.07 0.1 5 0.2\n"
              "pipepress: { strzalka rot 1.0 0 0.5 }\n"
              "i-mainbreaker: wylszybki\n"
              "i-resistors: { oporki soundinc: x.wav }\n"
              "i-doors: 2 drzwi\n"
              "brakes: 1 2 brk rot 0.5 0 0\n"
              "clock: analog\n"
              "dirkey: knob rot 0.08 0.01 0.1sounddec: x.wav\n"
              "pyscreen: timetable scripts/timetable\n"
              "pyscreen: { scripts/sterownik target: ekran updatetime: 100 }\n"
              "pyscreenupdatetime: 50\n"
              "i-edenabled foo\n"
              "unknown_sw: bar rot 1 2 3\n"
              "pydestinationsign: { scripts/led parameters: a=b instance: name }\n"
              "destinationsignbackground: make:scripts/led?dummy=1\n"
              "couplingdisconnect_sw: sprzeg2 mov -0.1 0 0.1 soundinc: cisza.wav sounddec: cisza.wav }\n"
              "i-doors: dvere\n"
              "i-battery: bateria\n"
              "sand_bt: piasek mov -0.007 0 0.2\n"
              "include cab0.inc (p2) end\n"
              "oiltemp: x rot 1 0 0\n");
    writeFile(dir.path() + "/cab0.inc",
              "cab0definition:\n"
              "-1 1 -1 1 2 1\n"
              "driver0pos: 0 2 0\n"
              "cab0model: veh/(p1).t3d\n"
              "oilpress: needle rot 1 0 0\n");

    const MaszynaCab cab = MaszynaCab::read(dir.path(), "a.mmd", {"name", "maszynownia"});
    const MaszynaCab::End &one = cab.ends[1], &two = cab.ends[2], &zero = cab.ends[0];
    test.check(cab.hasCab() && one.defined && two.defined && zero.defined, "three cabs");
    test.check(near(one.box[0], -1.2f) && near(one.box[2], 7.35f) && near(one.box[5], 8.5f),
               "cablight:'s nine numbers passed over, the box after them");
    test.check(one.hasAngle && near(one.angle[0], 10.0f) && near(one.angle[1], -5.0f) && one.hasPosition
                       && near(one.position[0], 0.92f) && near(one.position[2], 7.3f) && one.hasSitting
                       && near(one.sitting[0], -0.76f),
               "angle (yaw, pitch), head and seated head in MaSzyna's order");
    test.check(one.model == "veh/kabina_a.t3d", "the cab's model in lower case, / and no leading one: " + one.model);
    test.check(labels(one)
                       == QStringList({"mainctrl", "brakectrl", "pipepress", "i-mainbreaker", "i-resistors",
                                       "i-doors", "brakes", "clock", "dirkey", "couplingdisconnect_sw",
                                       "i-doors", "sand_bt"}),
               "cab 1: nothing before its definition, what came before its model cleared, cab 2's section "
               "read too, nothing after cab0definition: " + labels(one).join(' '));
    test.check(labels(two)
                       == QStringList({"brakectrl", "pipepress", "i-mainbreaker", "i-resistors", "i-doors",
                                       "brakes", "clock", "dirkey", "couplingdisconnect_sw", "i-doors",
                                       "sand_bt"}),
               "cab 2: from its own section: " + labels(two).join(' '));
    test.check(!two.hasAngle && !two.hasPosition && two.hasSitting && near(two.sitting[2], 3.0f)
                       && two.model == "veh/kabina_b.t3d",
               "a head after the seated head is out of order and passed over");
    test.check(labels(zero) == QStringList({"oilpress", "oiltemp"}) && zero.hasPosition
                       && zero.model == "veh/maszynownia.t3d",
               "cab 0 from an include with its (pN), to the end: " + labels(zero).join(' ') + " " + zero.model);

    const MaszynaCab::Control *main = find(one, "mainctrl");
    test.check(main != nullptr && main->submodel == "Nastawnik" && main->animation == MaszynaCab::Animation::Rotate
                       && near(main->scale, -0.0197f) && near(main->offset, 0.0f) && near(main->friction, 0.1f)
                       && main->type == MaszynaCab::Type::Push && !main->lamp,
               "a block: submodel as written, rot, scale, offset, friction, type: push");
    test.check(main != nullptr && keyValue(*main, "sound0") == "a.wav" && keyValue(*main, "sound-1") == "b.wav"
                       && keyValue(*main, "soundinc") == "{ soundmain: c.wav }" && keyValue(*main, "foo") == ""
                       && keyValue(*main, "soundproofing") == "0.2",
               "block keys: sounds by value, a sound block, an unknown key without a value, soundproofing's set");
    const MaszynaCab::Control *brake = find(two, "brakectrl");
    test.check(brake != nullptr && brake->interpolated && brake->animationName == "rotvar"
                       && near(brake->endValue, 5.0f) && near(brake->endScale, 0.2f)
                       && brake->type == MaszynaCab::Type::Toggle,
               "old syntax with rotvar's end value and scale; toggle by default");
    const MaszynaCab::Control *pipe = find(two, "pipepress");
    test.check(pipe != nullptr && near(pipe->scale, 0.1f) && near(pipe->friction, 0.5f),
               "a pressure gauge's scale times 0.1");
    const MaszynaCab::Control *breaker = find(two, "i-mainbreaker");
    const MaszynaCab::Control *resistors = find(two, "i-resistors");
    const MaszynaCab::Control *doors = find(two, "i-doors");
    test.check(breaker != nullptr && breaker->lamp && breaker->submodel == "wylszybki" && resistors != nullptr
                       && resistors->lamp && resistors->submodel == "oporki"
                       && keyValue(*resistors, "soundinc") == "x.wav" && doors != nullptr && doors->lamp
                       && doors->index[0] == 2 && doors->submodel == "drzwi",
               "lamps: a word, a block, i-doors: with its number");
    const MaszynaCab::Control *brakes = find(two, "brakes");
    const MaszynaCab::Control *clock = find(two, "clock");
    const MaszynaCab::Control *dirkey = find(two, "dirkey");
    test.check(brakes != nullptr && brakes->index[0] == 1 && brakes->index[1] == 2 && brakes->submodel == "brk"
                       && near(brakes->scale, 0.05f) && clock != nullptr && clock->word == "analog"
                       && dirkey != nullptr && dirkey->submodel == "knob" && near(dirkey->friction, 0.1f),
               "brakes: i j and a pressure gauge, clock: analog, a number by its leading digits");
    test.check(two.screens.size() == 2 && two.screens[0].target == "timetable"
                       && two.screens[0].script == "scripts/timetable" && two.screens[1].target == "ekran"
                       && two.screens[1].script == "scripts/sterownik" && two.screens[1].keys.size() == 1,
               "Python screens, old and block");
    const MaszynaCab::Control *coupling = find(two, "couplingdisconnect_sw");
    const MaszynaCab::Control *sand = find(two, "sand_bt");
    test.check(coupling != nullptr && coupling->submodel == "sprzeg2" && near(coupling->scale, -0.1f)
                       && coupling->keys.isEmpty() && sand != nullptr && sand->submodel == "piasek"
                       && near(sand->friction, 0.2f),
               "unknown labels passed over with their words: a destination sign, block keys without their block");
    test.check(two.controls.size() > 9 && two.controls[9].label == "i-doors" && two.controls[9].index[0] == 0
                       && two.controls[9].submodel == "i-battery:" && find(two, "i-battery") == nullptr,
               "a known entry takes its words whatever they are: i-doors: without its number eats the next label");
    test.check(cab.problems.size() == 1 && cab.problems[0] == "i-doors: \"dvere\" is no number",
               "a word that is no number is a problem: " + cab.problems.join("; "));
    test.check(!MaszynaCab::read(dir.path(), "missing.mmd").hasCab(), "no file, no cab");
    return test.finish();
}

int TsreTests::runMmdCabCorpusSuite(bool verbose) {
    TokenTest::Suite test{"[tests:mmd-cab-corpus]", verbose};
    const QString root = QDir::cleanPath(Game::root) + "/dynamic";
    if (!QFileInfo(root).isDir()) {
        qInfo() << "[tests:mmd-cab-corpus] SKIP: no dynamic/ in the game root" << Game::root;
        return 0;
    }
    QElapsedTimer timer;
    timer.start();
    int files = 0, withCab = 0, drivable = 0, controls = 0;
    int doorsWithoutNumber = 0, shortEntries = 0, baseFiles = 0;
    QSet<QString> cabModels;
    QMap<QString, int> animations;
    QStringList problems;
    QDirIterator it(root, {"*.mmd"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        if (!QFileInfo::exists(info.path() + "/" + info.completeBaseName() + ".fiz"))
            continue;
        const MaszynaCab cab = MaszynaCab::read(info.path(), info.fileName());
        ++files;
        if (!cab.problems.isEmpty()) {
            // The pack's own: i-doors: without its number, entries short of
            // a number, base files in cfg/ whose includes are beside the
            // vehicles including them.
            const QString all = cab.problems.join(", ");
            if (all.startsWith("i-doors: ") && cab.problems.size() == 1)
                ++doorsWithoutNumber;
            else if (all.contains("is no number"))
                ++shortEntries;
            else if (info.path().endsWith("/cfg"))
                ++baseFiles;
            else if (problems.size() < 8)
                problems << QDir(root).relativeFilePath(info.filePath()) + ": " + all;
        }
        if (!cab.hasCab())
            continue;
        ++withCab;
        bool drives = false;
        for (int n = 1; n <= 2; ++n)
            for (const MaszynaCab::Control &c : cab.ends[n].controls)
                drives = drives || c.label == "mainctrl" || c.label == "jointctrl" || c.label == "brakectrl"
                         || c.label == "dirkey";
        for (const MaszynaCab::End &end : cab.ends)
            for (const MaszynaCab::Control &c : end.controls) {
                ++controls;
                if (!c.lamp)
                    ++animations[c.animationName.isEmpty() ? QStringLiteral("(none)") : c.animationName];
            }
        if (drives) {
            ++drivable;
            const QString model = !cab.ends[1].model.isEmpty() ? cab.ends[1].model : cab.ends[2].model;
            cabModels.insert(QDir::cleanPath(info.path().toLower() + "/" + model));
        }
    }
    const qint64 ms = timer.elapsed();
    QStringList summary;
    for (auto i = animations.cbegin(); i != animations.cend(); ++i)
        summary << QString("%1 %2").arg(i.key()).arg(i.value());
    qInfo().noquote() << "[tests:mmd-cab-corpus]" << files << "vehicles in" << ms << "ms;" << withCab << "with a cab,"
                      << drivable << "drivable," << cabModels.size() << "cab models;" << controls
                      << "controls in all cabs;" << summary.join(", ");
    test.check(problems.isEmpty() && doorsWithoutNumber == 77 && shortEntries == 3 && baseFiles == 2,
               QString("only the pack's own errors: %1 i-doors: without its number, %2 entries short of a number, "
                       "%3 base files; others: %4")
                       .arg(doorsWithoutNumber)
                       .arg(shortEntries)
                       .arg(baseFiles)
                       .arg(problems.join("; ")));
    // The review's counts (reports/mmd-cab-controls.md, 2026-11-10), with
    // the 8 EN57 units (pkp/en57km_v1/*czoper*) its script missed: their
    // .mmd starts with a UTF-8 byte order mark before its include, which
    // MaSzyna's parser skips.
    test.check(files == 1453, QString("the review's 1453 vehicles (%1)").arg(files));
    test.check(withCab == 1117, QString("1117 with a cab (%1)").arg(withCab));
    test.check(drivable == 666, QString("666 drivable (%1)").arg(drivable));
    test.check(cabModels.size() == 291, QString("the review's 291 cab models (%1)").arg(cabModels.size()));

    // ET22 (pkp/et22_v3/201e.mmd:139-238, cab0.mmd).
    const QString et22 = root + "/pkp/et22_v3";
    if (QFileInfo::exists(et22 + "/201e.mmd")) {
        const MaszynaCab cab = MaszynaCab::read(et22, "201e.mmd");
        const MaszynaCab::End &one = cab.ends[1], &two = cab.ends[2];
        test.check(one.model == "201e/kabina_a.t3d" && two.model == "201e/kabina_b.t3d" && cab.ends[0].defined,
                   "ET22: kabina_a, kabina_b, the engine room from its include");
        test.check(near(one.position[0], 0.92f) && near(one.position[1], 3.35f) && near(one.position[2], 7.3f)
                           && near(one.sitting[0], -0.76f) && near(two.position[2], -7.3f)
                           && near(two.sitting[0], 0.76f) && near(one.box[2], 7.35f) && near(two.box[2], -8.55f),
                   "ET22: heads, seated heads and boxes as written");
        test.check(labels(one) == labels(two) && one.controls.size() > 50,
                   QString("ET22: both cabs the same %1 controls, written once after cab 2").arg(one.controls.size()));
        const MaszynaCab::Control *main = find(one, "mainctrl");
        test.check(main != nullptr && main->submodel == "nastawnik" && near(main->scale, -0.0197f)
                           && near(main->friction, 0.1f) && keyValue(*main, "sound48") != "(none)",
                   "ET22: the main controller, 48 notches' sounds");
        const MaszynaCab::Control *breaker = find(one, "i-mainbreaker");
        test.check(breaker != nullptr && breaker->lamp && breaker->submodel == "wylszybki", "ET22: a lamp");
    }
    return test.finish();
}

int TsreTests::runMmdCabScriptSuite(bool verbose) {
    TokenTest::Suite test{"[tests:mmd-cab-script]", verbose};
    const QString folder = QDir::cleanPath(Game::root) + "/dynamic/pkp/et22_v3";
    if (!QFileInfo(folder).isDir()) {
        qInfo() << "[tests:mmd-cab-script] SKIP: no dynamic/pkp/et22_v3 in the game root" << Game::root;
        return 0;
    }
    const auto et22 = MaszynaVehicles::definition(folder, "201e#201e-037.mmd");
    const std::shared_ptr<const VehicleDefinition::Cab3D> cab = et22 ? et22->cab3DOf() : nullptr;
    test.check(cab != nullptr && cab->viewpoints.size() == 4 && !cab->mirrored,
               "ET22: a 3D cab at each end, standing and seated");
    if (cab == nullptr || cab->viewpoints.size() != 4)
        return test.finish();
    const auto &front = cab->viewpoints[0], &rear = cab->viewpoints[2], &seated = cab->viewpoints[1];
    test.check(near(seated.position[0], -0.76f) && near(seated.position[2], 7.71f) && near(seated.start[1], 0.0f)
                       && seated.shape == front.shape && near(cab->viewpoints[3].position[0], 0.76f),
               "the seated place after the standing one, in the same cab");
    test.check(front.hiddenParts == QStringList({"cab1"}) && seated.hiddenParts == QStringList({"cab1"})
                       && rear.hiddenParts == QStringList({"cab2"}) && cab->lightClass[0] == "cab1"
                       && cab->lightClass[1] == "cab2",
               "its low-poly interior's cab<N> hidden from cab N, and lit by its light");
    test.check(front.shape.endsWith("201e/kabina_a.e3d") && rear.shape.endsWith("201e/kabina_b.e3d")
                       && QFileInfo(front.shape).isFile() && QFileInfo(rear.shape).isFile(),
               "its cab models' files, .e3d for .t3d: " + front.shape);
    test.check(near(front.position[0], 0.92f) && near(front.position[2], 7.3f) && near(front.start[1], 0.0f)
                       && near(rear.position[2], -7.3f) && near(rear.start[1], 180.0f),
               "the heads at driver1pos and driver2pos, looking toward their ends");
    test.check(front.hasBox && near(front.box[1], 2.0f) && near(front.box[4], 3.3f) && near(front.box[2], 7.35f)
                       && near(front.box[5], 8.5f),
               "the box: the head 0.5 to 1.8 m over the cab's floor");
    const std::shared_ptr<const CabControls> controls = et22->cab3DControlsAt(false);
    test.check(controls != nullptr && et22->cab3DControlsAt(true) != nullptr && controls != et22->cab3DControlsAt(true)
                       && controls->drawsBody() && controls->module() == "maszyna.cab" && controls->count() > 50,
               "controls for each end, drawn on top of the body, read by maszyna.cab");
    if (controls == nullptr)
        return test.finish();
    const auto &end = static_cast<const MaszynaCabControls &>(*controls).end();
    const auto index = [&](const char *label) {
        for (int i = 0; i < end.controls.size(); ++i)
            if (end.controls[i].label == QLatin1String(label))
                return i;
        return -1;
    };
    // The cab model bound: its parts for the mouse from the file read again.
    {
        const QFileInfo model(front.shape);
        E3dShape shape(model.filePath(), model.fileName(), model.path());
        shape.load();
        const std::unique_ptr<CabPose> pose = shape.isLoaded() ? controls->bind(shape, front.shape) : nullptr;
        const CabPicker::Loaded loaded = pose ? pose->pickerJob()() : CabPicker::Loaded();
        QSet<int> picked;
        if (loaded.picker)
            for (const CabPicker::Part &part : loaded.picker->parts())
                picked.insert(part.control);
        test.check(shape.isLoaded() && loaded.picker != nullptr && picked.size() > 30,
                   QString("the mouse's parts: %1 controls").arg(picked.size()));
    }
    test.check(MaszynaCabControls::worked("mainctrl") && MaszynaCabControls::worked("compressor_sw")
                       && MaszynaCabControls::worked("universal3") && !MaszynaCabControls::worked("pipepress")
                       && !MaszynaCabControls::worked("i-mainbreaker"),
               "the mouse works levers, switches, buttons and universals; not needles and lamps");

    const QString locoFolder = QDir::current().filePath(QString("appdata/") + Game::AppDataVersion + "/scripts/loco");
    Sim::LocoScripts scripts(locoFolder);
    std::unique_ptr<Sim::VehicleModel> model = scripts.make(et22);
    test.check(model != nullptr, "ET22 gets its script");
    if (!model)
        return test.finish();
    Sim::VehicleInputs in;
    in.dt = 0.01;
    in.cab = true;
    in.lineVoltage = 3000.0;
    in.brakePipe = 500000.0;
    in.brakePipeVolume = 0.0169;
    in.speed = 10.0;
    Sim::VehicleOutputs out;
    const auto step = [&](int steps) {
        for (int i = 0; i < steps; ++i) {
            out.events.clear();
            model->step(in, out);
        }
    };
    const auto cabCommand = [&](const char *label, const Sim::MessageValue &value) {
        model->command({QString("cab.%1").arg(index(label)), value});
    };
    std::vector<double> values;
    std::shared_ptr<const CabStops> stops;
    const auto valueOf = [&](const char *label) {
        values.clear();
        model->cabValues(*controls, in.speed, values, &stops);
        const int i = index(label);
        return i >= 0 && size_t(i) < values.size() ? values[size_t(i)] : std::numeric_limits<double>::quiet_NaN();
    };
    step(1);
    valueOf("mainctrl");
    const auto stopsOf = [&](const char *label) {
        const int i = index(label);
        return stops != nullptr && i >= 0 && size_t(i) < stops->size() ? (*stops)[size_t(i)] : std::vector<double>();
    };
    const std::vector<double> main = stopsOf("mainctrl"), brake = stopsOf("brakectrl"), dir = stopsOf("dirkey");
    test.check(main.size() == 49 && main.front() == 0.0 && main.back() == 48.0 && dir == std::vector<double>({-1, 0, 1})
                       && stopsOf("localbrake").size() == 11 && stopsOf("compressor_sw").empty(),
               QString("stops: the main controller 0 to 48, the direction key -1 to 1, the local brake 0 to 10, "
                       "a switch none (main %1)").arg(main.size()));
    QStringList brakeText;
    for (double p : brake)
        brakeText << QString::number(p);
    test.check(brake.size() >= 4 && std::is_sorted(brake.begin(), brake.end()),
               "the train brake handle's stops, its .fiz positions in order: " + brakeText.join(' '));

    test.check(std::fabs(valueOf("tachometer") - 36.0) < 1e-6 && std::fabs(valueOf("pipepress") - 5.0) < 1e-6,
               QString("gauges: km/h (%1), bar (%2)").arg(valueOf("tachometer")).arg(valueOf("pipepress")));
    cabCommand("mainctrl", 6.0);
    step(5);
    test.check(valueOf("mainctrl") == 6.0, QString("the main controller dragged to 6 shows 6 (%1)").arg(valueOf("mainctrl")));
    cabCommand("mainctrl", QString("-"));
    step(5);
    test.check(valueOf("mainctrl") == 5.0, "the wheel steps it back to 5");
    if (brake.size() >= 4) {
        cabCommand("brakectrl", brake[2]);
        step(5);
        test.check(valueOf("brakectrl") == brake[2],
                   QString("the train brake handle set to position %1 shows it (%2)").arg(brake[2]).arg(valueOf("brakectrl")));
    }
    cabCommand("dirkey", 1.0);
    step(5);
    test.check(valueOf("dirkey") == 1.0, "the direction key forward");
    const double before = valueOf("compressor_sw");
    cabCommand("compressor_sw", QString("press"));
    cabCommand("compressor_sw", QString("release"));
    step(1);
    test.check(before == 0.0 && valueOf("compressor_sw") == 1.0, "a switch TSRE models nothing of still turns");
    cabCommand("main_on_bt", QString("press"));
    step(1);
    const double held = valueOf("main_on_bt");
    cabCommand("main_on_bt", QString("release"));
    step(1);
    test.check(held == 1.0 && valueOf("main_on_bt") == 0.0, "a button held while pressed");
    test.check(!std::isnan(valueOf("i-mainbreaker")) && !std::isnan(valueOf("i-resistors")),
               QString("lamps given (the breaker's %1)").arg(valueOf("i-mainbreaker")));

    // ST44's cab: a switch without an entry sent to the loco by its label
    // (maszyna.auxiliaries takes fuelpump_sw) and shown at its value; its
    // start button (main_on_bt, circuitBreaker "close") MaSzyna's main
    // switch, starting the engine only with the fuel pump on; "open" (a stop
    // button's) stopping it.
    const QString st44Folder = QDir::cleanPath(Game::root) + "/dynamic/pkp/st44_v2";
    if (QFileInfo(st44Folder).isDir()) {
        const auto st44 = MaszynaVehicles::definition(st44Folder, "st44-1030#st44-1030.mmd");
        const std::shared_ptr<const CabControls> st44Controls = st44 ? st44->cab3DControlsAt(false) : nullptr;
        std::unique_ptr<Sim::VehicleModel> loco = st44Controls ? scripts.make(st44) : nullptr;
        test.check(loco != nullptr, "ST44 gets its script and its cab's controls");
        if (!loco)
            return test.finish();
        const auto &st44End = static_cast<const MaszynaCabControls &>(*st44Controls).end();
        const auto at = [&](const char *label) {
            for (int i = 0; i < st44End.controls.size(); ++i)
                if (st44End.controls[i].label == QLatin1String(label))
                    return i;
            return -1;
        };
        Sim::VehicleOutputs st44Out;
        in.speed = 0.0;
        const auto run = [&](double seconds) {
            for (int i = 0; i < int(seconds / in.dt); ++i) {
                st44Out.events.clear();
                loco->step(in, st44Out);
            }
        };
        const auto click = [&](const char *label) {
            loco->command({QString("cab.%1").arg(at(label)), QString("press")});
            loco->command({QString("cab.%1").arg(at(label)), QString("release")});
        };
        const auto shown = [&](const char *label) {
            std::vector<double> v;
            loco->cabValues(*st44Controls, in.speed, v, nullptr);
            const int i = at(label);
            return i >= 0 && size_t(i) < v.size() ? v[size_t(i)] : std::numeric_limits<double>::quiet_NaN();
        };
        const auto state = [&] { return Sim::Message{QString(), st44Out.values.value("engineState")}.text(); };
        run(1.0);
        const bool hot = shown("fuelpump_sw") == 1.0 && state() == "running";
        click("fuelpump_sw");
        run(90.0);
        const bool starved = shown("fuelpump_sw") == 0.0 && state() == "stopped";
        click("main_on_bt");
        run(20.0);
        const bool noFuel = state() == "stopped";
        click("fuelpump_sw");
        click("main_on_bt");
        run(45.0);
        const bool started = shown("fuelpump_sw") == 1.0 && state() == "running";
        // Its cab model has no stop button: main_off_bt's command.
        loco->command({QStringLiteral("circuitBreaker"), QString("open")});
        run(90.0);
        const bool stopped = state() == "stopped";
        test.check(at("fuelpump_sw") >= 0 && hot && starved && noFuel && started && stopped,
                   QString("ST44's cab: its fuel pump switch on at a hot start %1; turned off, the engine stops %2; "
                           "its start button without fuel does nothing %3, with it starts the engine %4; the main "
                           "switch off stops it %5")
                           .arg(hot).arg(starved).arg(noFuel).arg(started).arg(stopped));
    }
    return test.finish();
}
