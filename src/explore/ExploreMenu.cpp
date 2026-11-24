/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <explore/ExploreMenu.h>
#include <explore/ExplorePanel.h>
#include <tsre/Game.h>
#include <tsre/script/ScriptState.h>
#include <tsre/ui/UiRuntime.h>
#include <tsre/ui/UiScreen.h>

#include <RmlUi/Core.h>
#include <QDebug>
#include <QTimer>

namespace Explore {

namespace {

// An RmlUi event handled by a function.
class Listener : public Rml::EventListener {
public:
    explicit Listener(std::function<void(Rml::Event &)> handler) : handler(std::move(handler)) {}
    void ProcessEvent(Rml::Event &event) override { handler(event); }

private:
    std::function<void(Rml::Event &)> handler;
};

int keyOf(const Rml::Event &event) {
    return event.GetParameter<int>("key_identifier", 0);
}

// Whether `element` is `ancestor` or inside it.
bool inside(Rml::Element *element, Rml::Element *ancestor) {
    for (Rml::Element *e = element; e != nullptr; e = e->GetParentNode())
        if (e == ancestor)
            return true;
    return false;
}

}

struct Menu::Data {
    QString root;
    QString route;
    std::function<void(const RunChoice &)> start;
    Rml::ElementDocument *document = nullptr;
    Rml::Element *window = nullptr;
    std::unique_ptr<Panel> panel;
    std::vector<std::unique_ptr<Listener>> listeners;
    // Each listener with what it listens on, to be removed.
    struct Attached {
        Rml::Element *element;
        Rml::EventId event;
        Listener *listener;
        bool capture;
    };
    std::vector<Attached> attached;
    // The keys go into the window once its entry is made (the model's
    // elements come with the next update).
    bool enterPending = false;
    int enterTries = 0;

