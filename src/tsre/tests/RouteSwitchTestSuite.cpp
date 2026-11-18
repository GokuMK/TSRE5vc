/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// Task editor 10: the editor's window on route A, then on route B, back on
// A, A again, and a route that does not load. Two routes made for the test
// (RouteCreator's, at different start tiles), A with an object and a level
// crossing in its world file.

#include <tsre/tests/RouteSwitchTestSuite.h>

#include <routeEditor/RouteEditorGLWidget.h>
#include <routeEditor/RouteEditorWindow.h>
#include <tsre/ErrorMessage.h>
#include <tsre/ErrorMessagesLib.h>
#include <tsre/Game.h>
#include <tsre/Undo.h>
#include <tsre/sound/SoundSource.h>
#include <tsre/sound/SoundSystem.h>
#include <tsre/traffic/LevelCrossings.h>
#include <tsre/trains/ActLib.h>
#include <tsre/world/Route.h>
#include <tsre/world/RouteCreator.h>
#include <tsre/world/TerrainLib.h>
#include <tsre/world/Tile.h>
#include <tsre/world/Trk.h>
#include <tsre/world/objects/WorldObj.h>

#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <cmath>
#include <functional>

namespace {

const QString RouteA = QStringLiteral("SWITCHA");
const QString RouteB = QStringLiteral("SWITCHB");
// The route template's start tile (x, file z), and B's.
const int StartAX = -5000, StartAZ = 15000;
const int StartBX = -4990, StartBZ = 14990;

void settle(int ms) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

bool writeUtf16(const QString &path, const QString &text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf16);
    out.setGenerateByteOrderMark(true);
    out << text;
    out.flush();
    return file.error() == QFile::NoError;
}

QString readUtf16(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf16);
    return in.readAll();
}

const char *GlobalTSection =
        "SIMISA@@@@@@@@@@JINX0F0t______\n\n"
        "TrackSections ( 2\n"
        "\tTrackSection ( 1\n\t\tSectionSize ( 1.5 10 )\n\t)\n"
        ")\n"
        "TrackShapes ( 2\n"
        "\tTrackShape ( 1\n\t\tFileName ( SwitchTestStraight.s )\n\t\tNumPaths ( 1 )\n"
        "\t\tSectionIdx ( 1 0 0 0 0 1 )\n\t)\n"
        ")\n";

// A's world file with a static object (UiD 1) and a level crossing (UiD 2).
bool addObjectsToA(const QString &root) {
    QDirIterator it(root + "/ROUTES/" + RouteA + "/WORLD", {"*.w"});
    if (!it.hasNext())
        return false;
    const QString path = it.next();
    QString text = readUtf16(path);
    const int end = text.lastIndexOf(')');
    if (end < 0)
        return false;
    const QString objects =
            "\tStatic (\n\t\tUiD ( 1 )\n\t\tFileName ( SwitchTest.s )\n\t\tStaticFlags ( 00000000 )\n"
            "\t\tPosition ( 10 1 20 )\n\t\tQDirection ( 0 0 0 1 )\n\t\tVDbId ( 0 )\n\t)\n"
            "\tLevelCr (\n\t\tUiD ( 2 )\n\t\tLevelCrParameters ( 0 0 )\n\t\tCrashProbability ( 0 )\n"
            "\t\tLevelCrData ( 00000000 0 )\n\t\tLevelCrTiming ( 60 60 30 )\n\t\tFileName ( SwitchTestCrossing.s )\n"
            "\t\tPosition ( 30 1 40 )\n\t\tQDirection ( 0 0 0 1 )\n\t\tVDbId ( 0 )\n\t)\n";
    text.insert(end, objects);
    return writeUtf16(path, text);
}

WorldObj *objectOf(int tileX, int fileZ, unsigned int uid) {
    if (Game::currentRoute == nullptr)
        return nullptr;
    Tile *tile = Game::currentRoute->requestTile(tileX, -fileZ, false);
    if (tile == nullptr || tile->loaded != 1)
        return nullptr;
    return tile->findObject(uid, WorldObj::sstatic);
}

// The ObjTools list that holds the "signals" entry: how many there are.
int signalsEntries(QWidget *window) {
    int count = 0;
    for (QComboBox *box : window->findChildren<QComboBox *>())
        for (int i = 0; i < box->count(); i++)
            if (box->itemData(i).toString() == "signals")
                count++;
    return count;
}

}

