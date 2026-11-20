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
#include <routeEditor/LoadWindow.h>
#include <routeEditor/RouteEditorGLWidget.h>
#include <settings/SettingsAccess.h>
#include <settings/SettingsManager.h>
#include <tsre/Game.h>
#include <tsre/sound/SoundSystem.h>
#include <tsre/ui/UiScreen.h>

#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QRegularExpression>
#include <QScreen>
#include <QShortcut>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#include <QtGui/qpa/qplatformwindow_p.h>
#endif

namespace Explore {

Window::Window(const QString &route) : route(route) {
    setWindowTitle(Game::AppName);
    setAttribute(Qt::WA_DeleteOnClose, false);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    glView = new RouteEditorGLWidget(this, RouteEditorGLWidget::Role::Explore);
    layout->addWidget(glView);
    // The menus: made with the view's UI, each frame before it is drawn.
    glView->setUiFrame([this](Ui::Screen &screen) {
        if (!mainMenu)
            mainMenu = std::make_unique<Menu>(screen, [this] { exit(); });
        mainMenu->frame();
    });
    for (const QKeySequence &keys : {QKeySequence(Qt::Key_F11), QKeySequence(Qt::ALT | Qt::Key_Return),
                                     QKeySequence(Qt::ALT | Qt::Key_Enter)}) {
        QShortcut *shortcut = new QShortcut(keys, this);
        shortcut->setContext(Qt::WindowShortcut);
        connect(shortcut, &QShortcut::activated, this, [this] { setFillScreen(!filling); });
    }
    // The settings apply while it runs (read when they change, not each
    // frame).
    connect(&SettingsManager::instance(), &SettingsManager::runtimeSettingsChanged, this,
            [this](const QStringList &keys) {
        if (keys.contains("core.explore.fillScreen"))
            filling = Settings::boolean("core.explore.fillScreen");
        if (keys.contains("core.explore.fillScreen") || keys.contains("core.explore.resolution"))
            applySize();
    });
}

Window::~Window() {
    // The menus go before the view's UI screen.
    mainMenu.reset();
    if (glView != nullptr)
        glView->setUiFrame(nullptr);
}

QString Window::startRoot() {
    if (Game::checkRoot(Game::root))
        return Game::root;
    const QString recent = LoadWindow::recentRoot();
    return Game::checkRoot(recent) ? recent : QString();
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
