/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// People at stations (tasks humans 05 and 06), the parts that need no
// simulation: humans.dat read (with and without MSTS's header line), the
// people setting's modes (Always, Route defined, Never), and doors split
// from door parts (leaves as flat quads, whose corners lie apart). The
// people themselves are checked on routes (docs/tasks/humans/05 and 06).

#include "StationPeopleTestSuite.h"
#include "TokenTestSupport.h"
#include <tsre/humans/CarDoors.h>
#include <tsre/humans/HumanSet.h>
#include <tsre/trains/CarAnimation.h>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>

namespace {

void writeFile(const QString &path, const QByteArray &data) {
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(data);
}

// A folder laid out as a route: humans.dat, SHAPES with the files named.
void makeFolder(const QString &folder, const QByteArray &humansDat, const QStringList &shapes) {
    writeFile(folder + "/humans.dat", humansDat);
    for (const QString &name : shapes)
        writeFile(folder + "/SHAPES/" + name, "glTF");
}

// A door leaf: a flat quad across the car's side (x), its corners at z0 and
// z1, from y0 to y1.
void addLeaf(std::vector<float> &points, float x, float y0, float y1, float z0, float z1) {
    const float quad[6][3] = {{x, y0, z0}, {x, y0, z1}, {x, y1, z1}, {x, y0, z0}, {x, y1, z1}, {x, y1, z0}};
    for (const auto &p : quad)
        points.insert(points.end(), p, p + 3);
}

} // namespace

