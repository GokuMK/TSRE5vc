/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include "MenuRevealBar.h"

#include <QApplication>
#include <QCursor>
#include <QEasingCurve>
#include <QMenuBar>
#include <QResizeEvent>

namespace {
// The mouse within this many pixels of the screen's top row reveals the bar.
// In full screen the editor's 1-pixel border lies there too (main.cpp).
const int TopEdge = 2;
// How far under the bar the mouse may go before it counts as gone.
const int Margin = 8;
const int PollMs = 50;
const int SlideMs = 120;
// How long the mouse stays away before the bar slides up.
const int AwayMs = 400;
}

MenuRevealBar::MenuRevealBar(QWidget *editor, const QList<QAction*> &menus)
    : QWidget(editor, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint
              | Qt::WindowDoesNotAcceptFocus),
      editor(editor) {
    // Clicking a menu leaves the editor the active window (its shortcuts).
    setAttribute(Qt::WA_ShowWithoutActivating);
    bar = new QMenuBar(this);
    bar->setNativeMenuBar(false);
    bar->addActions(menus);

    animation.setDuration(SlideMs);
    animation.setEasingCurve(QEasingCurve::OutCubic);
    connect(&animation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) { place(value.toDouble()); });
    connect(&animation, &QVariantAnimation::finished, this, [this] {
        if (!revealed)
            hide();
    });
    connect(&timer, &QTimer::timeout, this, &MenuRevealBar::poll);
    timer.start(PollMs);
}

int MenuRevealBar::barHeight() const {
    return bar->sizeHint().height();
}

void MenuRevealBar::place(double fraction) {
    shownFraction = fraction;
    const QRect area = editor->geometry();
    setGeometry(area.left(), area.top(), area.width(),
                qMax(1, qRound(barHeight() * fraction)));
}

void MenuRevealBar::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    bar->setGeometry(0, height() - barHeight(), width(), barHeight());
}

void MenuRevealBar::slide(bool in) {
    revealed = in;
    away.invalidate();
    animation.stop();
    animation.setStartValue(shownFraction);
    animation.setEndValue(in ? 1.0 : 0.0);
    if (in) {
        if (!isVisible()) {
            place(shownFraction);
            show();
        }
        raise();
    }
    animation.start();
}

void MenuRevealBar::poll() {
    // Nothing before the editor shows (the window mode is set before), or
    // while another application is in front.
    if (!editor->isVisible()
            || QGuiApplication::applicationState() != Qt::ApplicationActive)
        return;
    const QPoint cursor = QCursor::pos();
    const QRect area = editor->frameGeometry();
    const bool atTop = cursor.x() >= area.left() && cursor.x() <= area.right()
            && cursor.y() <= area.top() + TopEdge;
    if (!revealed) {
        // Not while a button is held: a drag in the view reaching the edge.
        if (atTop && QGuiApplication::mouseButtons() == Qt::NoButton)
            slide(true);
        return;
    }
    // It stays while the mouse is over it or just under it, or a menu is open.
    if (atTop || geometry().adjusted(0, 0, 0, Margin).contains(cursor)
            || QApplication::activePopupWidget() || bar->activeAction()) {
        away.invalidate();
        return;
    }
    if (!away.isValid())
        away.start();
    else if (away.elapsed() >= AwayMs)
        slide(false);
}
