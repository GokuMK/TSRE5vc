/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/ui/UiScreen.h>
#include <tsre/ui/UiRenderInterface.h>
#include <tsre/ui/UiRuntime.h>
#include <tsre/Game.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/script/ScriptState.h>

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <QDebug>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Ui {

namespace {

int nextContext = 1;

Rml::Input::KeyIdentifier keyIdentifier(int key) {
    using namespace Rml::Input;
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return KeyIdentifier(KI_A + (key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return KeyIdentifier(KI_0 + (key - Qt::Key_0));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return KeyIdentifier(KI_F1 + (key - Qt::Key_F1));
    switch (key) {
    case Qt::Key_Space: return KI_SPACE;
    case Qt::Key_Return: return KI_RETURN;
    case Qt::Key_Enter: return KI_NUMPADENTER;
    case Qt::Key_Escape: return KI_ESCAPE;
    case Qt::Key_Tab:
    case Qt::Key_Backtab: return KI_TAB;
    case Qt::Key_Backspace: return KI_BACK;
    case Qt::Key_Delete: return KI_DELETE;
    case Qt::Key_Insert: return KI_INSERT;
    case Qt::Key_Home: return KI_HOME;
    case Qt::Key_End: return KI_END;
    case Qt::Key_PageUp: return KI_PRIOR;
    case Qt::Key_PageDown: return KI_NEXT;
    case Qt::Key_Left: return KI_LEFT;
    case Qt::Key_Right: return KI_RIGHT;
    case Qt::Key_Up: return KI_UP;
    case Qt::Key_Down: return KI_DOWN;
    case Qt::Key_Shift: return KI_LSHIFT;
    case Qt::Key_Control: return KI_LCONTROL;
    case Qt::Key_Alt: return KI_LMENU;
    case Qt::Key_Meta: return KI_LMETA;
    case Qt::Key_Semicolon: return KI_OEM_1;
    case Qt::Key_Plus:
    case Qt::Key_Equal: return KI_OEM_PLUS;
    case Qt::Key_Comma: return KI_OEM_COMMA;
    case Qt::Key_Minus: return KI_OEM_MINUS;
    case Qt::Key_Period: return KI_OEM_PERIOD;
    case Qt::Key_Slash: return KI_OEM_2;
    case Qt::Key_QuoteLeft: return KI_OEM_3;
    case Qt::Key_BracketLeft: return KI_OEM_4;
    case Qt::Key_Backslash: return KI_OEM_5;
    case Qt::Key_BracketRight: return KI_OEM_6;
    case Qt::Key_Apostrophe: return KI_OEM_7;
    default: return KI_UNKNOWN;
    }
}

int modifiers(Qt::KeyboardModifiers qt) {
    int state = 0;
    if (qt & Qt::ControlModifier)
        state |= Rml::Input::KM_CTRL;
    if (qt & Qt::ShiftModifier)
        state |= Rml::Input::KM_SHIFT;
    if (qt & Qt::AltModifier)
        state |= Rml::Input::KM_ALT;
    if (qt & Qt::MetaModifier)
        state |= Rml::Input::KM_META;
    return state;
}

int buttonIndex(Qt::MouseButton button) {
    switch (button) {
    case Qt::LeftButton: return 0;
    case Qt::RightButton: return 1;
    case Qt::MiddleButton: return 2;
    default: return -1;
    }
}

// Where a widget moved by a handle sits: x and y as parts of the room
// around it in its container (0 the left or top edge, 1 the right or
// bottom), kept in this attribute.
const char *PlaceAttribute = "data-place";

// What a handle moves: "#parent", "#document" or "#self", as RmlUi reads
// its move_target, or an element under it.
Rml::Element *moveTarget(Rml::Element *handle) {
    const Rml::String name = handle->GetAttribute<Rml::String>("move_target", "");
    if (name == "#self")
        return handle;
    if (name == "#parent")
        return handle->GetParentNode();
    if (name == "#document")
        return handle->GetOwnerDocument();
    return name.empty() ? nullptr : handle->GetElementById(name);
}

// The room around an element in its container, in pixels.
Rml::Vector2f room(Rml::Element *element) {
    Rml::Element *container = element->GetOffsetParent();
    if (container == nullptr)
        return Rml::Vector2f(0.0f, 0.0f);
    return Rml::Vector2f(std::max(0.0f, container->GetClientWidth() - element->GetOffsetWidth()),
                         std::max(0.0f, container->GetClientHeight() - element->GetOffsetHeight()));
}

// A widget a handle moved: its place kept as parts of the room around it.
class Placer : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event &event) override {
        Rml::Element *handle = event.GetTargetElement();
        if (handle == nullptr || handle->GetTagName() != "handle")
            return;
        Rml::Element *widget = moveTarget(handle);
        if (widget == nullptr || widget->GetOffsetParent() == nullptr)
            return;
        const Rml::Vector2f space = room(widget);
        const float x = space.x > 0.0f ? std::clamp(widget->GetOffsetLeft() / space.x, 0.0f, 1.0f) : 0.0f;
        const float y = space.y > 0.0f ? std::clamp(widget->GetOffsetTop() / space.y, 0.0f, 1.0f) : 0.0f;
        widget->SetAttribute(PlaceAttribute, Rml::CreateString("%g %g", x, y));
    }
};

// Lua runs only under a deadline: one for each event.
struct Deadline {
    std::unique_ptr<Script::State::Deadline> deadline;
    Deadline() {
        if (Runtime *runtime = Runtime::get())
            deadline.reset(new Script::State::Deadline(runtime->lua()));
    }
};

}

struct Screen::Data {
    std::unique_ptr<RenderInterface> renderInterface{new RenderInterface};
    Placer placer;
    // The view's size or the scale changed: moved widgets placed again.
    bool resized = false;
    Rml::Context *context = nullptr;
    std::string name;
    float pixelRatio = 1.0f;
    bool failed = false;
    // The mouse buttons whose press the UI took, and the view's.
    int uiButtons = 0;
    int viewButtons = 0;
    Keys lastKeys = Keys::View;
    std::function<void()> keysTaken;
    std::function<bool()> menu;
    bool debugger = false;
    // Shown: RmlUi's own answer is stale until the debugger is laid out.
    bool debuggerShown = false;
    // TSRE_UI_LOG.
    bool logged = qEnvironmentVariableIsSet("TSRE_UI_LOG");
    qint64 frame[4] = {};
    qint64 total[4] = {};
    qint64 most[4] = {};
    int frames = 0;
    QElapsedTimer since;

    int x(const QPointF &position) const { return int(std::lround(position.x() * pixelRatio)); }
    int y(const QPointF &position) const { return int(std::lround(position.y() * pixelRatio)); }
};

Screen::Screen() : d(new Data) {
}

Screen::~Screen() {
    // RmlUi lets go of the render interface before it goes.
    if (d->context != nullptr && Runtime::get() != nullptr) {
        Rml::RemoveContext(d->name);
        Rml::ReleaseRenderManagers();
    }
}

bool Screen::ready(int width, int height, float pixelRatio, float scale) {
    if (d->failed || width < 1 || height < 1)
        return false;
    if (d->context == nullptr) {
        if (!Runtime::start(QString("appdata/") + Game::AppDataVersion + "/ui")) {
            d->failed = true;
            return false;
        }
        d->name = "screen" + std::to_string(nextContext++);
        d->context = Rml::CreateContext(d->name, Rml::Vector2i(width, height), d->renderInterface.get());
        if (d->context == nullptr) {
            d->failed = true;
            return false;
        }
        d->context->AddEventListener("dragend", &d->placer);
    }
    d->pixelRatio = pixelRatio;
    if (d->context->GetDimensions() != Rml::Vector2i(width, height)) {
        d->context->SetDimensions(Rml::Vector2i(width, height));
        d->resized = true;
    }
    // A virtual pixel: the view's height over CanvasHeight.
    const float dp = float(height) / CanvasHeight * scale;
    if (std::fabs(d->context->GetDensityIndependentPixelRatio() - dp) > 1e-4f) {
        d->context->SetDensityIndependentPixelRatio(dp);
        d->resized = true;
    }
    return true;
}

bool Screen::usable() const {
    return !d->failed;
}

Rml::Context *Screen::context() const {
    return d->context;
}

void Screen::update() {
    if (d->context == nullptr)
        return;
    QElapsedTimer timer;
    timer.start();
    Deadline deadline;
    d->context->Update();
    // Resized: widgets moved by handles back at their parts of the room
    // (laid out at the new size first, then moved).
    if (d->resized) {
        d->resized = false;
        bool moved = false;
        for (int i = 0; i < d->context->GetNumDocuments(); ++i) {
            Rml::ElementList placed;
            d->context->GetDocument(i)->QuerySelectorAll(placed, Rml::String("[") + PlaceAttribute + "]");
            for (Rml::Element *widget : placed) {
                float x = 0.0f, y = 0.0f;
                if (std::sscanf(widget->GetAttribute<Rml::String>(PlaceAttribute, "").c_str(), "%f %f", &x, &y) != 2)
                    continue;
                const Rml::Vector2f space = room(widget);
                widget->SetProperty(Rml::PropertyId::Left, Rml::Property(std::round(x * space.x), Rml::Unit::PX));
                widget->SetProperty(Rml::PropertyId::Top, Rml::Property(std::round(y * space.y), Rml::Unit::PX));
                moved = true;
            }
        }
        if (moved)
            d->context->Update();
    }
    measured(Part::Update, timer.nsecsElapsed());
}

void Screen::draw(Renderer *renderer) {
    if (d->context == nullptr || renderer == nullptr)
        return;
    QElapsedTimer timer;
    timer.start();
    const Rml::Vector2i size = d->context->GetDimensions();
    d->renderInterface->beginFrame(size.x, size.y);
    d->context->Render();
    measured(Part::Render, timer.nsecsElapsed());
    timer.start();
    renderer->drawUi(d->renderInterface->list());
    measured(Part::Draw, timer.nsecsElapsed());
    if (!d->logged)
        return;
    for (int i = 0; i < 4; ++i) {
        d->total[i] += d->frame[i];
        d->most[i] = std::max(d->most[i], d->frame[i]);
        d->frame[i] = 0;
    }
    ++d->frames;
    if (!d->since.isValid())
        d->since.start();
    if (d->since.elapsed() < 5000)
        return;
    static const char *const names[4] = {"event", "update", "render", "draw"};
    QString line = QString("[ui] UI CPU over %1 frames, mean/max ms:").arg(d->frames);
    qint64 sum = 0;
    for (int i = 0; i < 4; ++i) {
        line += QString(" %1 %2/%3").arg(names[i]).arg(d->total[i] / 1e6 / d->frames, 0, 'f', 3)
                    .arg(d->most[i] / 1e6, 0, 'f', 3);
        sum += d->total[i];
        d->total[i] = d->most[i] = 0;
    }
    line += QString(", all %1").arg(sum / 1e6 / d->frames, 0, 'f', 3);
    qInfo().noquote() << line;
    d->frames = 0;
    d->since.start();
}

void Screen::measured(Part part, qint64 ns) {
    if (d->logged)
        d->frame[int(part)] += ns;
}

bool Screen::mouseMove(QMouseEvent *event) {
    if (d->context == nullptr || d->viewButtons != 0)
        return false;
    Deadline deadline;
    d->context->ProcessMouseMove(d->x(event->position()), d->y(event->position()), modifiers(event->modifiers()));
    return d->uiButtons != 0;
}

bool Screen::mousePress(QMouseEvent *event) {
    const int button = buttonIndex(event->button());
    if (d->context == nullptr || button < 0)
        return false;
    // A drag of the view's goes on as the view's.
    if (d->viewButtons != 0) {
        d->viewButtons |= 1 << button;
        return false;
    }
    Deadline deadline;
    const int state = modifiers(event->modifiers());
    d->context->ProcessMouseMove(d->x(event->position()), d->y(event->position()), state);
    if (d->uiButtons == 0 && !d->context->IsMouseInteracting() && keys() != Keys::Menu) {
        d->viewButtons |= 1 << button;
        // A click in the view: a text field lets go of the keys.
        if (keys() == Keys::Text)
            escape(Keys::Text);
        return false;
    }
    d->uiButtons |= 1 << button;
    d->context->ProcessMouseButtonDown(button, state);
    keysChanged();
    return true;
}

bool Screen::mouseRelease(QMouseEvent *event) {
    const int button = buttonIndex(event->button());
    if (d->context == nullptr || button < 0)
        return false;
    if (d->uiButtons & (1 << button)) {
        Deadline deadline;
        d->uiButtons &= ~(1 << button);
        d->context->ProcessMouseButtonUp(button, modifiers(event->modifiers()));
        keysChanged();
        return true;
    }
    d->viewButtons &= ~(1 << button);
    return false;
}

bool Screen::wheel(QWheelEvent *event) {
    if (d->context == nullptr || d->viewButtons != 0)
        return false;
    Deadline deadline;
    const int state = modifiers(event->modifiers());
    d->context->ProcessMouseMove(d->x(event->position()), d->y(event->position()), state);
    const bool menu = keys() == Keys::Menu;
    if (!d->context->IsMouseInteracting() && !menu)
        return false;
    // Lines down and right; true from RmlUi when nothing scrolled.
    const QPoint steps = event->angleDelta();
    return !d->context->ProcessMouseWheel(Rml::Vector2f(-steps.x() / 120.0f, -steps.y() / 120.0f), state) || menu;
}

bool Screen::keyPress(QKeyEvent *event) {
    if (d->context == nullptr)
        return false;
    const Keys owner = keys();
    if (owner == Keys::View) {
        if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier)
            return action(Action::Menu);
        if (event->key() == Qt::Key_F8 && event->modifiers() == Qt::NoModifier) {
            toggleDebugger();
            return true;
        }
        return false;
    }
    Deadline deadline;
    const int state = modifiers(event->modifiers());
    const bool unused = d->context->ProcessKeyDown(keyIdentifier(event->key()), state);
    // Typing: printable text, not shortcuts.
    const QString text = event->text();
    if (owner == Keys::Text && !text.isEmpty() && text.at(0).isPrint()
            && !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)))
        d->context->ProcessTextInput(text.toStdString());
    // Esc leaves a text field; it closes a menu unless the menu used it (a
    // list it closed).
    if (event->key() == Qt::Key_Escape && (unused || owner == Keys::Text))
        escape(owner);
    keysChanged();
    return true;
}

