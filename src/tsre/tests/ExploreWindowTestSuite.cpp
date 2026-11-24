/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// Task explore 01: explore mode's window, its view's Explore role and its
// main menu, without a route; the Load window's Explore and the game root
// --explore starts with. Task explore 02: the menu and its windows as tabs.

#include <tsre/tests/ExploreWindowTestSuite.h>

#include <explore/ExploreMenu.h>
#include <explore/ExplorePanel.h>
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
#include <cmath>

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

void key(QWidget *view, int key, const QString &text = QString()) {
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
    QApplication::sendEvent(view, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
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

// A route with only its TRK (the Explore window lists it).
bool makeRoute(const QString &root, const QString &name, const QString &shown) {
    const QString folder = root + "/ROUTES/" + name;
    if (!QDir().mkpath(folder))
        return false;
    QFile trk(folder + "/" + name + ".trk");
    if (!trk.open(QIODevice::WriteOnly))
        return false;
    const QString text = QString("SIMISA@@@@@@@@@@JINX0r1t______\r\n\r\nTr_RouteFile (\r\n\tRouteID ( %1 )\r\n"
                                 "\tName ( \"%2\" )\r\n\tFileName ( %1 )\r\n)\r\n").arg(name, shown);
    QByteArray bytes("\xff\xfe", 2);
    for (const QChar c : text) {
        bytes.append(char(c.unicode() & 0xff));
        bytes.append(char(c.unicode() >> 8));
    }
    return trk.write(bytes) == bytes.size();
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

    // The window: 1024 x 768, no route; a game root of two routes.
    check(makeRoute(rootA, "R1", "Route One") && makeRoute(rootA, "R2", "Route Two"), "two routes made");
    Game::root = rootA;
    Explore::Panel::setRememberedFile(workspace.filePath("explore.json"));
    // 16:9: the window beside the menu.
    settings.setSessionValue("core.explore.resolution", "1280x720");
    settings.setSessionValue("core.explore.fillScreen", false);
    Explore::Window *window = new Explore::Window("SOMEROUTE");
    bool exited = false;
    window->setExitHandler([&exited] { exited = true; });
    window->open();
    RouteEditorGLWidget *view = window->view();
    QScreen *screen = window->screen();
    const QSize expected = Explore::Window::logicalSize(QSize(1280, 720), screen->devicePixelRatio(),
                                                        screen->availableGeometry().size());
    settleUntil([window] { return window->menu() != nullptr; }, 5000);
    check(window->size() == expected, QString("the window 1280 x 720 screen pixels (%1 x %2, expected %3 x %4)")
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
        Game::root = rootWas;
        Explore::Panel::setRememberedFile(QString());
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
    // The Explore window as a tab: the menu stays beside it.
    key(view, Qt::Key_Return);
    settleUntil([menu] { return menu->focused() == "routes:item"; }, 1000);
    check(menu->page() == Explore::Menu::Page::Explore && menu->focused() == "routes:item",
          "Enter: the Explore window, the keys on its first route (" + menu->focused() + ")");
    check(menu->panel() != nullptr && menu->panel()->routeNames() == QStringList({"R1", "R2"}),
          "the game root's routes listed, by name");
    key(view, Qt::Key_Left);
    check(menu->page() == Explore::Menu::Page::Explore && menu->focused() == "explore",
          "Left from a route: back to the menu's item, the window still shown (" + menu->focused() + ")");
    key(view, Qt::Key_Right);
    check(menu->focused() == "routes:item", "Right on the marked item: into its window (" + menu->focused() + ")");
    // The route page (task explore 05): Enter on a route goes to its pages'
    // buttons; a route of only a TRK has no activities and no timetables,
    // their buttons greyed and passed.
    key(view, Qt::Key_Return);
    settleUntil([menu] { return menu->focused() == "open-explore"; }, 1000);
    check(menu->focused() == "open-explore" && menu->panel() != nullptr && menu->panel()->page() == "route",
          "Enter on a route: its pages' buttons, Explore the route (" + menu->focused() + ")");
    check(menu->panel() != nullptr && menu->panel()->text("route-counts").contains("0"),
          "the route's counts shown (" + (menu->panel() != nullptr ? menu->panel()->text("route-counts") : QString()) + ")");
    key(view, Qt::Key_Left);
    check(menu->focused() == "routes:item", "Left past the greyed buttons: the routes (" + menu->focused() + ")");
    key(view, Qt::Key_Right);
    check(menu->focused() == "open-explore", "Right: the buttons again (" + menu->focused() + ")");
    // Explore the route's page: no paths, no consists, the keys on its bar.
    key(view, Qt::Key_Return);
    settleUntil([menu] { return menu->focused() == "mode-drive"; }, 1000);
    check(menu->panel() != nullptr && menu->panel()->page() == "explore" && menu->focused() == "mode-drive",
          "Enter on Explore the route: its page, no path to choose: the bar's Drive (" + menu->focused() + ")");
    check(menu->panel() != nullptr && !menu->panel()->ready(), "no path, no consist: Start greyed");
    // Drive or Walk (task explore 04): Right Walk, Enter walks; Left and
    // Enter drive again.
    key(view, Qt::Key_Right);
    check(menu->focused() == "mode-walk", "Right: Walk (" + menu->focused() + ")");
    key(view, Qt::Key_Return);
    check(menu->panel() != nullptr && menu->panel()->choice().walk, "Enter on Walk: on foot");
    key(view, Qt::Key_Left);
    key(view, Qt::Key_Return);
    check(menu->focused() == "mode-drive" && menu->panel() != nullptr && !menu->panel()->choice().walk,
          "Left, Enter: Drive again (" + menu->focused() + ")");
    key(view, Qt::Key_Down);
    check(menu->focused() == "pick-day", "Down: the bar's second row, the day (" + menu->focused() + ")");
    key(view, Qt::Key_Right);
    check(menu->focused() == "pick-month", "Right: the month (" + menu->focused() + ")");
    key(view, Qt::Key_Return);
    settleUntil([menu] { return menu->panel() != nullptr && menu->panel()->picking(); }, 1000);
    check(menu->panel() != nullptr && menu->panel()->picking(), "Enter: the month's picker");
    key(view, Qt::Key_Escape);
    settleUntil([menu] { return menu->focused() == "pick-month"; }, 1000);
    check(menu->panel() != nullptr && !menu->panel()->picking() && menu->focused() == "pick-month",
          "Esc: the picker closed, the month again (" + menu->focused() + ")");
    // A filter takes the letters typed though the menu has the keys (task
    // explore 05; the screen gave text only to a field outside a menu).
    check(menu->click("path-filter"), "a click on the paths' filter");
    key(view, Qt::Key_A, "a");
    key(view, Qt::Key_B, "b");
    check(menu->focused() == "path-filter" && menu->panel() != nullptr && menu->panel()->text("path-filter") == "ab",
          "typed into it: '" + (menu->panel() != nullptr ? menu->panel()->text("path-filter") : QString()) + "'");
    // Esc: back to the route page, its button; Left from the first column
    // too.
    key(view, Qt::Key_Escape);
    settleUntil([menu] { return menu->focused() == "open-explore"; }, 1000);
    check(menu->panel() != nullptr && menu->panel()->page() == "route" && menu->focused() == "open-explore",
          "Esc on a page: the route page, Explore the route's button (" + menu->focused() + ", "
                  + (menu->panel() != nullptr ? menu->panel()->page() : QString()) + ")");
    key(view, Qt::Key_Return);
    settleUntil([menu] { return menu->focused() == "mode-drive"; }, 1000);
    key(view, Qt::Key_Left);
    settleUntil([menu] { return menu->focused() == "open-explore"; }, 1000);
    check(menu->panel() != nullptr && menu->panel()->page() == "route" && menu->focused() == "open-explore",
          "Left from a page's first column: the route page (" + menu->focused() + ")");
    // Out to the menu and in again: the button of the page last used.
    key(view, Qt::Key_Escape);
    check(menu->focused() == "explore", "Esc on the route page: the menu's item (" + menu->focused() + ")");
    key(view, Qt::Key_Right);
    settleUntil([menu] { return menu->focused() == "open-explore"; }, 1000);
    check(menu->focused() == "open-explore", "Right: in again, on the page last used (" + menu->focused() + ")");
    key(view, Qt::Key_Left);
    check(menu->focused() == "routes:item", "Left: the routes (" + menu->focused() + ")");
    key(view, Qt::Key_Escape);
    check(menu->page() == Explore::Menu::Page::Explore && menu->focused() == "explore",
          "Esc in the window: back to the item, the window still shown (" + menu->focused() + ")");
    key(view, Qt::Key_Escape);
    settle(200);
    check(menu->focused() == "explore", "Esc in the main menu: it stays (" + menu->focused() + ")");
    key(view, Qt::Key_Down);
    check(menu->focused() == "exit", "Down from Explore with the window shown: Exit (" + menu->focused() + ")");
    key(view, Qt::Key_Up);

    // Narrow and wide views (task explore 05): the window at most 1450 dp
    // wide (its 16:9 width), centred right of the menu; at 3:2 and narrower
    // over the menu, hidden while the keys are on the menu. In dp: the view
    // is 1080 tall; 8 dp spare for a small view's pixels. The window sized
    // as its frame dragged would (the resolutions offered are 4:3 and 16:9),
    // small enough for the offscreen screen (800 x 600).
    {
        const auto viewAt = [window](int width, int height) {
            window->resize(width, height);
            settle(400);
        };
        const auto near = [](double a, double b) { return std::fabs(a - b) < 8.0; };
        const QRectF items = menu->box("items");
        QRectF canvas = menu->box(QString());
        QRectF box = menu->box("explore-window");
        const auto boxText = [&canvas, &box] {
            return QString("the view %1 dp wide, the window %2 to %3").arg(canvas.width(), 0, 'f', 0)
                    .arg(box.left(), 0, 'f', 0).arg(box.right(), 0, 'f', 0);
        };
        const auto measure = [&] {
            canvas = menu->box(QString());
            box = menu->box("explore-window");
        };
        check(items.right() > 300 && items.right() < 430, QString("the menu's items end at %1 dp").arg(items.right()));
        check(!menu->covering() && menu->windowShown() && near(box.left(), 430) && near(box.right(), canvas.width() - 40)
                      && !menu->shows("to-menu") && menu->shows("items"),
              "16:9: the window beside the menu, no Main menu button (" + boxText() + ")");
        viewAt(784, 336);
        measure();
        const double leftGap = box.left() - 430, rightGap = canvas.width() - 40 - box.right();
        check(!menu->covering() && menu->windowShown() && box.width() < 1458 && box.width() > 1440 && leftGap > 250
                      && near(leftGap, rightGap),
              "21:9: at most 1450 dp wide, centred right of the menu (" + boxText() + ")");
        viewAt(768, 480);
        measure();
        check(!menu->covering() && menu->windowShown() && near(box.left(), 430) && near(box.right(), canvas.width() - 40),
              "16:10: beside the menu (" + boxText() + ")");
        // 4:3, the keys on the menu: the window over it is hidden.
        viewAt(640, 480);
        check(menu->covering() && !menu->windowShown() && menu->focused() == "explore",
              "4:3 with the keys on the menu: the window hidden (" + menu->focused() + ")");
        key(view, Qt::Key_Right);
        settleUntil([menu] { return menu->focused() == "open-explore"; }, 1000);
        settle(100);
        measure();
        check(menu->windowShown() && menu->focused() == "open-explore" && near(box.left(), 40)
                      && near(box.right(), canvas.width() - 40) && box.left() < items.left() && menu->shows("to-menu"),
              "Right: the window over the menu, the Main menu button (" + menu->focused() + ", " + boxText() + ")");
        check(!menu->shows("items"), "the menu's items under it hidden");
        key(view, Qt::Key_Escape);
        check(menu->focused() == "explore" && !menu->windowShown() && menu->shows("items"),
              "Esc on the route page: the menu, the window hidden (" + menu->focused() + ")");
        key(view, Qt::Key_Right);
        settleUntil([menu] { return menu->focused() == "open-explore"; }, 1000);
        key(view, Qt::Key_Left);
        check(menu->windowShown() && menu->focused() == "routes:item",
              "Right, Left: the routes, the window shown (" + menu->focused() + ")");
        key(view, Qt::Key_Left);
        check(menu->focused() == "explore" && !menu->windowShown(),
              "Left from the routes: the menu, the window hidden (" + menu->focused() + ")");
        check(menu->click("explore") && menu->windowShown(), "a click on Explore: the window again");
        settleUntil([menu] { return menu->focused() != "explore"; }, 1000);
        check(menu->click("to-menu") && menu->focused() == "explore" && !menu->windowShown(),
              "a click on Main menu: the menu, the window hidden (" + menu->focused() + ")");
        // Made wider with the window hidden: beside the menu again.
        viewAt(expected.width(), expected.height());
        check(!menu->covering() && menu->windowShown() && menu->focused() == "explore",
              "16:9 again: the window shown beside the menu, the keys on it (" + menu->focused() + ")");
        key(view, Qt::Key_Right);
        settleUntil([menu] { return menu->focused() == "open-explore"; }, 1000);
        viewAt(600, 480);
        measure();
        check(menu->covering() && menu->windowShown() && near(box.left(), 40) && near(box.right(), canvas.width() - 40),
              "5:4 with the keys in the window: over the menu, shown (" + boxText() + ")");
        viewAt(720, 480);
        measure();
        const double gap = box.left() - 40;
        check(menu->covering() && menu->windowShown() && box.width() < 1458 && box.width() > 1440 && gap > 30
                      && near(gap, canvas.width() - 40 - box.right()),
              "3:2: over the menu, at most 1450 dp wide, centred (" + boxText() + ")");
        key(view, Qt::Key_Escape);
        viewAt(expected.width(), expected.height());
        check(!menu->covering() && menu->windowShown() && menu->focused() == "explore",
              "back to 16:9 (" + menu->focused() + ")");
    }
    menu->open(Explore::Menu::Page::Main);
    check(menu->click("explore") && menu->page() == Explore::Menu::Page::Explore, "a click: the Explore window");
    check(menu->click("options") && menu->page() == Explore::Menu::Page::Explore && !exited,
          "Options does nothing yet");
    menu->open(Explore::Menu::Page::Main);
    settle(200);
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
    Game::root = rootWas;
    Explore::Panel::setRememberedFile(QString());
    settle(100);

    qInfo() << "[tests:explore-window] cases=" << passed + failed << "passed=" << passed << "failed=" << failed;
    return failed ? 1 : 0;
}
