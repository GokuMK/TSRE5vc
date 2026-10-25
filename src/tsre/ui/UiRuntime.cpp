/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/ui/UiRuntime.h>
#include <tsre/script/ScriptState.h>

#include <RmlUi/Core.h>
#include <RmlUi/Lua.h>

#include <QClipboard>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QRegularExpression>
#include <cstdio>

namespace Ui {

namespace {

Runtime *running = nullptr;

QString fromRml(const Rml::String &text) {
    return QString::fromUtf8(text.data(), qsizetype(text.size()));
}

class System : public Rml::SystemInterface {
public:
    QElapsedTimer clock;
    std::function<void(const QString &)> cursor;

    System() { clock.start(); }

    double GetElapsedTime() override { return double(clock.nsecsElapsed()) * 1e-9; }

    // A text that is exactly a TSRE translation id (`ui.hud.speed`) shows
    // its translation; any other text shows as written.
    int TranslateString(Rml::String &translated, const Rml::String &input) override {
        static const QRegularExpression id("^[a-z0-9]+(\\.[a-z0-9]+)+$");
        const QString text = fromRml(input).trimmed();
        if (id.match(text).hasMatch()) {
            const QByteArray key = text.toUtf8();
            const QString translation = qtTrId(key.constData());
            if (translation != text) {
                translated = translation.toStdString();
                return 1;
            }
        }
        translated = input;
        return 0;
    }

    // A file system path (content's files) as it is; others relative to the
    // document, as RmlUi joins them (its leading "/" meaning its own root).
    void JoinPath(Rml::String &translated, const Rml::String &documentPath, const Rml::String &path) override {
        if (QDir::isAbsolutePath(fromRml(path))) {
            translated = path;
            return;
        }
        Rml::SystemInterface::JoinPath(translated, documentPath, path);
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String &message) override {
        switch (type) {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:
        case Rml::Log::LT_WARNING:
            qWarning().noquote() << "[ui]" << fromRml(message);
            break;
        case Rml::Log::LT_ALWAYS:
            qInfo().noquote() << "[ui]" << fromRml(message);
            break;
        default:
            qDebug().noquote() << "[ui]" << fromRml(message);
            break;
        }
        return true;
    }

    void SetMouseCursor(const Rml::String &name) override {
        if (cursor)
            cursor(fromRml(name));
    }

    void SetClipboardText(const Rml::String &text) override {
        if (QClipboard *clipboard = QGuiApplication::clipboard())
            clipboard->setText(fromRml(text));
    }

    void GetClipboardText(Rml::String &text) override {
        if (QClipboard *clipboard = QGuiApplication::clipboard())
            text = clipboard->text().toStdString();
    }
};

// Files by their paths as RmlUi joins them (relative to the document).
class Files : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String &path) override {
        auto file = std::make_unique<QFile>(fromRml(path));
        if (!file->open(QIODevice::ReadOnly))
            return 0;
        // A compiled Lua chunk could crash Lua: documents get text only.
        if (file->peek(4) == QByteArray("\x1bLua", 4)) {
            qWarning().noquote() << "[ui]" << fromRml(path) << "is compiled Lua, which documents cannot load";
            return 0;
        }
        return reinterpret_cast<Rml::FileHandle>(file.release());
    }

    void Close(Rml::FileHandle handle) override { delete fileOf(handle); }

    size_t Read(void *buffer, size_t size, Rml::FileHandle handle) override {
        const qint64 read = fileOf(handle)->read(static_cast<char *>(buffer), qint64(size));
        return read > 0 ? size_t(read) : 0;
    }

    bool Seek(Rml::FileHandle handle, long offset, int origin) override {
        QFile *file = fileOf(handle);
        qint64 position = offset;
        if (origin == SEEK_CUR)
            position += file->pos();
        else if (origin == SEEK_END)
            position += file->size();
        return file->seek(position);
    }

    size_t Tell(Rml::FileHandle handle) override { return size_t(fileOf(handle)->pos()); }

    size_t Length(Rml::FileHandle handle) override { return size_t(fileOf(handle)->size()); }

private:
    static QFile *fileOf(Rml::FileHandle handle) { return reinterpret_cast<QFile *>(handle); }
};

}

struct Runtime::Interfaces {
    System system;
    Files files;
};

Runtime::Runtime(const QString &folder) : uiFolder(folder), interfaces(new Interfaces) {
}

Runtime::~Runtime() = default;

bool Runtime::start(const QString &folder) {
    if (running != nullptr)
        return true;
    auto runtime = std::unique_ptr<Runtime>(new Runtime(QDir(folder).absolutePath()));
    runtime->state.reset(new Script::State(Script::State::Kind::Ui));
    runtime->state->setModuleRoots({runtime->uiFolder});
    Rml::SetSystemInterface(&runtime->interfaces->system);
    Rml::SetFileInterface(&runtime->interfaces->files);
    // Documents' scripts and handlers run in the UI's sandboxed state.
    Rml::Lua::Initialise(runtime->state->lua());
    if (!Rml::Initialise()) {
        qWarning().noquote() << "[ui] RmlUi did not start";
        Rml::Shutdown();
        return false;
    }
    running = runtime.release();
    const QDir fonts(running->uiFolder + "/fonts");
    for (const QString &name : fonts.entryList({"*.ttf", "*.otf"}, QDir::Files, QDir::Name))
        running->loadFont(fonts.filePath(name));
    return true;
}

void Runtime::stop() {
    if (running == nullptr)
        return;
    // RmlUi first: its Lua plugin lets go of the state, its fonts of Qt's.
    Rml::Shutdown();
    delete running;
    running = nullptr;
}

Runtime *Runtime::get() {
    return running;
}

Script::State &Runtime::lua() {
    return *state;
}

bool Runtime::loadFont(const QString &path, bool fallback) {
    return Rml::LoadFontFace(path.toStdString(), fallback);
}

void Runtime::setCursorHandler(std::function<void(const QString &)> handler) {
    interfaces->system.cursor = std::move(handler);
}

}