    // `capture`: before the element the event is for (a text field stops
    // every key it is given).
    void listen(Rml::Element *element, Rml::EventId event, std::function<void(Rml::Event &)> handler,
                bool capture = false) {
        if (element == nullptr)
            return;
        listeners.push_back(std::make_unique<Listener>(std::move(handler)));
        element->AddEventListener(event, listeners.back().get(), capture);
        attached.push_back({element, event, listeners.back().get(), capture});
    }
    Rml::Element *item(const char *id) const {
        return document != nullptr ? document->GetElementById(id) : nullptr;
    }
};

Menu::Menu(Ui::Screen &screen, const QString &root, const QString &route,
           std::function<void(const RunChoice &)> start, std::function<void()> exit)
    : screen(screen), d(new Data), exitHandler(std::move(exit)) {
    d->root = root;
    d->route = route;
    d->start = std::move(start);
}

Menu::~Menu() {
    for (const Data::Attached &a : d->attached)
        a.element->RemoveEventListener(a.event, a.listener, a.capture);
    if (d->document != nullptr)
        d->document->Close();
    // The document goes at the context's next update; its model after it.
    if (d->panel && screen.context() != nullptr)
        screen.context()->Update();
    d->panel.reset();
}

Panel *Menu::panel() const {
    return d->panel.get();
}

bool Menu::load() {
    Rml::Context *context = screen.context();
    Ui::Runtime *runtime = Ui::Runtime::get();
    if (context == nullptr || runtime == nullptr)
        return false;
    // The model first: the document binds it as it loads.
    d->panel = std::make_unique<Panel>(*context, d->root, d->route, [this](const RunChoice &choice) {
        // After RmlUi's event: the run hides this document.
        QTimer::singleShot(0, [start = d->start, choice] {
            if (start)
                start(choice);
        });
    });
    if (!d->panel->usable()) {
        qWarning().noquote() << "[explore] the Explore window's data model could not be made";
        return false;
    }
    const QString folder = runtime->folder();
    {
        Script::State::Deadline deadline(runtime->lua());
        d->document = context->LoadDocument((folder + "/explore-menu.rml").toStdString());
        // Styled before it is shown: its first control is focused only when
        // the keys can reach it (tab-index computed).
        context->Update();
    }
    if (d->document == nullptr) {
        qWarning().noquote() << "[explore] the menu's document did not load:" << folder;
        return false;
    }
    d->panel->setDocument(d->document);
    d->window = d->document->GetElementById("explore-window");
    if (Rml::Element *version = d->item("version"))
        version->SetInnerRML(Rml::StringUtilities::EncodeRml(
                QString("%1 %2").arg(Game::AppName, Game::AppVersion).toStdString()));

    // The mouse over a menu item chooses it, as the keys do: one item lit.
    Rml::ElementList buttons;
    if (Rml::Element *items = d->item("items"))
        items->GetElementsByTagName(buttons, "button");
    for (Rml::Element *button : buttons)
        d->listen(button, Rml::EventId::Mouseover, [button](Rml::Event &) {
            if (button->IsClassSet("later"))
                return;
            button->Focus(true);
        });
    d->listen(d->item("explore"), Rml::EventId::Click, [this](Rml::Event &) {
        open(Page::Explore);
    });
    d->listen(d->item("exit"), Rml::EventId::Click, [this](Rml::Event &) {
        // After RmlUi's event: quitting may close the window under it.
        QTimer::singleShot(0, [exit = exitHandler] {
            if (exit)
                exit();
        });
    });
    // The keys between the menu and its window: Right on the marked item
    // in; Esc in the window back to it (a picker closed first; a run's page
    // back to the route page first, task explore 05). Esc on the menu stays
    // (the screen would hide the document). In the capture phase: a text
    // field (a filter) would stop Esc and Down itself.
    d->listen(d->document, Rml::EventId::Keydown, [this](Rml::Event &event) {
        Rml::Element *focus = screen.context() != nullptr ? screen.context()->GetFocusElement() : nullptr;
        const bool inWindow = d->window != nullptr && inside(focus, d->window);
        const int key = keyOf(event);
        if (key == Rml::Input::KI_ESCAPE) {
            event.StopPropagation();
            if (d->panel && d->panel->picking())
                d->panel->closePicker();
            else if (inWindow && d->panel && d->panel->back())
                return;
            else if (inWindow)
                if (Rml::Element *explore = d->item("explore"))
                    explore->Focus(true);
            return;
        }
        // A list's first row and its filter (task explore 05); the page's
        // text scrolled by Page Up and Page Down.
        if ((key == Rml::Input::KI_UP || key == Rml::Input::KI_DOWN) && inWindow && d->panel
                && !d->panel->picking() && d->panel->moveUpDown(focus, key == Rml::Input::KI_DOWN)) {
            event.StopPropagation();
            return;
        }
        if ((key == Rml::Input::KI_PRIOR || key == Rml::Input::KI_NEXT) && inWindow && d->panel
                && !d->panel->picking() && d->panel->scrollDetails(key == Rml::Input::KI_NEXT)) {
            event.StopPropagation();
            return;
        }
        if (key == Rml::Input::KI_RIGHT && focus != nullptr && focus->GetId() == "explore") {
            event.StopPropagation();
            open(Page::Explore);
            return;
        }
        // Across the window's columns (RmlUi's arrows stay in a list); none
        // while a picker is open.
        if ((key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT) && inWindow && d->panel) {
            if (d->panel->picking() || d->panel->moveAcross(focus, key == Rml::Input::KI_RIGHT))
                event.StopPropagation();
        }
    }, true);
    return true;
}

void Menu::frame() {
    if (failed)
        return;
    if (!loaded) {
        loaded = true;
        if (!load()) {
            failed = true;
            return;
        }
        if (current == Page::Explore)
            open(Page::Explore);
    }
    d->panel->frame();
    if (showing && !d->document->IsVisible())
        screen.showMenu(d->document);
    if (d->enterPending)
        enterWindow();
}

void Menu::open(Page page) {
    current = page;
    if (!loaded || failed)
        return;
    const bool window = page == Page::Explore;
    if (window)
        d->panel->open();
    if (d->window != nullptr)
        d->window->SetClass("shown", window);
    if (Rml::Element *explore = d->item("explore"))
        explore->SetClass("current", window);
    if (showing && !d->document->IsVisible())
        screen.showMenu(d->document);
    if (window) {
        d->enterPending = true;
        d->enterTries = 0;
        enterWindow();
    } else if (Rml::Element *explore = d->item("explore")) {
        d->enterPending = false;
        explore->Focus(true);
    }
}

void Menu::enterWindow() {
    Rml::Element *entry = d->panel ? d->panel->entry() : nullptr;
    if (entry != nullptr) {
        entry->Focus(true);
        d->enterPending = false;
    } else if (++d->enterTries > 5) {
        // No route to go to: the item keeps the keys.
        d->enterPending = false;
    }
}

void Menu::setShown(bool shown) {
    showing = shown;
    if (!loaded || failed)
        return;
    if (!shown && d->document->IsVisible())
        d->document->Hide();
    if (shown) {
        screen.showMenu(d->document);
        // Back from a run: in the window it was in.
        open(current);
    }
}

bool Menu::click(const QString &id) {
    if (!loaded || failed)
        return false;
    Rml::Element *element = d->document->GetElementById(id.toStdString());
    if (element == nullptr)
        return false;
    // Focused first, as a mouse click focuses it (a text field).
    element->Focus();
    element->Click();
    return true;
}

QString Menu::focused() const {
    Rml::Context *context = screen.context();
    Rml::Element *focus = context != nullptr ? context->GetFocusElement() : nullptr;
    if (focus == nullptr)
        return QString();
    if (!focus->GetId().empty())
        return QString::fromStdString(focus->GetId());
    for (Rml::Element *e = focus->GetParentNode(); e != nullptr; e = e->GetParentNode())
        if (!e->GetId().empty())
            return QString::fromStdString(e->GetId()) + ":item";
    return QString();
}

}
