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
#include <tsre/shape/ShapeMetadata.h>
#include <tsre/shape/ShapeSeats.h>
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
               "\tAnimations ( \"clips.glb\" )\r\n\tHuman ( Name ( \"woman.glb\" ) Weight ( 2 ) Temperature ( 26 8 ) )\r\n\tHuman ( Name ( \"man.glb\" ) )\r\n"
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
        const Humans::HumanSet::Human &woman = first.humans[0], &man = first.humans[1];
        test.check(woman.lowest == 8.0f && woman.highest == 26.0f && woman.outside(30.0f) == 4.0f
                   && woman.outside(0.0f) == 8.0f && woman.outside(20.0f) == 0.0f
                   && man.outside(-40.0f) == 0.0f && man.outside(45.0f) == 0.0f,
                   "a human's Temperature: the air its clothes are for (either order), how far outside; "
                   "without one any weather");
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
    // Both sides' leaves under one part (EN57-787's), a roof's hatch over a
    // door and a saloon's sliding door (task humans 11): two doors a side,
    // each by where it is, 1.5 m wide (the hatch not joined to its door).
    std::vector<float> both;
    for (const float x : {1.38f, -1.38f})
        for (const float z : {-4.5f, 3.7f})
            addLeaf(both, x, 1.12f, 3.0f, z, z + 1.5f);
    addLeaf(both, 0.6f, 4.0f, 4.1f, 2.9f, 5.7f);
    addLeaf(both, 1.1f, 0.4f, 2.4f, -1.0f, 1.6f);
    const std::vector<Humans::CarDoors::Door> sided = Humans::CarDoors::splitSides(both);
    int sidedLeft = 0, sidedRight = 0;
    bool sidedWide = true;
    for (const Humans::CarDoors::Door &door : sided) {
        sidedLeft += door.group == CarAnimation::DoorLeft && door.x > 1.3f ? 1 : 0;
        sidedRight += door.group == CarAnimation::DoorRight && door.x < -1.3f ? 1 : 0;
        sidedWide = sidedWide && std::fabs(door.width - 1.5f) < 0.01f;
    }
    test.check(sided.size() == 4 && sidedLeft == 2 && sidedRight == 2 && sidedWide,
               QString("one part with both sides' leaves, a roof's hatch and an inner door: %1 doors, %2 left at +x, "
                       "%3 right at -x").arg(sided.size()).arg(sidedLeft).arg(sidedRight));
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
    // An S file's doors (TSRE_OR_PSK_ROOT, OR_PSK_NEW's folder): the newer
    // EN57's middle car, two a side; its left (DOOR_D..F) at +x where it is
    // drawn, as people take the doors of an .sd (StationPeople's doorsOf).
    const QString psk = qEnvironmentVariable("TSRE_OR_PSK_ROOT");
    const QString psk57 = psk + "/TRAINS/TRAINSET/PKP_EN57-OR/PREG_EN57-1018s.s";
    if (!psk.isEmpty() && QFileInfo::exists(psk57)) {
        const std::shared_ptr<const Humans::CarDoors> read = Humans::CarDoors::readNow(psk57, QString());
        int left = 0, right = 0;
        if (read)
            for (const Humans::CarDoors::Door &door : read->doors) {
                left += door.group == CarAnimation::DoorLeft && door.x > 1.2f ? 1 : 0;
                right += door.group == CarAnimation::DoorRight && door.x < -1.2f ? 1 : 0;
            }
        test.check(read && read->doors.size() == 4 && left == 2 && right == 2,
                   QString("OR_PSK_NEW's EN57 middle car: %1 doors, %2 left at +x, %3 right at -x")
                   .arg(read ? int(read->doors.size()) : 0).arg(left).arg(right));
    } else {
        qInfo() << "[tests:station-people] set TSRE_OR_PSK_ROOT to OR_PSK_NEW's folder for an S file's doors";
    }

    // Seats (task humans 09): an .sd's ESD_TSRESeats, as written; an S
    // file's mirrored in x where the car is drawn; a glTF's extras.
    {
        QTemporaryDir seatsDir;
        QFile sd(seatsDir.filePath("car.sd"));
        if (sd.open(QIODevice::WriteOnly))
            sd.write("SIMISA@@@@@@@@@@JINX0t1t______\n\nShape ( car.s\n\tESD_Detail_Level ( 0 )\n"
                     "\tESD_TSRESeats (\n\t\tAisle ( 0.05 1.16 10.6 -10.6 )\n\t\tPassage ( 0.05 1.16 -5.4 0.7 )\n"
                     "\t\tSeat 3 ( 0.64 1.54 -9.77 180 )\n\t\tSeat ( -0.64 1.54 -9.32 0 )\n\t\tSeat ( 1 2 )\n"
                     "\t\tBench ( 1 2 3 4 )\n\t\tDoor 2 ( -1.36 1.14 -4.52 1.3 )\n\t\tDoor ( 1 2 )\n\t)\n)\n");
        sd.close();
        ShapeSeats::Seats seats;
        QStringList problems;
        const bool read = ShapeSeats::readFor(seatsDir.filePath("car.s"), seats, &problems);
        test.check(read && seats.seats.size() == 2 && seats.aisles.size() == 1 && seats.passages.size() == 1
                   && seats.seats[0].heading == 180.0f && seats.seats[1].position[0] == -0.64f
                   && seats.aisles[0].z0 == -10.6f && seats.aisles[0].z1 == 10.6f && seats.passages[0].width == 0.7f,
                   QString("ESD_TSRESeats read: %1 seats, %2 aisles, %3 passages").arg(seats.seats.size())
                   .arg(seats.aisles.size()).arg(seats.passages.size()));
        test.check(problems.size() == 3, QString("a short Seat, a short Door and an unknown line told: %1").arg(problems.join("; ")));
        test.check(read && seats.doors.size() == 1 && seats.doors[0].number == 2 && seats.doors[0].position[0] == -1.36f
                   && seats.doors[0].position[1] == 1.14f && seats.doors[0].position[2] == -4.52f && seats.doors[0].width == 1.3f,
                   QString("a Door line (task humans 11): %1 doors, its number its label").arg(seats.doors.size()));
        test.check(read && seats.seats[0].number == 3 && seats.seats[1].number == 0,
                   "a Seat's label is its number (Seat 3 ( ... )), a Seat without one read too");
        ShapeMetadata metadata;
        ShapeMetadata::read(seatsDir.filePath("car.s"), metadata);
        test.check(metadata.seats.seats.size() == 2, "the seats with the .sd's other blocks (ShapeMetadata)");
        const ShapeSeats::Seats onCar = ShapeSeats::drawn(seats, seatsDir.filePath("car.s"));
        const ShapeSeats::Seats onModel = ShapeSeats::drawn(seats, seatsDir.filePath("car.e3d"));
        test.check(onCar.seats[0].position[0] == -0.64f && onCar.seats[0].heading == -180.0f && onCar.aisles[0].x == -0.05f
                   && onModel.seats[0].position[0] == 0.64f && onModel.aisles[0].x == 0.05f,
                   "an S file's seats mirrored in x where the car is drawn, a MaSzyna model's as written");
        test.check(onCar.doors[0].position[0] == 1.36f && onModel.doors[0].position[0] == -1.36f,
                   "an S file's doors mirrored too: its DOOR_D..F side (-x as written) is the car's left (+x drawn)");

        // Doors without seats: read (a car whose doors are written, its
        // interior not), no seats used.
        QFile doorsOnly(seatsDir.filePath("doors.sd"));
        if (doorsOnly.open(QIODevice::WriteOnly))
            doorsOnly.write("SIMISA@@@@@@@@@@JINX0t1t______\n\nShape ( doors.s\n\tESD_TSRESeats (\n"
                            "\t\tDoor 1 ( 1.36 1.14 4.52 1.3 )\n\t)\n)\n");
        doorsOnly.close();
        problems.clear();
        const bool fromDoors = ShapeSeats::readFor(seatsDir.filePath("doors.s"), seats, &problems);
        test.check(fromDoors && seats.empty() && !seats.none() && seats.doors.size() == 1 && problems.isEmpty(),
                   "an .sd with doors only: read, its doors without seats");

        // Seats without an aisle: none (people could not reach them).
        QFile bare(seatsDir.filePath("bare.sd"));
        if (bare.open(QIODevice::WriteOnly))
            bare.write("SIMISA@@@@@@@@@@JINX0t1t______\n\nShape ( bare.s\n\tESD_TSRESeats (\n"
                       "\t\tSeat ( 0.64 1.54 -9.77 180 )\n\t)\n)\n");
        bare.close();
        problems.clear();
        const bool fromBare = ShapeSeats::readFor(seatsDir.filePath("bare.s"), seats, &problems);
        test.check(!fromBare && problems.size() == 1,
                   "seats without an Aisle: none, told");

        // A glTF's default scene's extras.
        QFile gltf(seatsDir.filePath("car.gltf"));
        if (gltf.open(QIODevice::WriteOnly))
            gltf.write("{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[],\"extras\":"
                       "{\"TSRE_seats\":{\"aisles\":[[0,1.1,-9,9]],\"passages\":[[0,1.1,-5,0.7]],"
                       "\"seats\":[[0.6,1.5,-8,180],[0.6,1.5,-7,0],[1,2]],\"doors\":[[1.4,1.1,-6,1.3]]}}}]}");
        gltf.close();
        problems.clear();
        const bool fromGltf = ShapeSeats::readFor(seatsDir.filePath("car.gltf"), seats, &problems);
        test.check(fromGltf && seats.seats.size() == 2 && seats.seats[1].number == 2
                   && seats.aisles.size() == 1 && seats.passages.size() == 1 && seats.doors.size() == 1
                   && seats.doors[0].number == 1 && problems.size() == 1,
                   QString("TSRE_seats in a glTF scene's extras: %1 seats, numbered by their place").arg(seats.seats.size()));
    }

    // Aisles as routes (task humans 12): numbered aisles, their joins found
    // (overlapping or nearly meeting on one floor, a level change with room
    // for its stairs), the way over them, each seat's aisle and the seats a
    // door reaches by the way's length.
    {
        QTemporaryDir aislesDir;
        QFile sd(aislesDir.filePath("aisles.sd"));
        if (sd.open(QIODevice::WriteOnly))
            sd.write("SIMISA@@@@@@@@@@JINX0t1t______\n\nShape ( aisles.s\n\tESD_TSRESeats (\n"
                     "\t\tAisle 2 ( 0.5 1.2 -10 -1 )\n\t\tAisle ( 0 1.2 -1.5 1.5 )\n"
                     "\t\tSeat 1 ( 1 1.65 -8 0 )\n\t)\n)\n");
        sd.close();
        ShapeSeats::Seats read;
        ShapeSeats::readFor(aislesDir.filePath("aisles.s"), read);
        test.check(read.aisles.size() == 2 && read.aisles[0].number == 2 && read.aisles[1].number == 0,
                   "an Aisle's label is its number (Aisle 2 ( ... )), an Aisle without one read too");

        using ShapeSeats::Aisle;
        const auto near = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
        const auto pointIs = [&](const ShapeSeats::Way &way, size_t i, float x, float y, float z) {
            return way.points.size() >= 3 * (i + 1) && near(way.points[3 * i], x) && near(way.points[3 * i + 1], y)
                    && near(way.points[3 * i + 2], z);
        };

        // Two overlapping aisles join at their overlap's middle; two on one
        // floor more than JoinGap apart do not.
        QVector<Aisle> two{{0.5f, 1.2f, -10.0f, 1.0f}, {-0.5f, 1.2f, -1.0f, 10.0f}};
        std::vector<ShapeSeats::Join> joins = ShapeSeats::joins(two);
        test.check(joins.size() == 1 && joins[0].a == 0 && joins[0].b == 1 && near(joins[0].za, 0.0f) && near(joins[0].zb, 0.0f),
                   QString("overlapping aisles join at the overlap's middle: %1 joins").arg(joins.size()));
        QVector<Aisle> apart{{0.0f, 1.2f, -10.0f, -1.0f}, {0.0f, 1.2f, -1.5f, 10.0f}, {0.0f, 1.2f, 10.4f, 12.0f}};
        joins = ShapeSeats::joins(apart);
        test.check(joins.size() == 1 && joins[0].a == 0 && joins[0].b == 1 && near(joins[0].za, -1.25f)
                   && near(joins[0].zb, -1.25f),
                   QString("overlapping by 0.5 m: joined; 0.4 m apart on one floor: not (%1 joins)").arg(joins.size()));

        // A level change: a low floor and a raised end 0.9 m along (its
        // stairs), joined at their ends; the leg over it rises. A deck 2.5 m
        // above: no join.
        QVector<Aisle> levels{{0.0f, 0.73f, -3.0f, 3.0f}, {0.0f, 1.23f, 3.9f, 11.0f}, {0.0f, 3.23f, -3.0f, 11.0f}};
        joins = ShapeSeats::joins(levels);
        test.check(joins.size() == 1 && joins[0].a == 0 && joins[0].b == 1 && near(joins[0].za, 3.0f) && near(joins[0].zb, 3.9f),
                   QString("a level change joins at the stairs' ends (3 to 3.9), a deck above does not: %1 joins").arg(joins.size()));
        ShapeSeats::Way way;
        const bool up = ShapeSeats::route(levels, joins, 0, 0.0f, 1, 8.0f, &way);
        test.check(up && way.points.size() == 12 && pointIs(way, 0, 0.0f, 0.73f, 0.0f) && pointIs(way, 1, 0.0f, 0.73f, 3.0f)
                   && pointIs(way, 2, 0.0f, 1.23f, 3.9f) && pointIs(way, 3, 0.0f, 1.23f, 8.0f) && near(way.length, 8.0f),
                   QString("the way up: along the low floor, up the stairs' leg, along the raised one (%1 points, %2 m)")
                   .arg(way.points.size() / 3).arg(way.length));
        test.check(!ShapeSeats::route(levels, joins, 0, 0.0f, 2, 0.0f),
                   "no way onto an aisle nothing joins (the deck)");
        // A double-deck car (the Bhp's): its entrance's aisle and its upper
        // deck's 1.28 m above, 2 m along (the middle stairs), joined; the
        // lower deck 2 m below the upper one is not.
        QVector<Aisle> decks{{0.0f, 1.12f, -9.5f, -8.3f}, {0.0f, 2.4f, -6.25f, 3.0f}, {0.0f, 0.4f, -6.0f, 2.6f}};
        joins = ShapeSeats::joins(decks);
        const bool stairs = std::any_of(joins.begin(), joins.end(), [&](const ShapeSeats::Join &j) {
            return j.a == 0 && j.b == 1 && near(j.za, -8.3f) && near(j.zb, -6.25f);
        });
        const bool decksJoined = std::any_of(joins.begin(), joins.end(), [](const ShapeSeats::Join &j) {
            return j.a == 1 && j.b == 2;
        });
        test.check(stairs && !decksJoined,
                   QString("a double-deck car: its entrance joins its upper deck 1.28 m up its stairs (-8.3 to -6.25), "
                           "the decks 2 m apart do not join: %1 joins").arg(joins.size()));

        // A way over three aisles: two saloons at their own x (2+1 seating
        // swapping sides) and a short piece through the vestibule between.
        QVector<Aisle> three{{0.5f, 1.2f, -10.0f, -1.0f}, {0.0f, 1.2f, -1.5f, 1.5f}, {-0.5f, 1.2f, 1.0f, 10.0f}};
        joins = ShapeSeats::joins(three);
        const bool over = ShapeSeats::route(three, joins, 0, -8.0f, 2, 8.0f, &way);
        test.check(joins.size() == 2 && over && way.points.size() == 18 && pointIs(way, 0, 0.5f, 1.2f, -8.0f)
                   && pointIs(way, 1, 0.5f, 1.2f, -1.25f) && pointIs(way, 2, 0.0f, 1.2f, -1.25f)
                   && pointIs(way, 3, 0.0f, 1.2f, 1.25f) && pointIs(way, 4, -0.5f, 1.2f, 1.25f)
                   && pointIs(way, 5, -0.5f, 1.2f, 8.0f) && near(way.length, 17.0f),
                   QString("a way over three aisles: %1 joins, %2 points, %3 m").arg(joins.size())
                   .arg(way.points.size() / 3).arg(way.length));
        const ShapeSeats::Way queue = ShapeSeats::wayShort(way, 1.1f);
        test.check(near(queue.length, 15.9f) && queue.points.size() == 18 && pointIs(queue, 5, -0.5f, 1.2f, 6.9f),
                   QString("a way 1.1 m short (a queue's place): %1 m").arg(queue.length));
        const ShapeSeats::Way start = ShapeSeats::wayStart(way, 7.0f);
        test.check(start.points.size() == 9 && pointIs(start, 2, 0.25f, 1.2f, -1.25f),
                   "a way's first 7 m: to the middle of the first crossing");

        // One aisle: the way as before, straight along it.
        QVector<Aisle> one{{0.1f, 1.2f, -10.0f, 10.0f}};
        const bool along = ShapeSeats::route(one, ShapeSeats::joins(one), 0, -4.0f, 0, 5.0f, &way);
        test.check(along && way.points.size() == 6 && pointIs(way, 0, 0.1f, 1.2f, -4.0f) && pointIs(way, 1, 0.1f, 1.2f, 5.0f)
                   && near(way.length, 9.0f), "one aisle: straight along it, as before");

        // Each seat's aisle: of those holding its z, the floor a cushion's
        // height below it; the seats a door reaches, nearest by the way, the
        // unreachable ones (the deck's) never.
        ShapeSeats::Seats car;
        car.aisles = levels;
        car.seats = {{{0.6f, 1.18f, 2.0f}, 0.0f, 1}, {{0.6f, 1.68f, 5.0f}, 0.0f, 2}, {{0.6f, 1.68f, 3.5f}, 0.0f, 3},
                     {{0.6f, 3.68f, 0.0f}, 0.0f, 4}, {{0.6f, 1.18f, -2.5f}, 0.0f, 5}};
        ShapeSeats::connect(car);
        test.check(car.seatAisles == std::vector<int>({0, 1, 1, 2, 0}),
                   "a seat's aisle: the one whose floor is a cushion's height below it");
        const std::vector<int> taken{-1, -1, -1, -1, 7};
        const std::vector<std::pair<float, int>> byWay = ShapeSeats::seatsByWay(car, 0, 0.0f, &taken);
        QStringList order;
        for (const auto &s : byWay)
            order << QString::number(car.seats[s.second].number);
        test.check(order == QStringList({"1", "3", "2"}),
                   QString("the seats a door on the low floor reaches, nearest by the way: %1 (the deck's 4 and the taken 5 not)")
                   .arg(order.join(" ")));
    }
    return test.finish();
}

} // namespace TsreTests
