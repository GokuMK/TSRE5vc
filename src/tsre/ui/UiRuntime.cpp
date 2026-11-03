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
#include <tsre/Game.h>
#include <tsre/ui/UiDisplayDocument.h>
#include <tsre/fileFunctions/SimisDocument.h>
#include <tsre/script/ScriptState.h>
#include <tsre/shape/ShapeDisplays.h>

#include <RmlUi/Core.h>
#include <RmlUi/Lua.h>

#include <QClipboard>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <algorithm>
#include <cstdio>
#include <lua.h>

namespace Ui {

namespace {

Runtime *running = nullptr;
// The display's document whose RmlUi messages are reported (Reporting).
QString reportedDocument;

#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity PathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity PathCase = Qt::CaseSensitive;
#endif

QString fromRml(const Rml::String &text) {
    return QString::fromUtf8(text.data(), qsizetype(text.size()));
}

// A path from RmlUi, which keeps a document's external scripts with ':' as
// '|' (DocumentHeader): "C|/..." as "C:/...".
QString fromRmlPath(const Rml::String &path) {
    QString name = fromRml(path);
    if (name.size() > 2 && name[1] == QLatin1Char('|') && name[0].isLetter()
            && (name[2] == QLatin1Char('/') || name[2] == QLatin1Char('\\')))
        name[1] = QLatin1Char(':');
    return name;
}

// A path with its symbolic links followed: a missing file's as its folder's.
QString realPath(const QString &path) {
    const QFileInfo info(path);
    const QString real = info.canonicalFilePath();
    if (!real.isEmpty())
        return real;
    const QString folder = QFileInfo(info.absolutePath()).canonicalFilePath();
    return folder.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : folder + "/" + info.fileName();
}

bool inside(const QString &path, const QString &folder) {
    return !folder.isEmpty() && (path.compare(folder, PathCase) == 0 || path.startsWith(folder + "/", PathCase));
}

bool isFont(const QString &path) {
    static const QStringList kinds = {"ttf", "otf", "ttc", "otc", "woff", "woff2"};
    return kinds.contains(QFileInfo(path).suffix().toLower());
}

// A path a document may not take (task scripting 02's file access): to the
// log, and to its author's messages while a display's document is reported
// (Runtime::Reporting).
void refused(const QString &problem) {
    qWarning().noquote() << "[ui] a path refused:" << problem;
    if (!reportedDocument.isEmpty())
        //% "The display document %1: a path refused: %2."
        displayMessage(true, qtTrId("displays.document.path").arg(reportedDocument, problem));
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

    // A document's paths by the documents' rule (task scripting 02's file
    // access, the user's 2026-11-03): down from the document's folder or
    // from a root (::ShapeDisplays::roots), no `..`; a picture's from its
    // asset's texture folder (the user: as its shapes' textures). RmlUi
    // joins its own results again: an absolute path passes where documents
    // may read (Runtime::reads). Refused, the path is empty.
    void JoinPath(Rml::String &translated, const Rml::String &documentPath, const Rml::String &path) override {
        const QString file = fromRmlPath(path);
        translated.clear();
        if (file.isEmpty())
            return;
        if (QDir::isAbsolutePath(file)) {
            if (running != nullptr && running->reads(file))
                translated = path;
            else
                refused(QString("%1: outside the folders documents may read").arg(file));
            return;
        }
        const QString document = fromRmlPath(documentPath);
        QString holder = document;
        if (::ShapeDisplays::isPicture(file)) {
            const QString pictures = ::ShapeDisplays::picturesFolder(document);
            if (!pictures.isEmpty())
                holder = pictures + "/.rml";
        }
        QString problem;
        const QString joined = SimisDocument::resolve(file, holder, ::ShapeDisplays::roots(document), &problem,
                                                      SimisDocument::Rule::Documents);
        if (joined.isEmpty())
            refused(problem);
        translated = joined.toStdString();
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String &message) override {
        switch (type) {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:
        case Rml::Log::LT_WARNING:
            qWarning().noquote() << "[ui]" << fromRml(message);
            if (!reportedDocument.isEmpty())
                //% "The display document %1: %2"
                displayMessage(type != Rml::Log::LT_WARNING,
                               qtTrId("displays.document.message").arg(reportedDocument, fromRml(message)));
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

// Files by their paths as RmlUi joins them (System::JoinPath), only where
// documents may read (Runtime::reads: also Lua's context:LoadDocument and a
// style sheet's @font-face, which are not joined); fonts only from TSRE's
// fonts folder (the user's 2026-11-03: routes' and trainsets' own fonts are
// a design of their own).
class Files : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String &path) override {
        const QString name = fromRmlPath(path);
        // A path refused when joined comes empty: told then.
        if (name.isEmpty())
            return 0;
        if (running == nullptr || !running->reads(name)) {
            refused(QString("%1: outside the folders documents may read").arg(name));
            return 0;
        }
        if (isFont(name) && !inside(realPath(name), realPath(running->folder() + "/fonts"))) {
            refused(QString("%1: fonts are read from TSRE's fonts folder only").arg(name));
            return 0;
        }
        auto file = std::make_unique<QFile>(name);
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
    // Fonts only from TSRE's fonts folder and contexts only TSRE's (task
    // scripting 02's file access): rmlui's LoadFontFace and CreateContext
    // gone (rmlui's methods are the global table LuaRmlUi).
    lua_State *L = running->state->lua();
    lua_getglobal(L, "LuaRmlUi");
    if (lua_istable(L, -1))
        for (const char *name : {"LoadFontFace", "CreateContext"}) {
            lua_pushnil(L);
            lua_setfield(L, -2, name);
        }
    lua_pop(L, 1);
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

Runtime::Reporting::Reporting(const QString &document) : previous(reportedDocument) {
    reportedDocument = document;
}

Runtime::Reporting::~Reporting() {
    reportedDocument = previous;
}

Script::State &Runtime::lua() {
    return *state;
}

bool Runtime::loadFont(const QString &path, bool fallback) {
    return Rml::LoadFontFace(path.toStdString(), fallback);
}

bool Runtime::reads(const QString &path) const {
    const QString file = realPath(path);
    if (inside(file, realPath(uiFolder)) || (!Game::root.isEmpty() && inside(file, realPath(Game::root))))
        return true;
    return std::any_of(readable.cbegin(), readable.cend(),
                       [&file](const QString &folder) { return inside(file, folder); });
}

void Runtime::loadsFrom(const QString &document) {
    const QString game = ::ShapeDisplays::roots(document).game;
    const QString folder = realPath(game.isEmpty() ? QFileInfo(document).absolutePath() : game);
    if (!readable.contains(folder))
        readable.push_back(folder);
}

void Runtime::setCursorHandler(std::function<void(const QString &)> handler) {
    interfaces->system.cursor = std::move(handler);
}

}