int TsreTests::runRouteSwitchSuite(bool verbose) {
    int passed = 0;
    int failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) {
            ++passed;
            if (verbose) qInfo().noquote() << "[tests:route-switch] PASS" << name;
        } else {
            ++failed;
            qWarning().noquote() << "[tests:route-switch] FAIL" << name;
        }
    };
    const auto finish = [&] {
        qInfo() << "[tests:route-switch] cases=" << passed + failed << "passed=" << passed << "failed=" << failed;
        return failed ? 1 : 0;
    };
    const QString rootWas = Game::root;
    const QString routeWas = Game::route;
    const bool writeWas = Game::writeEnabled;

    QTemporaryDir workspace;
    const QString root = workspace.filePath("root");
    bool made = workspace.isValid() && RouteCreator::templateResourcesAvailable();
    if (made) {
        QDir().mkpath(root + "/ROUTES");
        QDir().mkpath(root + "/GLOBAL");
        made = writeUtf16(root + "/GLOBAL/tsection.dat", QString::fromLatin1(GlobalTSection));
        Game::root = root;
        Game::writeEnabled = true;
        QString error;
        Game::route = RouteA;
        made = made && RouteCreator::create(RouteA, Trk::createNewRouteTemplate(RouteA), &error);
        std::unique_ptr<Trk> templateB = Trk::createNewRouteTemplate(RouteB);
        templateB->startTileX = StartBX;
        templateB->startTileZ = StartBZ;
        Game::route = RouteB;
        made = made && RouteCreator::create(RouteB, std::move(templateB), &error);
        if (!error.isEmpty())
            qWarning() << "[tests:route-switch] routes:" << error;
        made = made && addObjectsToA(root);
    }
    check(made, "two routes made for the test (needs assets/templateRoute_0.6)");
    if (!made) {
        Game::root = rootWas;
        Game::route = routeWas;
        Game::writeEnabled = writeWas;
        return finish();
    }

    // Route A in the editor's window.
    Game::route = RouteA;
    Game::checkRoute(RouteA);
    RouteEditorWindow *window = new RouteEditorWindow();
    window->showRoute();
    settle(1500);
    RouteEditorGLWidget *view = window->findChild<RouteEditorGLWidget *>();
    Route *routeA = Game::currentRoute;
    check(view != nullptr && routeA != nullptr && routeA->loaded && Game::route == RouteA, "route A loaded");
    WorldObj *placed = objectOf(StartAX, StartAZ, 1);
    check(placed != nullptr && LevelCrossings::objects().size() == 1, "A's object and level crossing there");
    check(signalsEntries(window) == 1, "the object lists filled once");
    if (view == nullptr || placed == nullptr) {
        Game::root = rootWas;
        Game::route = routeWas;
        Game::writeEnabled = writeWas;
        return finish();
    }

    // What A leaves behind: a selection, an unsaved move, undo, messages, a
    // sound.
    view->objectSelected(placed);
    const float filePosition = placed->position[0];
    Undo::StateBegin();
    Undo::PushWorldObjData(placed);
    placed->translate(5, 0, 0);
    placed->setModified();
    Undo::StateEnd();
    ErrorMessage *trackMessage = new ErrorMessage(ErrorMessage::Type_Warning, ErrorMessage::Source_TDB, "A's track");
    ErrorMessagesLib::PushErrorMessage(trackMessage);
    ErrorMessage *editorMessage = new ErrorMessage(ErrorMessage::Type_Info, ErrorMessage::Source_Editor, "a session note");
    editorMessage->setObject(placed);
    ErrorMessagesLib::PushErrorMessage(editorMessage);
    SoundSource *sound = new SoundSource(root + "/none.sms", SoundSource::Kind::World);
    check(Game::currentSelectedGameObj == placed && SoundSystem::sourceCount() >= 1, "A's selection and a sound");
    TDB *tdbA = Game::trackDB;
    TerrainLib *terrainA = Game::terrainLib;

    // To B.
    const bool toB = window->switchRoute(RouteB, false);
    settle(800);
    check(toB && Game::route == RouteB && Game::currentRoute != routeA && Game::currentRoute->loaded
                  && Game::trackDB != tdbA && Game::terrainLib != terrainA && !view->changingRoute(),
          "B loaded: its route, track database and terrain");
    int tileX = 0, tileZ = 0;
    float pos[3], yaw = 0, pitch = 0;
    view->diagnosticView(tileX, tileZ, pos, yaw, pitch);
    check(tileX == StartBX && tileZ == -StartBZ, "the camera at B's start");
    check(Game::currentSelectedGameObj == nullptr, "nothing selected");
    check(!Undo::UndoLast(), "undo empty");
    check(!ErrorMessagesLib::ErrorMessages.contains(trackMessage) && ErrorMessagesLib::ErrorMessages.contains(editorMessage)
                  && editorMessage->obj == nullptr,
          "A's track message gone; the session's kept, without A's object");
    check(SoundSystem::sourceCount() == 0 && LevelCrossings::objects().empty(), "no sound, no level crossing of A");
    check(ActLib::Act.isEmpty() && ActLib::Paths.isEmpty() && ActLib::route.isEmpty(), "A's activities and paths forgotten");
    check(signalsEntries(window) == 1, "the object lists B's, nothing twice");
    delete sound;

    // Back to A, its unsaved move not saved: as its files have it.
    const bool toA = window->switchRoute(RouteA, false);
    settle(800);
    WorldObj *again = objectOf(StartAX, StartAZ, 1);
    check(toA && Game::route == RouteA && again != nullptr && again != placed
                  && std::fabs(again->position[0] - filePosition) < 0.01f && LevelCrossings::objects().size() == 1,
          "back on A: the object as its file has it, its crossing again");

    // A again: the camera stays.
    view->setDiagnosticView(StartAX, -StartAZ, 100.0f, 50.0f, 200.0f, 0.3f, 0.1f);
    int beforeX = 0, beforeZ = 0;
    float before[3];
    view->diagnosticView(beforeX, beforeZ, before, yaw, pitch);
    const bool reloaded = window->switchRoute(RouteA, false);
    settle(800);
    view->diagnosticView(tileX, tileZ, pos, yaw, pitch);
    check(reloaded && tileX == beforeX && tileZ == beforeZ && std::fabs(pos[0] - before[0]) < 0.01f
                  && std::fabs(pos[2] - before[2]) < 0.01f && signalsEntries(window) == 1,
          "the same route again: the camera where it was, the lists once");

    // A route that does not load: A back.
    Route *beforeBroken = Game::currentRoute;
    const bool broken = window->switchRoute(QStringLiteral("NOSUCHROUTE"), false);
    settle(500);
    check(!broken && Game::route == RouteA && Game::currentRoute != nullptr && Game::currentRoute != beforeBroken
                  && Game::currentRoute->loaded && !view->changingRoute(),
          "a route that does not load: the previous one loaded again");

    window->hide();
    Game::root = rootWas;
    Game::route = routeWas;
    Game::writeEnabled = writeWas;
    return finish();
}
