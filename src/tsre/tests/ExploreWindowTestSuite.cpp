/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// Task explore 01: explore mode's window, its view's Explore role and its
// main menu, without a route; the Load window's Explore and the game root
// --explore starts with.

#include <tsre/tests/ExploreWindowTestSuite.h>

#include <explore/ExploreMenu.h>
#include <explore/ExploreWindow.h>
#include <routeEditor/LoadWindow.h>
#include <routeEditor/RouteEditorGLWidget.h>
#include <settings/SettingsManager.h>
#include <tsre/Game.h>
#include <tsre/map/TrackItemMapLayer.h>

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QPushButton>
#include <QScreen>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>

namespace {

void settle(int ms) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

// Waits until `done` or `ms` pass.
bool settleUntil(const std::function<bool()> &done, int ms) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        if (done())
            return true;
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return done();
}

void key(QWidget *view, int key) {
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(view, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QApplication::sendEvent(view, &release);
    settle(120);
}

// A game root as Game::checkRoot wants one.
bool makeRoot(const QString &path) {
    if (!QDir().mkpath(path + "/ROUTES") || !QDir().mkpath(path + "/GLOBAL"))
        return false;
    QFile tsection(path + "/GLOBAL/tsection.dat");
    return tsection.open(QIODevice::WriteOnly) && tsection.write("SIMISA@@@@@@@@@@JINX0F0t______\n") > 0;
}

bool writeLines(const QString &path, const QStringList &lines) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream out(&file);
    for (const QString &line : lines)
        out << line << "\n";
    return true;
}

QStringList readLines(const QString &path) {
    QFile file(path);
    QStringList lines;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return lines;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (!line.isEmpty())
            lines << line;
    }
    return lines;
}

// Bright pixels (text) in a part of the image, as parts of its width.
int brightPixels(const QImage &image, double from, double to) {
    int count = 0;
    for (int y = 0; y < image.height(); y += 2)
        for (int x = int(image.width() * from); x < int(image.width() * to); x += 2)
            if (qGray(image.pixel(x, y)) > 150)
                ++count;
    return count;
}

}

