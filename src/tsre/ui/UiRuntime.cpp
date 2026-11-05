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
#include <functional>
#include <vector>
#include <cstdio>
#include <lua.h>

namespace Ui {

namespace {

Runtime *running = nullptr;
// The display's document whose RmlUi messages are reported (Reporting).
QString reportedDocument;
// In Runtime::loadFont: a font file may be opened (Files).
bool loadingFont = false;

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
// documents may read (Runtime::reads: also Lua's context:LoadDocument,
// which is not joined); a font file only while TSRE loads it
// (Runtime::loadFont; the user's 2026-11-03: TSRE loads every font, its
// own and later content's, documents name the family), so no style sheet's
// @font-face and no Lua loads one.
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
        if (isFont(name) && !loadingFont) {
            refused(QString("%1: fonts are loaded by TSRE only (a document names the family)").arg(name));
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
    // The UI's Lua collects generationally (task scripting 04): each frame
    // hands it short-lived userdata with finalizers (elements, events),
    // which Lua's incremental collector, paced by the bytes allocated, fell
    // behind on: a long drive filled the state's 64 MB. TSRE_UI_GC=inc
    // gives the incremental collector back, to compare.
    if (qEnvironmentVariable("TSRE_UI_GC") != "inc")
        lua_gc(runtime->state->lua(), LUA_GCGEN);
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
    // Fonts and contexts only TSRE's (task scripting 02's file access):
    // rmlui's LoadFontFace and CreateContext gone (rmlui's methods are the
    // global table LuaRmlUi).
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

void Runtime::collectIfGrown() {
    constexpr size_t CollectAt = size_t(16) << 20;
    if (running == nullptr || running->lua().memoryUsed() < CollectAt)
        return;
    const size_t before = running->lua().memoryUsed();
    QElapsedTimer timer;
    timer.start();
    // Twice: a userdata with a finalizer goes on the second collection.
    lua_gc(running->lua().lua(), LUA_GCCOLLECT, 0);
    lua_gc(running->lua().lua(), LUA_GCCOLLECT, 0);
    static const bool logged = qEnvironmentVariableIntValue("TSRE_UI_MEMORY_LOG") > 0;
    if (logged)
        qInfo().noquote() << QString("[ui] Lua collected: %1 KB to %2 KB in %3 ms")
                                     .arg(before / 1024).arg(running->lua().memoryUsed() / 1024)
                                     .arg(timer.nsecsElapsed() / 1e6, 0, 'f', 2);
}

void Runtime::logMemory() {
    static const int every = qEnvironmentVariableIntValue("TSRE_UI_MEMORY_LOG");
    static QElapsedTimer clock;
    if (every <= 0 || running == nullptr || (clock.isValid() && clock.elapsed() < every * 1000LL))
        return;
    clock.start();
    lua_State *L = running->lua().lua();
    const auto entries = [L](int table) {
        int count = 0;
        lua_pushnil(L);
        while (lua_next(L, table) != 0) {
            ++count;
            lua_pop(L, 1);
        }
        return count;
    };
    const int stack = lua_gettop(L);
    const int registry = entries(LUA_REGISTRYINDEX);
    lua_pushglobaltable(L);
    const int globals = entries(lua_gettop(L));
    lua_pop(L, 1);
    // TSRE_UI_MEMORY_CENSUS: what stays after full collections, and the
    // tables reachable from the registry two levels down by their entries,
    // the largest first (the collections hide how far the collector lags).
    const size_t counted = running->lua().memoryUsed();
    static const bool census = qEnvironmentVariableIsSet("TSRE_UI_MEMORY_CENSUS");
    if (!census) {
        qInfo().noquote() << QString("[ui] Lua memory: counted %1 KB, stack %2, registry %3, globals %4")
                                     .arg(counted / 1024).arg(stack).arg(registry).arg(globals);
        return;
    }
    lua_gc(L, LUA_GCCOLLECT, 0);
    lua_gc(L, LUA_GCCOLLECT, 0);
    const auto keyName = [L](int index) {
        if (lua_type(L, index) == LUA_TSTRING)
            return QString::fromUtf8(lua_tostring(L, index));
        if (lua_type(L, index) == LUA_TNUMBER)
            return QString("[%1]").arg(lua_tointeger(L, index));
        return QString("<%1>").arg(lua_typename(L, lua_type(L, index)));
    };
    std::vector<std::pair<int, QString>> tables;
    const std::function<void(int, const QString &, int)> walk = [&](int table, const QString &path, int depth) {
        lua_pushnil(L);
        while (lua_next(L, table) != 0) {
            if (lua_type(L, -1) == LUA_TTABLE) {
                const QString name = path + "." + keyName(-2);
                tables.push_back({entries(lua_gettop(L)), name});
                if (depth < 2)
                    walk(lua_gettop(L), name, depth + 1);
            }
            lua_pop(L, 1);
        }
    };
    walk(LUA_REGISTRYINDEX, "registry", 1);
    std::sort(tables.begin(), tables.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList largest;
    for (size_t i = 0; i < tables.size() && i < 8; ++i)
        largest << QString("%1=%2").arg(tables[i].second).arg(tables[i].first);
    const QString onStack = stack > 0 ? QString::fromLatin1(lua_typename(L, lua_type(L, 1)))
                                                + (lua_istable(L, 1) ? QString("(%1)").arg(entries(1)) : QString())
                                      : QString("-");
    qInfo().noquote() << QString("[ui] Lua memory: counted %1 KB, after full collections %2 KB, stack %3 (%4), "
                                 "registry %5, globals %6; largest tables %7")
                                 .arg(counted / 1024).arg(running->lua().memoryUsed() / 1024)
                                 .arg(stack).arg(onStack).arg(registry).arg(globals).arg(largest.join(' '));
}

bool Runtime::loadFont(const QString &path, bool fallback) {
    loadingFont = true;
    const bool loaded = Rml::LoadFontFace(path.toStdString(), fallback);
    loadingFont = false;
    return loaded;
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
