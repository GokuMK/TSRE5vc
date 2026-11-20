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

}

struct Menu::Data {
    Rml::ElementDocument *main = nullptr;
    Rml::ElementDocument *explore = nullptr;
    std::vector<std::unique_ptr<Listener>> listeners;
    // Each listener with what it listens on, to be removed.
    std::vector<std::pair<Rml::Element *, std::pair<Rml::EventId, Listener *>>> attached;

    void listen(Rml::Element *element, Rml::EventId event, std::function<void(Rml::Event &)> handler) {
        if (element == nullptr)
            return;
        listeners.push_back(std::make_unique<Listener>(std::move(handler)));
        element->AddEventListener(event, listeners.back().get());
        attached.push_back({element, {event, listeners.back().get()}});
    }
};

Menu::Menu(Ui::Screen &screen, std::function<void()> exit)
    : screen(screen), d(new Data), exitHandler(std::move(exit)) {
}

Menu::~Menu() {
    for (const auto &[element, listener] : d->attached)
        element->RemoveEventListener(listener.first, listener.second);
    if (d->main != nullptr)
        d->main->Close();
    if (d->explore != nullptr)
        d->explore->Close();
}

bool Menu::load() {
    Rml::Context *context = screen.context();
    Ui::Runtime *runtime = Ui::Runtime::get();
    if (context == nullptr || runtime == nullptr)
        return false;
    const QString folder = runtime->folder();
    {
        Script::State::Deadline deadline(runtime->lua());
        d->main = context->LoadDocument((folder + "/explore-menu.rml").toStdString());
        d->explore = context->LoadDocument((folder + "/explore-window.rml").toStdString());
        // Styled before one is shown: its first control is focused only
        // when the keys can reach it (tab-index computed).
        context->Update();
    }
    if (d->main == nullptr || d->explore == nullptr) {
        qWarning().noquote() << "[explore] the menu's documents did not load:" << folder;
        return false;
    }
    if (Rml::Element *version = d->main->GetElementById("version"))
        version->SetInnerRML(Rml::StringUtilities::EncodeRml(
                QString("%1 %2").arg(Game::AppName, Game::AppVersion).toStdString()));

    // The mouse over an item chooses it, as the keys do: one item lit.
    for (Rml::ElementDocument *document : {d->main, d->explore}) {
        Rml::ElementList buttons;
        document->GetElementsByTagName(buttons, "button");
        for (Rml::Element *button : buttons)
            d->listen(button, Rml::EventId::Mouseover, [button](Rml::Event &) {
                if (button->IsClassSet("later"))
                    return;
                button->Focus();
            });
    }
    d->listen(d->main->GetElementById("explore"), Rml::EventId::Click, [this](Rml::Event &) {
        open(Page::Explore);
    });
    d->listen(d->main->GetElementById("exit"), Rml::EventId::Click, [this](Rml::Event &) {
        // After RmlUi's event: quitting may close the window under it.
        QTimer::singleShot(0, [exit = exitHandler] {
            if (exit)
                exit();
        });
    });
    d->listen(d->explore->GetElementById("back"), Rml::EventId::Click, [this](Rml::Event &) {
        open(Page::Main);
    });
    // Esc: the main menu stays; the Explore window goes back to it.
    d->listen(d->main, Rml::EventId::Keydown, [](Rml::Event &event) {
        if (event.GetParameter<int>("key_identifier", 0) == Rml::Input::KI_ESCAPE)
            event.StopPropagation();
    });
    d->listen(d->explore, Rml::EventId::Keydown, [this](Rml::Event &event) {
        if (event.GetParameter<int>("key_identifier", 0) != Rml::Input::KI_ESCAPE)
            return;
        event.StopPropagation();
        open(Page::Main);
    });
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
    }
    Rml::ElementDocument *shown = current == Page::Main ? d->main : d->explore;
    if (!shown->IsVisible())
        open(current);
}

void Menu::open(Page page) {
    if (!loaded || failed)
        return;
    current = page;
    Rml::ElementDocument *next = page == Page::Main ? d->main : d->explore;
    Rml::ElementDocument *other = page == Page::Main ? d->explore : d->main;
    if (other->IsVisible())
        other->Hide();
    screen.showMenu(next);
}

bool Menu::click(const QString &id) {
    if (!loaded || failed)
        return false;
    Rml::ElementDocument *shown = current == Page::Main ? d->main : d->explore;
    Rml::Element *element = shown->GetElementById(id.toStdString());
    if (element == nullptr)
        return false;
    element->Click();
    return true;
}

QString Menu::focused() const {
    Rml::Context *context = screen.context();
    Rml::Element *focus = context != nullptr ? context->GetFocusElement() : nullptr;
    return focus != nullptr ? QString::fromStdString(focus->GetId()) : QString();
}

}
