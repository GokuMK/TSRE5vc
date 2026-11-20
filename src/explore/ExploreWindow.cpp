/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <explore/ExploreWindow.h>
#include <explore/ExploreMenu.h>
#include <explore/ExplorePanel.h>
#include <explore/ExploreRun.h>
#include <routeEditor/LoadWindow.h>
#include <routeEditor/RouteEditorGLWidget.h>
#include <settings/SettingsAccess.h>
#include <settings/SettingsManager.h>
#include <tsre/Game.h>
#include <tsre/sim/SimulationController.h>
#include <tsre/sound/SoundSystem.h>
#include <tsre/ui/UiScreen.h>

#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QRegularExpression>
#include <QScreen>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#include <QtGui/qpa/qplatformwindow_p.h>
#endif

namespace Explore {

// The editor's view aids (its View menu's): track database and section
// lines, its markers of interactive objects, grids, the pointer, the
// compass, pivot points, track items. A player sees the world without them
// (task explore 02).
struct Window::ViewAids {
    bool trackDbLines = Game::viewTrackDbLines, tsectionLines = Game::viewTsectionLines,
         interactives = Game::viewInteractives, markers = Game::viewMarkers, snapable = Game::viewSnapable,
         compass = Game::viewCompass, pointer = Game::viewPointer3d, walkAreas = Game::viewWalkAreas,
         worldGrid = Game::viewWorldGrid, tileGrid = Game::viewTileGrid, terrainGrid = Game::viewTerrainGrid,
         terrainShape = Game::viewTerrainShape, pivots = Game::showWorldObjPivotPoints,
         trackItems = Game::renderTrItems;