bool Screen::keyRelease(QKeyEvent *event) {
    if (d->context == nullptr || keys() == Keys::View)
        return false;
    Deadline deadline;
    d->context->ProcessKeyUp(keyIdentifier(event->key()), modifiers(event->modifiers()));
    return true;
}

bool Screen::action(Action action) {
    if (d->context == nullptr)
        return false;
    const Keys owner = keys();
    if (owner == Keys::View) {
        if (action != Action::Menu || !d->menu)
            return false;
        const bool opened = d->menu();
        keysChanged();
        return opened;
    }
    using namespace Rml::Input;
    KeyIdentifier key = KI_UNKNOWN;
    int state = 0;
    switch (action) {
    case Action::Up: key = KI_UP; break;
    case Action::Down: key = KI_DOWN; break;
    case Action::Left: key = KI_LEFT; break;
    case Action::Right: key = KI_RIGHT; break;
    case Action::Accept: key = KI_RETURN; break;
    case Action::Back:
    case Action::Menu: key = KI_ESCAPE; break;
    case Action::Next: key = KI_TAB; break;
    case Action::Previous: key = KI_TAB; state = KM_SHIFT; break;
    }
    Deadline deadline;
    const bool unused = d->context->ProcessKeyDown(key, state);
    d->context->ProcessKeyUp(key, state);
    if (key == KI_ESCAPE && unused)
        escape(owner);
    keysChanged();
    return true;
}