int TsreTests::runExploreWindowSuite(bool verbose) {
    int passed = 0;
    int failed = 0;
    const auto check = [&](bool value, const QString &name) {
        if (value) {
            ++passed;
            if (verbose) qInfo().noquote() << "[tests:explore-window] PASS" << name;
        } else {
            ++failed;
            qWarning().noquote() << "[tests:explore-window] FAIL" << name;
        }
    };
    const QString rootWas = Game::root;
    const QString workingWas = QDir::currentPath();
    SettingsManager &settings = SettingsManager::instance();

    // The game root --explore starts with, and the Load window's Explore, in
    // a folder of their own (the Load window keeps its roots in the working
    // folder's cerecent.txt).
    QTemporaryDir workspace;
    const QString rootA = QDir(workspace.path()).filePath("rootA");
    const QString rootB = QDir(workspace.path()).filePath("rootB");
    check(workspace.isValid() && makeRoot(rootA) && makeRoot(rootB), "two game roots made");
    QDir::setCurrent(workspace.path());
    writeLines("cerecent.txt", {rootA, rootB});
    check(LoadWindow::recentRoot() == rootB, "the most recent root is the list's last");
    Game::root.clear();
    check(Explore::Window::startRoot() == rootB, "no root of the launch or the settings: the most recent");
    Game::root = rootA;
    check(Explore::Window::startRoot() == rootA, "the launch's or the settings' root first");
    Game::root = workspace.filePath("none");
    writeLines("cerecent.txt", {workspace.filePath("gone")});
    check(Explore::Window::startRoot().isEmpty(), "no root at all: none (the Load window opens)");

    writeLines("cerecent.txt", {rootA, rootB});
    Game::root = rootA;
    {
        LoadWindow load;
        QStringList order;
        if (QHBoxLayout *row = load.findChild<QHBoxLayout *>()) {
            for (int i = 0; i < row->count(); ++i)
                if (QPushButton *button = qobject_cast<QPushButton *>(row->itemAt(i)->widget()))
                    order << button->text();
        }
        QPushButton *explore = nullptr;
        for (QPushButton *button : load.findChildren<QPushButton *>())
            if (button->text() == qtTrId("route.editor.load.window.button.explore"))
                explore = button;
        check(order.size() == 4 && order.at(0) == qtTrId("route.editor.load.window.button.explore")
                      && order.at(1) == qtTrId("route.editor.load.window.button.edit"),
              "the Load window: Explore left of Edit (was Load) (" + order.join(", ") + ")");
        check(explore != nullptr && !explore->isHidden(), "Explore shown with a game root");
        QString asked = "unset";
        QObject::connect(&load, &LoadWindow::showExplore, [&asked](const QString &route) { asked = route; });
        load.exploreLoad();
        check(asked.isEmpty(), "Explore needs no route chosen");
        load.handleBrowseButton(rootA);
        check(readLines("cerecent.txt") == QStringList({rootB, rootA}) && LoadWindow::recentRoot() == rootA,
              "a root chosen again goes last, the most recent");
    }
    QDir::setCurrent(workingWas);
    Game::root = rootWas;

    // The window: 1024 x 768, no route.
    settings.setSessionValue("core.explore.resolution", "1024x768");
    settings.setSessionValue("core.explore.fillScreen", false);
    Explore::Window *window = new Explore::Window("SOMEROUTE");
    bool exited = false;
    window->setExitHandler([&exited] { exited = true; });
    window->open();
    RouteEditorGLWidget *view = window->view();
    QScreen *screen = window->screen();
    const QSize expected = Explore::Window::logicalSize(QSize(1024, 768), screen->devicePixelRatio(),
                                                        screen->availableGeometry().size());
    settleUntil([window] { return window->menu() != nullptr; }, 5000);
    check(window->size() == expected, QString("the window 1024 x 768 screen pixels (%1 x %2, expected %3 x %4)")
              .arg(window->width()).arg(window->height()).arg(expected.width()).arg(expected.height()));
    check(window->chosenRoute() == "SOMEROUTE", "the route to choose first kept");

    // The Explore role.
    check(view != nullptr && view->exploring(), "the view in its Explore role");
    view->enableTool("selectTool");
    check(view->chosenTool().isEmpty(), "no editing tool");
    const MapLayers &layers = view->shownMapLayers();
    check(layers.shows(MapLayer::Track) && layers.shows(MapLayer::TrackObjects) && layers.shows(MapLayer::Activity)
              && !layers.shows(MapLayer::Junctions) && !layers.shows(MapLayer::Ends)
              && !layers.shows(MapLayer::Pointer) && !layers.shows(MapLayer::Paths)
              && MapLayers::has(layers.trackObjectKinds, TrackItemMapLayer::Signal)
              && !MapLayers::has(layers.trackObjectKinds, TrackItemMapLayer::SoundRegion)
              && MapLayers::has(layers.activityKinds, int(MapActivityKind::TrainNames))
              && !MapLayers::has(layers.activityKinds, int(MapActivityKind::Events)),
          "the map's fewer layers");

    // The main menu over black.
    Explore::Menu *menu = window->menu();
    check(menu != nullptr && menu->usable(), "the main menu loaded");
    if (menu == nullptr || !menu->usable()) {
        delete window;
        settings.clearSessionValue("core.explore.resolution");
        settings.clearSessionValue("core.explore.fillScreen");
        qInfo() << "[tests:explore-window] cases=" << passed + failed << "passed=" << passed << "failed=" << failed;
        return 1;
    }
    settle(400);
    check(menu->page() == Explore::Menu::Page::Main && menu->focused() == "explore",
          "the main menu shown, Explore chosen (" + menu->focused() + ")");
    const QImage frame = view->grabFramebuffer();
    // TSRE_EXPLORE_SHOT=<file.png>: the frame kept, to look at.
    if (qEnvironmentVariableIsSet("TSRE_EXPLORE_SHOT") && !frame.isNull())
        frame.save(qEnvironmentVariable("TSRE_EXPLORE_SHOT"));
    const int left = frame.isNull() ? 0 : brightPixels(frame, 0.0, 0.5);
    const int right = frame.isNull() ? 0 : brightPixels(frame, 0.6, 1.0);
    check(!frame.isNull() && left > 50 && right < 20,
          QString("a frame drawn without a route: the menu on the left, black on the right (%1, %2 bright)")
              .arg(left).arg(right));

    // The keys: Multiplayer and Options are not built, the arrows pass them.
    key(view, Qt::Key_Down);
    check(menu->focused() == "exit", "Down: Exit (" + menu->focused() + ")");
    key(view, Qt::Key_Up);
    check(menu->focused() == "explore", "Up: Explore (" + menu->focused() + ")");
    key(view, Qt::Key_Return);
    check(menu->page() == Explore::Menu::Page::Explore && menu->focused() == "back",
          "Enter: the Explore window, Back chosen (" + menu->focused() + ")");
    key(view, Qt::Key_Escape);
    check(menu->page() == Explore::Menu::Page::Main, "Esc: back to the main menu");
    key(view, Qt::Key_Escape);
    settle(200);
    check(menu->page() == Explore::Menu::Page::Main && !menu->focused().isEmpty(),
          "Esc in the main menu: it stays (" + menu->focused() + ")");
    check(menu->click("explore") && menu->page() == Explore::Menu::Page::Explore, "a click: the Explore window");
    check(menu->click("back") && menu->page() == Explore::Menu::Page::Main, "Back: the main menu");
    check(menu->click("options") && menu->page() == Explore::Menu::Page::Main && !exited,
          "Options does nothing yet");
    key(view, Qt::Key_Down);
    key(view, Qt::Key_Return);
    settleUntil([&exited] { return exited; }, 1000);
    check(exited, "Exit quits");

    // Fill screen, and the resolution changed while it runs.
    window->setFillScreen(true);
    settle(300);
    check(window->windowState().testFlag(Qt::WindowFullScreen), "Fill screen: full screen");
    window->setFillScreen(false);
    settle(300);
    check(!window->windowState().testFlag(Qt::WindowFullScreen) && window->size() == expected,
          "Fill screen off: the window again");
    settings.setSessionValue("core.explore.resolution", "640x480");
    settle(300);
    const QSize small = Explore::Window::logicalSize(QSize(640, 480), screen->devicePixelRatio(),
                                                     screen->availableGeometry().size());
    check(window->size() == small, "the resolution changed: the window follows");
    settings.setSessionValue("core.explore.fillScreen", true);
    settle(300);
    check(window->fillsScreen() && window->windowState().testFlag(Qt::WindowFullScreen),
          "the Fill screen setting changed: full screen");

    delete window;
    settings.clearSessionValue("core.explore.resolution");
    settings.clearSessionValue("core.explore.fillScreen");
    Game::exploreMode = false;
    settle(100);

    qInfo() << "[tests:explore-window] cases=" << passed + failed << "passed=" << passed << "failed=" << failed;
    return failed ? 1 : 0;
}