    static void hide() {
        Game::viewTrackDbLines = Game::viewTsectionLines = Game::viewInteractives = Game::viewMarkers = false;
        Game::viewSnapable = Game::viewCompass = Game::viewPointer3d = Game::viewWalkAreas = false;
        Game::viewWorldGrid = Game::viewTileGrid = Game::viewTerrainGrid = false;
        Game::showWorldObjPivotPoints = Game::renderTrItems = false;
        Game::viewTerrainShape = true;
    }
    void restore() const {
        Game::viewTrackDbLines = trackDbLines;
        Game::viewTsectionLines = tsectionLines;
        Game::viewInteractives = interactives;
        Game::viewMarkers = markers;
        Game::viewSnapable = snapable;
        Game::viewCompass = compass;
        Game::viewPointer3d = pointer;
        Game::viewWalkAreas = walkAreas;
        Game::viewWorldGrid = worldGrid;
        Game::viewTileGrid = tileGrid;
        Game::viewTerrainGrid = terrainGrid;
        Game::viewTerrainShape = terrainShape;
        Game::showWorldObjPivotPoints = pivots;
        Game::renderTrItems = trackItems;
    }
};

Window::Window(const QString &route) : route(route), aidsWere(new ViewAids) {
    ViewAids::hide();
    setWindowTitle(Game::AppName);
    setAttribute(Qt::WA_DeleteOnClose, false);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    glView = new RouteEditorGLWidget(this, RouteEditorGLWidget::Role::Explore);
    layout->addWidget(glView);
    // A run chosen in the Explore window: the menu hidden while it is on,
    // back after it (with why, when it did not start).
    runs = std::make_unique<Run>(*glView, [this](const QString &why) {
        if (!mainMenu)
            return;
        mainMenu->setShown(true);
        if (!why.isEmpty() && mainMenu->panel() != nullptr)
            mainMenu->panel()->setStatus(why);
    }, [this] { exit(); });
    // The menus: made with the view's UI, each frame before it is drawn.
    glView->setUiFrame([this](Ui::Screen &screen) {
        if (!mainMenu)
            mainMenu = std::make_unique<Menu>(screen, Game::root, this->route, [this](const RunChoice &choice) {
                mainMenu->setShown(false);
                runs->start(choice);
            }, [this] { exit(); });
        mainMenu->frame();
        runs->frame(screen);
    });
    // The settings apply while it runs (read when they change, not each
    // frame): explore mode's Options, later.
    connect(&SettingsManager::instance(), &SettingsManager::runtimeSettingsChanged, this,
            [this](const QStringList &keys) {
        if (keys.contains("core.explore.fillScreen"))
            filling = Settings::boolean("core.explore.fillScreen");
        if (keys.contains("core.explore.fillScreen") || keys.contains("core.explore.resolution"))
            applySize();
    });
}

Window::~Window() {
    // The menus and the run's documents go before the view's UI screen.
    runs.reset();
    mainMenu.reset();
    if (glView != nullptr)
        glView->setUiFrame(nullptr);
    aidsWere->restore();
}

QString Window::startRoot() {
    // An empty one is not checked: it would log a missing folder.
    if (!Game::root.isEmpty() && Game::checkRoot(Game::root))
        return Game::root;
    const QString recent = LoadWindow::recentRoot();
    return !recent.isEmpty() && Game::checkRoot(recent) ? recent : QString();
}

QSize Window::resolution(const QString &value) {
    static const QRegularExpression form("^\\s*(\\d+)\\s*x\\s*(\\d+)\\s*$");
    const QRegularExpressionMatch match = form.match(value);
    if (!match.hasMatch())
        return QSize();
    const QSize size(match.captured(1).toInt(), match.captured(2).toInt());
    return size.width() > 0 && size.height() > 0 ? size : QSize();
}

QSize Window::logicalSize(const QSize &pixels, qreal ratio, const QSize &available) {
    if (ratio <= 0.0)
        ratio = 1.0;
    QSize size(int(std::lround(pixels.width() / ratio)), int(std::lround(pixels.height() / ratio)));
    // Larger than the screen: the largest of its shape that fits.
    if (available.isValid() && (size.width() > available.width() || size.height() > available.height()))
        size = size.scaled(available, Qt::KeepAspectRatio);
    return size.expandedTo(QSize(320, 240));
}

void Window::open() {
    filling = Settings::boolean("core.explore.fillScreen");
    applySize();
}

void Window::setFillScreen(bool on) {
    filling = on;
    applySize();
}

void Window::applySize() {
    QScreen *on = screen() != nullptr ? screen() : QGuiApplication::primaryScreen();
    if (filling) {
        // Qt's full screen: no frame, the taskbar covered.
        setWindowState(windowState() | Qt::WindowFullScreen);
        show();
#ifdef Q_OS_WIN
        // main.cpp gives every window a 1-pixel border in full screen, for
        // the editor's menus and tool windows over its view (task editor 09).
        // Nothing Qt draws goes over this one: none.
        if (QWindow *handle = windowHandle())
            if (auto *windows = handle->nativeInterface<QNativeInterface::Private::QWindowsWindow>())
                windows->setHasBorderInFullScreen(false);
#endif
        return;
    }
    QSize pixels = resolution(Settings::variant("core.explore.resolution", SettingType::Enum).toString());
    if (!pixels.isValid())
        pixels = QSize(1280, 720);
    const QRect available = on != nullptr ? on->availableGeometry() : QRect(0, 0, 1280, 720);
    const QSize size = logicalSize(pixels, on != nullptr ? on->devicePixelRatio() : 1.0, available.size());
    setWindowState(windowState() & ~(Qt::WindowFullScreen | Qt::WindowMaximized));
    resize(size);
    move(available.center() - QPoint(size.width() / 2, size.height() / 2));
    show();
}

void Window::exit() {
    if (exiting)
        return;
    if (exitHandler) {
        exitHandler();
        return;
    }
    exiting = true;
    Sim::SimulationController::instance()->stop();
    hide();
    SoundSystem::shutdown();
    qApp->quit();
}

void Window::closeEvent(QCloseEvent *event) {
    if (exitHandler) {
        QWidget::closeEvent(event);
        return;
    }
    exit();
    event->accept();
}

}
