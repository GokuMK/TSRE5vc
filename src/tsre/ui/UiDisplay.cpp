/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/ui/UiDisplay.h>
#include <tsre/ui/UiRenderInterface.h>
#include <tsre/ui/UiRuntime.h>
#include <tsre/renderer/Renderer.h>
#include <tsre/script/ScriptState.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>
#include <tsre/Game.h>

#include <RmlUi/Core.h>
#include <QDebug>

namespace Ui {

struct Display::Private {
    std::unique_ptr<RenderInterface> renderInterface = std::make_unique<RenderInterface>();
    Rml::Context *context = nullptr;
    Rml::ElementDocument *document = nullptr;
    std::string name;
    bool picturesPending = false;
    unsigned int texture = 0;
};

namespace {
int nextDisplay = 0;
}

Display::Display(int width, int height) : d(new Private) {
    if (width < 1 || height < 1 || !Runtime::start(QString("appdata/") + Game::AppDataVersion + "/ui"))
        return;
    d->name = "display" + std::to_string(nextDisplay++);
    d->context = Rml::CreateContext(d->name, Rml::Vector2i(width, height), d->renderInterface.get());
}

Display::~Display() {
    // RmlUi lets go of the render interface before it goes: its document's
    // pictures (TexLib's, released through it) and its render manager.
    if (d->context != nullptr && Runtime::get() != nullptr) {
        Rml::RemoveContext(d->name);
        Rml::ReleaseTextures(d->renderInterface.get());
        Rml::ReleaseRenderManagers();
    }
}

Rml::Context *Display::context() const {
    return d->context;
}

bool Display::load(const QString &path) {
    if (d->context == nullptr)
        return false;
    Runtime::get()->loadsFrom(path);
    Script::State::Deadline deadline(Runtime::get()->lua());
    Rml::ElementDocument *document = d->context->LoadDocument(path.toStdString());
    if (document == nullptr) {
        qWarning().noquote() << "[ui] the display's document did not load:" << path;
        return false;
    }
    document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    d->document = document;
    return true;
}

Rml::ElementDocument *Display::document() const {
    return d->document;
}

bool Display::picturesPending() const {
    return d->picturesPending;
}

void Display::update() {
    if (d->context == nullptr)
        return;
    Script::State::Deadline deadline(Runtime::get()->lua());
    d->context->Update();
}

UiDrawList &Display::render() {
    if (d->context != nullptr) {
        const Rml::Vector2i size = d->context->GetDimensions();
        d->renderInterface->beginFrame(size.x, size.y);
        d->context->Render();
    }
    return d->renderInterface->list();
}

unsigned int Display::draw(Renderer &renderer) {
    if (d->context == nullptr)
        return 0;
    UiDrawList &list = render();
    // Its pictures not yet uploaded before the draw (the renderers upload
    // loaded ones as they draw, QRhi within a budget): maybe left out.
    d->picturesPending = false;
    for (const auto &[handle, texture] : list.libraryTextures) {
        const auto found = TexLib::mtex.find(texture);
        d->picturesPending = d->picturesPending || found == TexLib::mtex.end() || found->second == nullptr
                || !found->second->glLoaded;
    }
    d->texture = renderer.drawUiTexture(list, d->texture);
    return d->texture;
}

unsigned int Display::texture() const {
    return d->texture;
}

void Display::release(Renderer &renderer) {
    if (d->texture != 0)
        renderer.releaseUiTexture(d->texture);
    d->texture = 0;
}

}
