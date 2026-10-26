/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef UIRUNTIME_H
#define UIRUNTIME_H

#include <QString>
#include <functional>
#include <memory>

namespace Script {
class State;
}

namespace Ui {

// RmlUi in TSRE (task scripting 01): one for the process, used on the GUI
// thread only (RmlUi is not thread safe). RmlUi gets Qt's clock, log,
// clipboard and files, fonts drawn by Qt (RmlQtFontInterface.cpp), text
// translated by TSRE's catalogues, and the UI's sandboxed Lua state for its
// Lua plugin (documents' scripts and event handlers). Contexts are made by
// the views, each with its renderer's render interface.
//
// Lua runs only under a deadline: wrap calls that may run a document's
// scripts (loading a document, input, Context::Update) in a
// Script::State::Deadline on lua(). Keep nothing on the UI state's stack
// across them: RmlUi's plugin empties it before each handler.
class Runtime {
public:
    // RmlUi started with TSRE's UI folder (appdata/<version>/ui): the fonts
    // in its `fonts` folder loaded, its Lua modules found by `require`.
    // False, nothing started, when RmlUi does not start.
    static bool start(const QString &folder);
    // Contexts are to be removed first.
    static void stop();
    // Null when not started.
    static Runtime *get();

    Script::State &lua();
    const QString &folder() const { return uiFolder; }
    // A font file for documents (its family, style and weight read from
    // it); a fallback face fills characters other faces lack.
    bool loadFont(const QString &path, bool fallback = false);
    // Where the pointer shape RmlUi asks for ("pointer", "text", "move" or
    // empty) goes: the view showing the documents.
    void setCursorHandler(std::function<void(const QString &)> handler);

    ~Runtime();

private:
    struct Interfaces;
    explicit Runtime(const QString &folder);

    QString uiFolder;
    std::unique_ptr<Interfaces> interfaces;
    std::unique_ptr<Script::State> state;
};

}

#endif // UIRUNTIME_H
