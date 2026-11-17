/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MENUREVEALBAR_H
#define MENUREVEALBAR_H

#include <QElapsedTimer>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

class QAction;
class QMenuBar;

// The Route Editor's menu bar in full screen (task editor 09). The editor's
// own bar is hidden there; this one slides down over the top of the editor
// when the mouse touches the screen's top edge, and up again once the mouse
// has left it and no menu is open. It shows the editor's own menus: one QMenu
// can sit in several menu bars.
//
// A frameless tool window owned by the editor, not a child widget: under QRhi
// the 3D view is a native child window, which no child widget can lie over;
// showing the editor's own bar would resize the view (its swapchain and render
// targets) each time. The mouse is polled (QCursor::pos every 50 ms) as its
// events go to whichever window lies under it, the 3D view's mostly.
class MenuRevealBar : public QWidget {
    Q_OBJECT
public:
    MenuRevealBar(QWidget *editor, const QList<QAction*> &menus);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void poll();
    void slide(bool in);
    // The window over the editor's top, `fraction` of the bar's height tall;
    // the bar sits at its bottom, so it comes down from above.
    void place(double fraction);
    int barHeight() const;

    QWidget *editor;
    QMenuBar *bar;
    QTimer timer;
    QVariantAnimation animation;
    double shownFraction = 0.0;
    // Sliding in or in.
    bool revealed = false;
    // Since the mouse left the bar (invalid while over it).
    QElapsedTimer away;
};

#endif // MENUREVEALBAR_H