Screen::Keys Screen::keys() const {
    if (d->context == nullptr)
        return Keys::View;
    for (int i = 0; i < d->context->GetNumDocuments(); ++i) {
        Rml::ElementDocument *document = d->context->GetDocument(i);
        if (document->IsVisible() && document->IsModal())
            return Keys::Menu;
    }
    if (Rml::Element *focus = d->context->GetFocusElement()) {
        const Rml::String tag = focus->GetTagName();
        const Rml::String type = focus->GetAttribute<Rml::String>("type", "text");
        if (tag == "textarea" || (tag == "input" && (type == "text" || type == "password")))
            return Keys::Text;
    }
    return Keys::View;
}

void Screen::showMenu(Rml::ElementDocument *document) {
    if (document == nullptr)
        return;
    Deadline deadline;
    document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Auto);
    // Its first control focused, unless one has `autofocus`: keys and a
    // gamepad navigate it from the start.
    if (d->context->GetFocusElement() == document) {
        d->context->ProcessKeyDown(Rml::Input::KI_TAB, 0);
        d->context->ProcessKeyUp(Rml::Input::KI_TAB, 0);
    }
    keysChanged();
}

void Screen::setKeysTaken(std::function<void()> handler) {
    d->keysTaken = std::move(handler);
}

void Screen::setMenuHandler(std::function<bool()> handler) {
    d->menu = std::move(handler);
}

void Screen::toggleDebugger() {
    if (d->context == nullptr)
        return;
    if (!d->debugger)
        d->debugger = Rml::Debugger::Initialise(d->context);
    if (d->debugger) {
        d->debuggerShown = !d->debuggerShown;
        Rml::Debugger::SetVisible(d->debuggerShown);
    }
}

void Screen::keysChanged() {
    const Keys now = keys();
    if (d->lastKeys == Keys::View && now != Keys::View && d->keysTaken)
        d->keysTaken();
    d->lastKeys = now;
}

void Screen::escape(Keys keys) {
    if (keys == Keys::Menu) {
        for (int i = d->context->GetNumDocuments() - 1; i >= 0; --i) {
            Rml::ElementDocument *document = d->context->GetDocument(i);
            if (document->IsVisible() && document->IsModal()) {
                document->Hide();
                break;
            }
        }
    } else if (keys == Keys::Text) {
        if (Rml::Element *focus = d->context->GetFocusElement())
            focus->Blur();
    }
    d->lastKeys = this->keys();
}

}
