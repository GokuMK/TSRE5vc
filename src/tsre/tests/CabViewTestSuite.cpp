#include <tsre/tests/CabViewTestSuite.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStringEncoder>
#include <QTemporaryDir>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/script/ScriptState.h>
#include <tsre/shape/SFileComplex.h>
#include <tsre/shape/SFileLegacy.h>
#include <tsre/sim/LocoScript.h>
#include <tsre/sim/VehicleModel.h>
#include <tsre/tests/TokenTestSupport.h>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <tsre/trains/CabPicker.h>
#include <tsre/trains/CabView.h>
#include <tsre/trains/VehicleDefinition.h>

namespace {

const QString Header = "SIMISA@@@@@@@@@@JINX0h0t______\r\n\r\n";

// UTF-16 with its mark, as cab files are written.
bool write(const QString &path, const QString &text) {
    QDir().mkpath(QFileInfo(path).path());
    QStringEncoder encoder(QStringEncoder::Utf16LE);
    const QByteArray bytes = "\xff\xfe" + QByteArray(encoder(text));
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

const QString Controls = Header
        + "Tr_CabViewFile (\n"
          "  CabViewType ( 1 )\n"
          "  CabViewControls ( 8\n"
          "    Dial ( Type ( SPEEDOMETER DIAL ) Position ( 1 2 3 4 ) ScaleRange ( 0 160 ) Units ( KM_PER_HOUR ) )\n"
          "    Lever ( Type ( THROTTLE LEVER ) Position ( 10 20 30 40 ) Graphic ( kontroler.ace ) Style ( NOT_SPRUNG )\n"
          "      ScaleRange ( 0 1 ) NumFrames ( 11 11 1 ) MouseControl ( 1 ) Orientation ( 1 ) DirIncrease ( 1 )\n"
          "      NumPositions ( 11 0 1 2 3 4 5 6 7 8 9 10 ) NumValues ( 11 0 0.1 0.2 0.3 0.4 0.5 0.6 0.7 0.8 0.9 1 ) )\n"
          "    TwoState ( Type ( HORN TWO_STATE ) NumFrames ( 2 2 1 ) MouseControl ( 1 ) )\n"
          "    TwoState ( type ( horn two_state ) NumFrames ( 2 2 1 ) ORTSLabel ( \"Second horn\" ) )\n"
          "    TriState ( Type ( DIRECTION TRI_STATE ) NumFrames ( 3 3 1 ) NumPositions ( 3 2 1 0 ) NumValues ( 3 1 0 -1 ) )\n"
          "    Gauge ( Type ( AMMETER GAUGE ) Style ( POINTER ) ScaleRange ( -1000 1000 ) Units ( AMPS ) )\n"
          "    MultiStateDisplay ( Type ( WHEELSLIP MULTI_STATE_DISPLAY ) )\n"
          "    Lever ( Position ( 0 0 1 1 ) )\n"
          "  )\n"
          "  ORTSCabviewControls ( 1\n"
          "    TwoState ( Type ( ORTS_MASTER_KEY TWO_STATE ) NumFrames ( 2 2 1 ) )\n"
          "  )\n"
          ")\n";

// The file read: every control of the file, in order, as written.
void checkControls(TokenTest::Suite &test, const QString &game) {
    const QString path = game + "/TRAINS/TRAINSET/X/CABVIEW3D/x.cvf";
    write(path, Controls);
    const std::shared_ptr<const CabView> view = CabView::read(path, {game, QFileInfo(path).path()});
    const auto &c = view->controls();
    test.check(view->ok() && c.size() == 8 && view->problems().size() == 1
                       && view->problems()[0].message.contains("without a Type"),
               QString("%1 controls; a Lever without a Type a problem").arg(c.size()));
    if (c.size() != 8)
        return;
    test.check(c[0].kind == "Dial" && c[0].type == "SPEEDOMETER" && c[0].typeStyle == "DIAL" && c[0].minValue == 0
                       && c[0].maxValue == 160 && c[0].units == "KM_PER_HOUR" && c[0].position[2] == 3.0f,
               "a dial: its type, scale range, units and position");
    test.check(c[1].kind == "Lever" && c[1].frames == 11 && c[1].framesX == 11 && c[1].framesY == 1
                       && c[1].positions.size() == 11 && c[1].positions.back() == 10 && c[1].values.size() == 11
                       && c[1].values[5] == 0.5 && c[1].mouseControl && c[1].orientation == 1 && c[1].direction == 1
                       && c[1].style == "NOT_SPRUNG" && c[1].graphic == "kontroler.ace",
               "a lever: its frames, positions and values (the counts left out), the mouse, orientation, style");
    test.check(c[2].type == "HORN" && c[2].order == 0 && c[3].type == "HORN" && c[3].order == 1
                       && c[3].label == "Second horn" && !c[3].mouseControl,
               "two horns, orders 0 and 1 (keys in any case), the second's ORTSLabel");
    test.check(c[4].positions == std::vector<int>{2, 1, 0} && c[4].values == std::vector<double>{1, 0, -1}
                       && c[5].style == "POINTER" && c[5].minValue == -1000 && c[6].kind == "MultiStateDisplay"
                       && c[7].type == "ORTS_MASTER_KEY",
               "positions and values as written (descending too), a pointer gauge, Open Rails' extra controls after");
    test.check(view->find(u"horn", 1) == 3 && view->find(u"HORN", 2) == -1 && view->find(u"THROTTLE", 0) == 1,
               "a control found by type (any case) and order");
}

// Found as Open Rails finds it: beside the shape by its name, else the
// CabView file in CABVIEW3D, else in CABVIEW.
void checkFinding(TokenTest::Suite &test, const QString &game) {
    const QString trainset = game + "/TRAINS/TRAINSET";
    const auto eng = [&](const QString &folder, const QString &shape, const QString &cabView) {
        write(folder + "/x.eng", Header + "Wagon ( X Type ( Engine ) ORTS3DCab ( ORTS3DCabFile ( " + shape
                                         + " ) ORTS3DCabHeadPos ( 0 2 5 ) ) )\n"
                                           "Engine ( X Type ( Electric ) CabView ( "
                                         + cabView + " ) )\n");
        return VehicleDefinition::read(folder, "x.eng", game, false);
    };
    const QString a = trainset + "/A", b = trainset + "/B", c = trainset + "/C", d = trainset + "/D";
    for (const QString &f : {a, b, c})
        write(f + "/CABVIEW3D/cab.s", Header);
    write(a + "/CABVIEW3D/cab.cvf", Controls);
    write(a + "/CABVIEW3D/two.cvf", Controls);
    write(b + "/CABVIEW3D/two.cvf", Controls);
    write(b + "/CABVIEW/two.cvf", Controls);
    write(c + "/CABVIEW/two.cvf", Controls);
    // A shape in a shared folder, through a CABVIEW3D the vehicle does not
    // have (Open Rails resolves .. without the folder).
    write(trainset + "/common.cab/S/shared.s", Header);
    write(trainset + "/common.cab/S/shared.cvf", Controls);
    const auto beside = eng(a, "cab.s", "two.cvf"), in3D = eng(b, "cab.s", "two.cvf"), in2D = eng(c, "cab.s", "two.cvf");
    const auto shared = eng(d, "..//..//common.cab//S//shared.s", "two.cvf");
    test.check(beside->cab3DViewPath == a + "/CABVIEW3D/cab.cvf" && in3D->cab3DViewPath == b + "/CABVIEW3D/two.cvf"
                       && in2D->cab3DViewPath == c + "/CABVIEW/two.cvf"
                       && shared->cab3DViewPath == trainset + "/common.cab/S/shared.cvf",
               "beside the shape, else CabView in CABVIEW3D, else in CABVIEW; a shared shape through a missing "
               "CABVIEW3D: "
                       + shared->cab3DViewPath);
    test.check(beside->cab3DView && beside->cab3DView->controls().size() == 8 && shared->cab3DView
                       && shared->cab3DView->ok(),
               "the .cvf read with the definition");
    write(trainset + "/E/x.eng", Header + "Wagon ( X Type ( Engine ) ORTS3DCab ( ORTS3DCabFile ( none.s ) ) )\n");
    const auto noShape = VehicleDefinition::read(trainset + "/E", "x.eng", game, false);
    test.check(noShape->cab3DViewPath.isEmpty() && !noShape->cab3DView, "no shape there: no .cvf");
}

// The shape's part names matched to the controls.
void checkParts(TokenTest::Suite &test, const QString &game) {
    const QString path = game + "/TRAINS/TRAINSET/P/x.cvf";
    write(path, Controls);
    const std::shared_ptr<const CabView> view = CabView::read(path, {game, QFileInfo(path).path()});
    const QStringList names{"MAIN", "THROTTLE:0:0", "throttle:0:0-1", "HORN:1:0", "SPEEDOMETER:0:5:2",
                            "EXTERNALWIPERS:0:0", "ORTS_EXTERNALLEFTWINDOWFRONT:0", "CERVRUCICKA:0:0", "HORN:5:0",
                            "AMMETER:x", "DIRECTION:0"};
    const std::vector<float> last{-1, 10, 12, 1, 100, 40, 20, -1, 1, -1, -1};
    const CabParts parts = CabParts::match(view.get(), names, last);
    const CabParts::Control *throttle = parts.of(1), *horn = parts.of(3), *speed = parts.of(0), *dir = parts.of(4);
    test.check(throttle && throttle->parts == std::vector<int>{1, 2} && throttle->lastFrame == 12.0f && horn
                       && horn->parts == std::vector<int>{3} && speed && speed->parameter1 == "5"
                       && speed->parameter2 == "2" && dir && dir->lastFrame == -1.0f && parts.controls.size() == 4,
               "a control's parts (a sub-part too, any case), its last key frame, its parameters");
    test.check(parts.outside.size() == 2 && parts.outside[0].type == "EXTERNALWIPERS"
                       && parts.outside[1].type == "ORTS_EXTERNALLEFTWINDOWFRONT",
               "outside parts apart, needing no .cvf entry");
    test.check(parts.unmatched == QStringList{"CERVRUCICKA:0:0", "HORN:5:0"},
               "names no control has: " + parts.unmatched.join(", ") + " (MAIN and AMMETER:x are not cab parts)");
    const CabParts none = CabParts::match(nullptr, names, last);
    test.check(none.controls.empty() && none.outside.size() == 2 && none.unmatched.size() == 7,
               "without a .cvf only the outside parts");
}

// A value's frame, as Open Rails poses a 3D cab's part: through the tables
// it makes from NumPositions and NumValues, or the value's share of the
// scale for dials and gauges.
void checkFrames(TokenTest::Suite &test, const QString &game) {
    const QString path = game + "/TRAINS/TRAINSET/F/f.cvf";
    write(path, Header
                        + "Tr_CabViewFile ( CabViewControls ( 7\n"
                          "  Lever ( Type ( THROTTLE LEVER ) Graphic ( t.ace ) ScaleRange ( 0 1 ) NumFrames ( 11 11 1 )\n"
                          "    NumPositions ( 11 0 1 2 3 4 5 6 7 8 9 10 ) NumValues ( 11 0 0.1 0.2 0.3 0.4 0.5 0.6 0.7 0.8 0.9 1 ) )\n"
                          // Open Rails' own example (the Acela's train brake): frames
                          // between positions filled.
                          "  Lever ( Type ( TRAIN_BRAKE LEVER ) Graphic ( b.ace ) ScaleRange ( 0 1 ) NumFrames ( 12 4 3 )\n"
                          "    NumPositions ( 5 0 1 9 10 11 ) NumValues ( 5 0 0.2 0.85 0.9 0.95 ) )\n"
                          "  TwoState ( Type ( HORN TWO_STATE ) Graphic ( h.ace ) NumFrames ( 2 2 1 ) )\n"
                          "  TriState ( Type ( DIRECTION TRI_STATE ) NumFrames ( 3 3 1 ) NumPositions ( 3 2 1 0 ) NumValues ( 3 2 1 0 ) )\n"
                          "  Dial ( Type ( SPEEDOMETER DIAL ) ScaleRange ( 0 160 ) Units ( KM_PER_HOUR ) )\n"
                          "  MultiStateDisplay ( Type ( WHEELSLIP MULTI_STATE_DISPLAY ) States ( 2 2 1\n"
                          "    State ( Style ( 0 ) SwitchVal ( 0 ) ) State ( Style ( 0 ) SwitchVal ( 1 ) ) ) )\n"
                          "  Digital ( Type ( SPEEDOMETER DIGITAL ) ScaleRange ( 0 160 ) )\n"
                          ") )\n");
    const std::shared_ptr<const CabView> view = CabView::read(path, {game, QFileInfo(path).path()});
    const auto &c = view->controls();
    if (c.size() != 7) {
        test.check(false, QString("7 controls read, %1").arg(c.size()));
        return;
    }
    const auto f = [&](int i, double value, float last = 0.0f) { return CabView::frame(c[size_t(i)], value, last); };
    test.check(f(0, 0.32) == 3.0f && f(0, 0.38) == 4.0f && f(0, 1.0) == 10.0f && f(0, 70) == 7.0f && f(0, -1) == 0.0f,
               QString("a lever's notch, the nearest value (0.32 %1, 0.38 %2, 1 %3, 70 as per cent %4, below the scale %5)")
                       .arg(f(0, 0.32)).arg(f(0, 0.38)).arg(f(0, 1.0)).arg(f(0, 70)).arg(f(0, -1)));
    const std::vector<double> &brake = c[1].tableValues;
    test.check(c[1].tableFrames == 12 && brake.size() == 12 && std::fabs(brake[2] - (0.2 + 0.65 * 2 / 9)) < 1e-9
                       && brake[9] == 0.85 && f(1, 0.2) == 1.0f && f(1, 0.85) == 9.0f && f(1, 0.9) == 10.0f
                       && f(1, 0.5) == 4.0f,
               QString("the Acela's brake: the 7 frames between 0.2 and 0.85 filled as Open Rails fills them (%1 at "
                       "frame 2); 0.85 at frame %2, 0.5 at %3")
                       .arg(brake.size() > 2 ? brake[2] : -1).arg(f(1, 0.85)).arg(f(1, 0.5)));
    test.check(f(2, 0) == 0.0f && f(2, 1) == 1.0f && f(2, 5) == 1.0f && c[3].reversed
                       && c[3].tablePositions == std::vector<int>{0, 1, 2} && f(3, 2) == 2.0f && f(3, 0) == 0.0f,
               "switches: the state is the frame, at most the last; positions written descending turned");
    test.check(f(4, 80, 100) == 50.0f && f(4, 200, 100) == 100.0f && f(4, -5, 100) == 0.0f
                       && f(5, 1) == 1.0f && c[5].tableValues == std::vector<double>{0, 1} && f(6, 50, 10) == -1.0f,
               "a dial at its value's share of the scale times the part's last frame; a display's states; digits "
               "not in frames");
}

// The driven cab's controls' values: tsre.cabview over an electric loco's
// script, in each control's units.
void checkValues(TokenTest::Suite &test, const QString &game) {
    const QString folder = game + "/TRAINS/TRAINSET/V";
    const QString scripts = QDir::current().filePath(QString("appdata/") + Game::AppDataVersion + "/scripts/loco");
    if (!QFileInfo::exists(scripts + "/tsre/cabview.lua")) {
        test.check(false, "tsre/cabview.lua is in " + scripts);
        return;
    }
    write(folder + "/CABVIEW3D/cab.s", Header);
    write(folder + "/CABVIEW3D/cab.cvf",
          Header + "Tr_CabViewFile ( CabViewControls ( 8\n"
                   "  Dial ( Type ( SPEEDOMETER DIAL ) ScaleRange ( 0 160 ) Units ( KM_PER_HOUR ) )\n"
                   "  Dial ( Type ( SPEEDOMETER DIAL ) ScaleRange ( 0 100 ) Units ( MILES_PER_HOUR ) )\n"
                   "  Lever ( Type ( THROTTLE LEVER ) ScaleRange ( 0 1 ) NumFrames ( 11 11 1 ) )\n"
                   "  TriState ( Type ( DIRECTION TRI_STATE ) NumFrames ( 3 3 1 ) )\n"
                   "  Dial ( Type ( MAIN_RES DIAL ) ScaleRange ( 0 10 ) Units ( BAR ) )\n"
                   "  MultiState ( Type ( PANTOGRAPHS_4C MULTI_STATE ) NumFrames ( 4 4 1 ) )\n"
                   "  TwoState ( Type ( ORTS_MASTER_KEY TWO_STATE ) NumFrames ( 2 2 1 ) )\n"
                   "  TwoState ( Type ( CAB_RADIO TWO_STATE ) NumFrames ( 2 2 1 ) )\n"
                   ") )\n");
    write(folder + "/v.eng", Header + "Wagon ( V Type ( Engine ) Mass ( 80t ) ORTS3DCab ( ORTS3DCabFile ( cab.s ) ) )\n"
                                        "Engine ( V Type ( Electric ) MaxPower ( 5000kW ) MaxForce ( 300kN ) "
                                        "MaxVelocity ( 140kph ) )\n");
    const auto def = VehicleDefinition::read(folder, "v.eng", game, false);
    if (!def->cab3DView || def->cab3DView->controls().size() != 8) {
        test.check(false, "the loco's 3D cab's .cvf read");
        return;
    }
    Sim::LocoScripts lua(scripts);
    std::unique_ptr<Sim::VehicleModel> m = lua.make(def);
    Sim::VehicleInputs in;
    in.dt = 0.01;
    in.speed = 10.0;
    in.cab = true;
    in.lineVoltage = 3000.0;
    Sim::VehicleOutputs out;
    m->step(in, out);
    m->command({"direction", 1.0});
    m->command({"throttle", 0.5});
    m->command({"pantograph1", QString("up")});
    for (int i = 0; i < 5; ++i)
        m->step(in, out);
    std::vector<double> v;
    const bool given = m->cabValues(*def->cab3DView, 10.0, v);
    const double reservoir = Sim::Message{QString(), out.values.value("mainReservoir")}.number();
    const bool key = Sim::Message{QString(), out.values.value("masterKey")}.text() == "true";
    const auto near = [](double a, double b) { return std::fabs(a - b) < 1e-6; };
    const auto at = [&](size_t i) { return i < v.size() ? v[i] : -1.0; };
    test.check(given && v.size() == 8 && near(v[0], 36.0) && near(v[1], 10.0 / 0.44704) && near(v[2], 0.5)
                       && v[3] == 2.0 && near(v[4], reservoir / 100000.0) && v[5] == 1.0 && v[6] == (key ? 1.0 : 0.0)
                       && std::isnan(v[7]),
               QString("the cab's values: %1 km/h, %2 mph, throttle %3, direction %4 (forward), main reservoir %5 "
                       "bar, pantographs %6 (the first raised), master key %7; no rule for a cab radio: %8")
                       .arg(at(0)).arg(at(1)).arg(at(2)).arg(at(3)).arg(at(4)).arg(at(5)).arg(at(6)).arg(at(7)));
    // A type added by a script sharing the module.
    const int adding = lua.lua().loadText("require(\"tsre.cabview\").types.CAB_RADIO = function(v) return 1 end\n",
                                          "adding");
    std::vector<double> added;
    m->cabValues(*def->cab3DView, 10.0, added);
    test.check(adding >= 0 && added.size() == 8 && added[7] == 1.0,
               "a type added to tsre.cabview's table by a script counts");

    // The mouse's actions as the loco's commands ("cab.<control>").
    const auto valueOf = [&](const char *name) { return Sim::Message{QString(), out.values.value(name)}; };
    const bool keyBefore = valueOf("masterKey").text() == "true";
    m->command({"cab.2", 0.8});
    m->command({"cab.5", 2.0});
    for (int i = 0; i < 5; ++i)
        m->step(in, out);
    const double throttle = valueOf("throttle").number();
    const bool second = valueOf("pantograph2").text() == "true";
    m->command({"cab.2", 0.0});
    m->step(in, out);
    m->command({"cab.3", QString("-")});
    m->command({"cab.6", QString("press")});
    m->command({"cab.7", QString("press")});
    m->command({"cab.99", QString("press")});
    m->step(in, out);
    const double direction = valueOf("direction").number();
    const bool keyAfter = valueOf("masterKey").text() == "true";
    test.check(std::fabs(throttle - 0.8) < 1e-9 && second && direction == 0.0,
               QString("the mouse's actions as commands: the throttle dragged to %1, the pantograph switch to both "
                       "(the second up: %2), the direction a step back to %3; the master key clicked: %4 to %5")
                       .arg(throttle).arg(second).arg(direction).arg(keyBefore).arg(keyAfter));
}

// A ray against parts placed by matrices: the nearest met, its texture
// coordinate, the spheres missed, a part left out.
void checkPicking(TokenTest::Suite &test) {
    CabPicker picker;
    // A unit triangle in z = 0 for two parts, the second placed 1 m nearer.
    const auto part = [](int index, int control) {
        CabPicker::Part p;
        p.part = index;
        p.control = control;
        p.points = {0, 0, 0, 1, 0, 0, 0, 1, 0};
        p.uvs = {0, 0, 1, 0, 0, 1};
        return p;
    };
    picker.add(part(4, 10));
    picker.add(part(7, 11));
    bool seven = true;
    const auto place = [&](int index, float *m) {
        QMatrix4x4 t;
        if (index == 7) {
            if (!seven)
                return false;
            t.translate(0, 0, 1);
        }
        std::copy(t.constData(), t.constData() + 16, m);
        return true;
    };
    const float origin[3] = {0.25f, 0.25f, 5.0f}, toward[3] = {0.0f, 0.0f, -1.0f};
    const CabPicker::Hit near = picker.pick(origin, toward, place);
    seven = false;
    const CabPicker::Hit behind = picker.pick(origin, toward, place);
    const float aside[3] = {3.0f, 3.0f, 5.0f};
    const CabPicker::Hit miss = picker.pick(aside, toward, place);
    test.check(near.control == 11 && near.part == 7 && std::fabs(near.distance - 4.0f) < 1e-5f
                       && std::fabs(near.uv[0] - 0.25f) < 1e-5f && std::fabs(near.uv[1] - 0.25f) < 1e-5f
                       && behind.control == 10 && std::fabs(behind.distance - 5.0f) < 1e-5f && miss.control == -1,
               QString("a ray: the nearer part's control %1 at %2 m, its texture at (%3, %4); that part left out, "
                       "the one behind at %5 m; past them nothing")
                       .arg(near.control).arg(near.distance).arg(near.uv[0]).arg(near.uv[1]).arg(behind.distance));
}

}

int TsreTests::runCabViewSuite(bool verbose) {
    TokenTest::Suite test{"[tests:cabview]", verbose};
    QTemporaryDir dir;
    test.check(dir.isValid(), "a temporary game folder");
    checkControls(test, dir.path());
    checkFinding(test, dir.path());
    checkParts(test, dir.path());
    checkFrames(test, dir.path());
    checkValues(test, dir.path());
    checkPicking(test);
    return test.finish();
}

int TsreTests::runCabViewCorpusSuite(bool verbose) {
    TokenTest::Suite test{"[tests:cabview-corpus]", verbose};
    const QString root = QDir::cleanPath(Game::root);
    QDirIterator it(root + "/TRAINS/TRAINSET", {"*.eng", "*.ENG"}, QDir::Files, QDirIterator::Subdirectories);
    int engines = 0, withView = 0, withParts = 0, enginesWithParts = 0, problems = 0;
    QSet<QString> cabs;
    QStringList lines;
    while (it.hasNext()) {
        const QString file = it.next();
        const auto def = VehicleDefinition::read(QFileInfo(file).path(), QFileInfo(file).fileName(), root, false);
        if (!def->cab3D)
            continue;
        ++engines;
        if (def->cab3DView && def->cab3DView->ok())
            ++withView;
        const QString key = def->cab3DShapePath + "|" + def->cab3DViewPath;
        if (def->cab3DShapePath.isEmpty())
            continue;
        SFileComplex shape(def->cab3DShapePath, QFileInfo(def->cab3DShapePath).fileName(),
                           QFileInfo(def->cab3DShapePath).path());
        if (!shape.loadData())
            continue;
        QStringList names;
        std::vector<float> last;
        for (int i = 0; i < shape.partCount(); ++i) {
            names << shape.partName(i);
            last.push_back(shape.partLastFrame(i));
        }
        const CabParts parts = CabParts::match(def->cab3DView.get(), names, last);
        if (parts.controls.empty())
            continue;
        ++enginesWithParts;
        if (cabs.contains(key))
            continue;
        cabs.insert(key);
        ++withParts;
        int keyed = 0, partsMatched = 0;
        QStringList short_;
        for (const CabParts::Control &c : parts.controls) {
            partsMatched += int(c.parts.size());
            keyed += c.lastFrame >= 0 ? 1 : 0;
            // Frames the .cvf can pick that the part has no keys for.
            const CabView::Control &control = def->cab3DView->controls()[size_t(c.control)];
            if (control.tableFrames > 0 && c.lastFrame >= 0 && control.tableFrames - 1 > c.lastFrame)
                short_ << QString("%1:%2 %3>%4").arg(control.type).arg(control.order).arg(control.tableFrames - 1).arg(c.lastFrame);
        }
        problems += def->cab3DView ? def->cab3DView->problems().size() : 0;
        lines << QString("%1: %2 controls (%3 parts, %4 keyed) of %5, %6 outside, unmatched %7")
                         .arg(QDir(root).relativeFilePath(def->cab3DShapePath))
                         .arg(parts.controls.size())
                         .arg(partsMatched)
                         .arg(keyed)
                         .arg(def->cab3DView ? int(def->cab3DView->controls().size()) : 0)
                         .arg(parts.outside.size())
                         .arg(parts.unmatched.isEmpty() ? "-" : parts.unmatched.join(" "))
                 + (short_.isEmpty() ? QString() : "; frames past the keys " + short_.join(" "));
        // Its parts for the mouse, and a ray from the driver's head (its
        // first viewpoint; x the other way, as the shape's root mirrors it)
        // to each interactive control's first part.
        QElapsedTimer made;
        made.start();
        const std::shared_ptr<const CabPicker> picker = CabPicker::make(def->cab3DShapePath, *def->cab3DView, parts);
        const double madeMs = made.nsecsElapsed() / 1e6;
        int aimed = 0, met = 0, triangles = 0;
        QStringList missed;
        if (picker != nullptr && !def->cab3D->viewpoints.isEmpty()) {
            const unsigned state = shape.newState();
            const auto place = [&](int part, float *m) { return shape.partMatrix(state, part, m); };
            const float *head = def->cab3D->viewpoints[0].position;
            const QVector3D eye(-head[0], head[1], head[2]);
            for (const CabPicker::Part &p : picker->parts())
                triangles += int(p.points.size() / 9);
            QSet<int> done;
            for (const CabPicker::Part &p : picker->parts()) {
                if (done.contains(p.control))
                    continue;
                done.insert(p.control);
                QMatrix4x4 m;
                if (!place(p.part, m.data()))
                    continue;
                const QVector3D target = m.map(QVector3D(p.center[0], p.center[1], p.center[2]));
                const QVector3D way = target - eye;
                const float origin[3] = {eye.x(), eye.y(), eye.z()}, toward[3] = {way.x(), way.y(), way.z()};
                ++aimed;
                const CabPicker::Hit hit = picker->pick(origin, toward, place);
                if (hit.control == p.control)
                    ++met;
                else
                    missed << def->cab3DView->controls()[size_t(p.control)].type;
            }
        }
        // A part posed at a frame is its parent as posed times its own
        // matrix at that frame (how a dragged lever's path is found).
        int composed = 0, composedWrong = 0;
        if (picker != nullptr) {
            const unsigned state = shape.newState();
            for (const CabPicker::Part &p : picker->parts()) {
                const int up = shape.partParent(p.part);
                const float last = shape.partLastFrame(p.part);
                if (up < 0 || last <= 0.0f)
                    continue;
                for (float f : {0.0f, last * 0.4f, last}) {
                    shape.setPartFrame(state, p.part, f);
                    QMatrix4x4 posed, parent, own;
                    if (!shape.partMatrix(state, p.part, posed.data()) || !shape.partMatrix(state, up, parent.data())
                            || !shape.partAtFrame(p.part, f, own.data()))
                        continue;
                    ++composed;
                    const QMatrix4x4 product = parent * own;
                    bool same = true;
                    for (int k = 0; k < 16; ++k)
                        same = same && std::fabs(product.constData()[k] - posed.constData()[k]) < 1e-4f;
                    composedWrong += same ? 0 : 1;
                }
            }
        }
        lines.back() += QString("; composed %1 of %2 as posed").arg(composed - composedWrong).arg(composed);
        // The same on the legacy backend (the drawn shapes' by default).
        int legacyComposed = 0, legacyWrong = 0;
        SFileLegacy legacy(def->cab3DShapePath, QFileInfo(def->cab3DShapePath).fileName(),
                           QFileInfo(def->cab3DShapePath).path());
        if (picker != nullptr && legacy.loadData()) {
            const unsigned state = legacy.newState();
            for (const CabPicker::Part &p : picker->parts()) {
                const int up = legacy.partParent(p.part);
                const float last = legacy.partLastFrame(p.part);
                if (up < 0 || last <= 0.0f)
                    continue;
                for (float f : {0.0f, last * 0.4f, last}) {
                    legacy.setPartFrame(state, p.part, f);
                    QMatrix4x4 posed, parent, own;
                    if (!legacy.partMatrix(state, p.part, posed.data()) || !legacy.partMatrix(state, up, parent.data())
                            || !legacy.partAtFrame(p.part, f, own.data()))
                        continue;
                    ++legacyComposed;
                    const QMatrix4x4 product = parent * own;
                    bool same = true;
                    for (int k = 0; k < 16; ++k)
                        same = same && std::fabs(product.constData()[k] - posed.constData()[k]) < 1e-4f;
                    legacyWrong += same ? 0 : 1;
                }
            }
        }
        lines.back() += QString(", legacy %1 of %2").arg(legacyComposed - legacyWrong).arg(legacyComposed);
        lines.back() += QString("; mouse: %1 parts, %2 triangles, read in %3 ms; from the head %4 of %5 controls met "
                                "first%6")
                                .arg(picker ? int(picker->parts().size()) : 0)
                                .arg(triangles)
                                .arg(madeMs, 0, 'f', 0)
                                .arg(met)
                                .arg(aimed)
                                .arg(missed.isEmpty() ? QString() : " (not: " + missed.join(" ") + ")");
    }
    lines.sort();
    for (const QString &line : lines)
        qInfo().noquote() << "[tests:cabview-corpus]" << line;
    test.check(engines > 0, QString("%1 ENGs with a 3D cab, %2 with its .cvf read; %3 cabs (%4 ENGs) with parts "
                                    "matched to controls; %5 problems in their .cvf files")
                                    .arg(engines)
                                    .arg(withView)
                                    .arg(withParts)
                                    .arg(enginesWithParts)
                                    .arg(problems));
    return test.finish();
}