namespace TsreTests {

int runStationPeopleSuite(bool verbose) {
    TokenTest::Suite test{"[tests:station-people]", verbose};
    QTemporaryDir dir;
    if (!dir.isValid()) {
        test.check(false, "a temporary folder");
        return test.finish();
    }
    const QString route = dir.path() + "/ROUTE", tsre = dir.path() + "/TSRE", bare = dir.path() + "/BARE";
    const QString assets = dir.path() + "/ASSETS", empty = dir.path() + "/EMPTY";
    // The route's: MSTS's header, one human missing, a clips file.
    makeFolder(route,
               "SIMISA@@@@@@@@@@JINX0h0t______\r\n\r\nHumanList (\r\n\tName ( \"Passengers\" )\r\n"
               "\tAnimations ( \"clips.glb\" )\r\n\tHuman ( Name ( \"woman.glb\" ) Weight ( 2 ) )\r\n\tHuman ( Name ( \"man.glb\" ) )\r\n"
               "\tHuman ( Name ( \"gone.glb\" ) Weight ( 1 ) )\r\n\tHuman ( Weight ( 3 ) )\r\n)\r\nHumanList (\r\n\tName ( \"Staff\" )\r\n\tHuman ( Name ( \"guard.glb\" ) Height ( 1.7 1.9 ) Weight ( 1 ) )\r\n)\r\n",
               {"clips.glb", "woman.glb", "man.glb", "guard.glb"});
    // TSRE's: no header line.
    makeFolder(tsre, "HumanList (\n\tName ( \"Default\" )\n\tHuman ( Name ( \"young.glb\" ) )\n)\n", {"young.glb"});
    makeFolder(assets, "HumanList (\n\tName ( \"Full\" )\n\tHuman ( Name ( \"man.glb\" ) )\n)\n", {"man.glb"});
    // A humans.dat whose humans are all missing: as none.
    makeFolder(empty, "HumanList (\n\tName ( \"Gone\" )\n\tHuman ( Name ( \"gone.glb\" ) )\n)\n", {});
    QDir().mkpath(bare);

    Humans::HumanSet set;
    test.check(Humans::HumanSet::read(route + "/humans.dat", route, set) && set.lists.size() == 2,
               QString("humans.dat with its header: two lists (%1)").arg(set.lists.size()));
    if (set.lists.size() == 2) {
        const Humans::HumanSet::List &first = set.lists[0];
        test.check(first.name == "Passengers" && first.humans.size() == 2 && first.humans[0].weight == 2.0f
                   && first.humans[1].weight == 1.0f && first.humans[0].path.endsWith("SHAPES/woman.glb")
                   && first.clips.endsWith("SHAPES/clips.glb"),
                   "the first list: its name, its humans and how often (1 when not given), its clips file, in SHAPES");
        test.check(set.problems.size() == 2 && set.problems[0].contains("gone.glb") && set.problems[1].contains("without"),
                   QString("a human not there, a human without a name: told and left out (%1)").arg(set.problems.join("; ")));
        test.check(set.lists[1].humans.size() == 1, "a field not known yet (Height) is skipped");
        test.check(set.textures.endsWith("ROUTE/TEXTURES"), "textures from the folder's TEXTURES");
    }
    Humans::HumanSet plain;
    test.check(Humans::HumanSet::read(tsre + "/humans.dat", tsre, plain) && plain.lists.size() == 1
               && plain.lists[0].humans.size() == 1 && plain.lists[0].clips.isEmpty(),
               "humans.dat without a header line: read from its first line");

    using Humans::HumanSet;
    test.check(HumanSet::forRoute(route, HumanSet::Always, assets, tsre).source == HumanSet::Source::Route
               && HumanSet::forRoute(bare, HumanSet::Always, assets, tsre).source == HumanSet::Source::Assets
               && HumanSet::forRoute(bare, HumanSet::Always, bare, tsre).source == HumanSet::Source::Basic,
               "Always: the route's own humans, else the assets' set, else appdata's basic set");
    const HumanSet gone = HumanSet::forRoute(empty, HumanSet::Always, assets, tsre);
    test.check(gone.source == HumanSet::Source::Assets && !gone.problems.isEmpty(),
               "a route's humans.dat with nobody there: the assets' set, the missing files told");
    test.check(HumanSet::forRoute(route, HumanSet::RouteDefined, assets, tsre).source == HumanSet::Source::Route
               && HumanSet::forRoute(bare, HumanSet::RouteDefined, assets, tsre).empty(),
               "Route defined: the route's own humans, else nobody");
    test.check(HumanSet::forRoute(route, HumanSet::Never, assets, tsre).empty()
               && HumanSet::forRoute(bare, HumanSet::Never, assets, tsre).empty(),
               "Never: nobody");

    // Doors: an EN57's side (two doors of two leaves each, 0.65 m a leaf,
    // the floor at 1.14 m), a narrow step piece and a long strip.
    std::vector<float> points;
    for (const float z : {-4.53f, 2.17f}) {
        addLeaf(points, 1.36f, 1.14f, 3.0f, z, z + 0.65f);
        addLeaf(points, 1.36f, 1.14f, 3.0f, z + 0.65f, z + 1.30f);
    }
    std::vector<Humans::CarDoors::Door> doors = Humans::CarDoors::split(points, CarAnimation::DoorLeft);
    test.check(doors.size() == 2 && std::fabs(doors[0].z + 3.88f) < 0.01f && std::fabs(doors[0].width - 1.30f) < 0.01f
               && std::fabs(doors[1].z - 2.82f) < 0.01f && doors[0].bottom == 1.14f && std::fabs(doors[0].x - 1.36f) < 0.01f
               && doors[0].group == CarAnimation::DoorLeft,
               QString("two leaves whose corners lie 0.65 m apart make one door (%1 doors, the first at z %2, %3 m wide)")
               .arg(doors.size()).arg(doors.empty() ? 0.0 : double(doors[0].z), 0, 'f', 2)
               .arg(doors.empty() ? 0.0 : double(doors[0].width), 0, 'f', 2));
    std::vector<float> odd;
    addLeaf(odd, 1.36f, 1.0f, 1.1f, 0.0f, 0.2f);
    addLeaf(odd, 1.36f, 1.0f, 1.1f, 5.0f, 11.0f);
    test.check(Humans::CarDoors::split(odd, CarAnimation::DoorRight).empty(),
               "pieces narrower than 0.4 m or wider than 4.5 m are not doors");
    // A MaSzyna model's doors (TSRE_MASZYNA_ROOT, MaSzyna's folder): the
    // EN57's middle car (6bsiv_led2.e3d, its .mmd's prefix door0: door01..door08):
    // two a side, 1.5 m wide, odd ones on its left (+x in MaSzyna's space).
    const QString maszyna = qEnvironmentVariable("TSRE_MASZYNA_ROOT");
    const QString en57 = maszyna + "/dynamic/pkp/en57_v1/6bsiv_led2.e3d";
    if (!maszyna.isEmpty() && QFileInfo::exists(en57)) {
        const std::shared_ptr<const Humans::CarDoors> read = Humans::CarDoors::readNow(en57, QString(), "door0", 8);
        int left = 0, right = 0;
        bool sized = read != nullptr;
        if (read)
            for (const Humans::CarDoors::Door &door : read->doors) {
                left += door.group == CarAnimation::DoorLeft && door.x > 1.0f ? 1 : 0;
                right += door.group == CarAnimation::DoorRight && door.x < -1.0f ? 1 : 0;
                sized = sized && std::fabs(door.width - 1.5f) < 0.1f && door.bottom > 0.9f && door.bottom < 1.3f;
            }
        test.check(left == 2 && right == 2 && sized,
                   QString("the MaSzyna EN57's middle car: %1 doors left, %2 right, 1.5 m wide at the floor")
                   .arg(left).arg(right));
    } else {
        qInfo() << "[tests:station-people] set TSRE_MASZYNA_ROOT to MaSzyna's folder for its e3d doors";
    }
    return test.finish();
}

} // namespace TsreTests
