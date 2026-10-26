/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef UISCREEN_H
#define UISCREEN_H

#include <QString>
#include <QtGlobal>
#include <functional>
#include <memory>

class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class Renderer;

namespace Rml {
class Context;
class ElementDocument;
}

namespace Ui {

class RenderInterface;

// The UI over a view (task scripting 01): one RmlUi context for its
// documents (the HUD; later menus and other widgets), drawn by the view's
// renderer over the finished frame, and the router that decides, event by
// event, whether the UI or the view (its camera and tools) gets the input.
//
// Mouse: whoever gets a press keeps that button's drag until it is
// released. A press goes to the UI only over an element that takes the
// mouse (documents' bodies, and parts with `pointer-events: none`, let it
// through to the view; base.rcss), or anywhere while a menu is shown; a drag
// the view started never goes to the UI, even over a document. Moves always
// reach the UI for hover, the wheel only over something it scrolls.
// Documents move their widgets with RmlUi's `<handle move_target="...">`.
//
// Keyboard: the view has the keys, unless a menu is shown (a modal
// document: every key and action goes to the UI, which navigates it, Esc
// closing it) or a text field has the focus (typing, until Enter, Esc or a
// click elsewhere). Hovering or clicking a widget never takes the keys.
// While the view has the keys, Esc asks the menu handler (the game menu,
// later) and F8 shows RmlUi's debugger.
class Screen {
public:
    // Who has the keyboard.
    enum class Keys { View, Menu, Text };
    // TSRE_UI_LOG: the CPU time of the documents' events (their Lua), the
    // update, RmlUi's rendering into the draw list and the renderer's
    // drawer, every 5 s in the log.
    enum class Part { Event, Update, Render, Draw };
    // What drives the UI besides the mouse: the keys now, a gamepad later.
    enum class Action { Up, Down, Left, Right, Accept, Back, Next, Previous, Menu };

    Screen();
    ~Screen();
    // The context over a view of width x height pixels (`pixelRatio` pixels
    // a logical pixel of the view's events), documents at `scale` times
    // their size. Starts the UI first; false when it cannot.
    bool ready(int width, int height, float pixelRatio, float scale);
    // False once the UI failed to start.
    bool usable() const;
    Rml::Context *context() const;
    // The documents updated (their Lua under deadlines) and, with a
    // renderer, drawn by it.
    void update();
    void draw(Renderer *renderer);

    // The view's input, true when the UI took it: the view then leaves it.
    bool mouseMove(QMouseEvent *event);
    bool mousePress(QMouseEvent *event);
    bool mouseRelease(QMouseEvent *event);
    bool wheel(QWheelEvent *event);
    bool keyPress(QKeyEvent *event);
    bool keyRelease(QKeyEvent *event);
    // An action, true when taken. With the view's keys only Menu does
    // anything.
    bool action(Action action);

    Keys keys() const;
    // A document shown as a menu: modal, the keys its own, its first
    // control focused (or the one with `autofocus`).
    void showMenu(Rml::ElementDocument *document);
    // Called when the UI takes the keys from the view (a menu shown, a text
    // field focused), so the camera lets go of the keys it holds.
    void setKeysTaken(std::function<void()> handler);
    // The game menu: Esc (or the Menu action) with the view's keys; true
    // when it opened one. Without it Esc stays the view's.
    void setMenuHandler(std::function<bool()> handler);
    void toggleDebugger();
    // A part of this frame's work done (TSRE_UI_LOG); the frame ends with
    // draw().
    void measured(Part part, qint64 ns);

private:
    struct Data;
    std::unique_ptr<Data> d;

    // Who has the keys now; the view told when the UI takes them.
    void keysChanged();
    // Esc not used by the document: a menu closes, a text field lets go.
    void escape(Keys keys);
};

}

#endif // UISCREEN_H
