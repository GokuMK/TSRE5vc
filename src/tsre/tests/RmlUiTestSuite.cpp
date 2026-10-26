#include "RmlUiTestSuite.h"
#include "TokenTestSupport.h"
#include <QDir>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QFile>
#include <QTemporaryDir>
#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <cmath>
#include <map>
#include <tsre/Game.h>
#include <tsre/script/ScriptState.h>
#include <tsre/ui/UiRuntime.h>
#include <tsre/ui/UiRenderInterface.h>
#include <tsre/ui/UiScreen.h>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <tsre/texture/AceDocument.h>
#include <tsre/texture/TexLib.h>
#include <QImage>
#include <QThread>

namespace {

// What RmlUi asks a renderer to do, kept.
class Recorder : public Rml::RenderInterface {
public:
    struct Geometry {
        std::vector<Rml::Vertex> vertices;
        std::vector<int> indices;
    };
    struct Draw {
        Rml::CompiledGeometryHandle geometry;
        Rml::Vector2f translation;
        Rml::TextureHandle texture;
    };
    struct Texture {
        Rml::Vector2i size;
        std::vector<Rml::byte> data;
    };
    std::map<Rml::CompiledGeometryHandle, Geometry> geometries;
    std::map<Rml::TextureHandle, Texture> textures;
    std::vector<Draw> draws;
    uintptr_t next = 1;

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override {
        Geometry &geometry = geometries[next];
        geometry.vertices.assign(vertices.begin(), vertices.end());
        geometry.indices.assign(indices.begin(), indices.end());
        return next++;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override {
        draws.push_back({geometry, translation, texture});
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override { geometries.erase(geometry); }
    Rml::TextureHandle LoadTexture(Rml::Vector2i &size, const Rml::String &) override {
        size = {4, 4};
        textures[next].size = size;
        return next++;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> data, Rml::Vector2i size) override {
        Texture &texture = textures[next];
        texture.size = size;
        texture.data.assign(data.begin(), data.end());
        return next++;
    }
    void ReleaseTexture(Rml::TextureHandle texture) override { textures.erase(texture); }
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};

const char *Document = R"rml(<rml>
<head>
<style>
body { font-family: "Noto Sans"; font-size: 20px; color: #ffffff; }
div, p { display: block; }
.w { display: inline-block; }
#box { width: 200px; height: 50px; background-color: #ff0000; }
#button, #spin { width: 100px; height: 40px; }
</style>
<script>
function clicked(event)
  event.current_element.inner_rml = "clicked " .. tostring(io) .. " " .. tostring(os.date ~= nil)
end
</script>
</head>
<body>
<div id="box"></div>
<span id="wide" class="w">WWWW</span>
<span id="narrow" class="w">iiii</span>
<span id="small" class="w" style="font-size: 10px;">WWWW</span>
<span id="polish" class="w">ąęłńóśźż</span>
<span id="latin" class="w">aelnoszz</span>
<span id="missing" class="w">&#x2603;&#x2603;&#x2603;&#x2603;&#x2603;&#x2603;&#x2603;&#x2603;</span>
<p id="translated">route.editor.about.window.button.browse</p>
<p id="plain">Speed 10</p>
<div id="button" onclick="clicked(event)">press</div>
<div id="spin" onclick="while true do end">spin</div>
<div data-model="sim"><span id="bound">{{speed}}</span></div>
</body>
</rml>
)rml";

float widthOf(Rml::ElementDocument *document, const char *id) {
    Rml::Element *element = document->GetElementById(id);
    return element != nullptr ? element->GetOffsetWidth() : -1.0f;
}

QString innerOf(Rml::ElementDocument *document, const char *id) {
    Rml::Element *element = document->GetElementById(id);
    return element != nullptr ? QString::fromStdString(element->GetInnerRML()) : QString("(none)");
}

// A click in the middle of the element, its handlers under one deadline.
void click(Rml::Context *context, Rml::ElementDocument *document, const char *id, int milliseconds) {
    Rml::Element *element = document->GetElementById(id);
    if (element == nullptr)
        return;
    const Rml::Vector2f at = element->GetAbsoluteOffset(Rml::BoxArea::Border)
                             + Rml::Vector2f(element->GetOffsetWidth(), element->GetOffsetHeight()) * 0.5f;
    Script::State::Deadline deadline(Ui::Runtime::get()->lua(), milliseconds);
    context->ProcessMouseMove(int(at.x), int(at.y), 0);
    context->ProcessMouseButtonDown(0, 0);
    context->ProcessMouseButtonUp(0, 0);
    context->Update();
}

}

int TsreTests::runRmlUiSuite(bool verbose) {
    TokenTest::Suite test{"[tests:rmlui]", verbose};
    const QString folder = QDir::current().filePath(QString("appdata/") + Game::AppDataVersion + "/ui");
    const bool started = Ui::Runtime::start(folder);
    test.check(started, "RmlUi starts with TSRE's UI folder");
    if (!started)
        return test.finish();

    QTemporaryDir temporary;
    const QString documentPath = temporary.filePath("test.rml");
    {
        QFile file(documentPath);
        if (file.open(QIODevice::WriteOnly))
            file.write(Document);
    }

    Recorder recorder;
    Rml::Context *context = Rml::CreateContext("tests", Rml::Vector2i(400, 300), &recorder);
    Rml::String speed = "72 km/h";
    Rml::DataModelHandle model;
    if (Rml::DataModelConstructor constructor = context->CreateDataModel("sim")) {
        constructor.Bind("speed", &speed);
        model = constructor.GetModelHandle();
    }
    Rml::ElementDocument *document = nullptr;
    {
        Script::State::Deadline deadline(Ui::Runtime::get()->lua());
        document = context->LoadDocument(documentPath.toStdString());
        if (document != nullptr)
            document->Show();
        context->Update();
    }
    test.check(document != nullptr, "a document with style, script, handlers and a data binding loads");
    if (document == nullptr) {
        Rml::RemoveContext("tests");
        Ui::Runtime::stop();
        return test.finish();
    }
    context->Render();

    // Layout and drawing.
    Rml::Element *box = document->GetElementById("box");
    const Rml::Vector2f boxAt = box->GetAbsoluteOffset(Rml::BoxArea::Border);
    bool red = false;
    for (const Recorder::Draw &draw : recorder.draws) {
        const auto found = recorder.geometries.find(draw.geometry);
        if (found == recorder.geometries.end() || found->second.vertices.empty())
            continue;
        float left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
        bool allRed = true;
        for (const Rml::Vertex &v : found->second.vertices) {
            left = std::min(left, v.position.x + draw.translation.x);
            right = std::max(right, v.position.x + draw.translation.x);
            top = std::min(top, v.position.y + draw.translation.y);
            bottom = std::max(bottom, v.position.y + draw.translation.y);
            allRed = allRed && v.colour.red == 255 && v.colour.green == 0 && v.colour.alpha == 255;
        }
        if (allRed && std::fabs(left - boxAt.x) < 0.5f && std::fabs(right - left - 200) < 0.5f
                && std::fabs(top - boxAt.y) < 0.5f && std::fabs(bottom - top - 50) < 0.5f)
            red = true;
    }
    test.check(box->GetOffsetWidth() == 200 && box->GetOffsetHeight() == 50 && red,
               "a 200 x 50 box laid out and drawn red where it is");

    // Text through Qt's fonts.
    const float wide = widthOf(document, "wide"), narrow = widthOf(document, "narrow"),
                small = widthOf(document, "small"), polish = widthOf(document, "polish"),
                latin = widthOf(document, "latin"), missing = widthOf(document, "missing");
    if (verbose)
        qInfo().noquote() << "[tests:rmlui] widths: WWWW" << wide << "iiii" << narrow << "WWWW at 10 px" << small
                          << "Polish" << polish << "plain" << latin << "missing" << missing;
    // Whole-pixel advances: up to a pixel a glyph apart.
    test.check(wide > 2 * narrow && narrow > 0 && std::fabs(small * 2 - wide) <= 4,
               "text measured by Noto Sans' glyphs: widths by letter and by size");
    test.check(std::fabs(polish - latin) <= 0.15f * latin && std::fabs(polish - missing) > 2,
               "Polish letters drawn from the font, not as missing boxes");
    int atlasCoverage = 0;
    bool textDrawn = false;
    for (const auto &texture : recorder.textures)
        for (Rml::byte b : texture.second.data)
            atlasCoverage = std::max(atlasCoverage, int(b));
    for (const Recorder::Draw &draw : recorder.draws)
        textDrawn = textDrawn || draw.texture != 0;
    test.check(atlasCoverage > 200 && textDrawn, "glyphs filled into a texture and drawn from it");

    // TSRE's translations.
    const QString close = qtTrId("route.editor.about.window.button.browse");
    test.check(innerOf(document, "translated") == close && close != "route.editor.about.window.button.browse"
                   && innerOf(document, "plain") == "Speed 10",
               QString("a translation id shows its translation (%1), other text as written").arg(close));

    // Lua handlers in the UI's state.
    click(context, document, "button", 100);
    test.check(innerOf(document, "button") == "clicked nil true",
               "a click runs the document's Lua handler, sandboxed (no io, the UI's os)");
    QElapsedTimer timer;
    timer.start();
    click(context, document, "spin", 50);
    const qint64 spun = timer.elapsed();
    document->GetElementById("button")->SetInnerRML("press");
    click(context, document, "button", 100);
    test.check(spun < 1000 && innerOf(document, "button") == "clicked nil true",
               QString("an endless handler stopped by its deadline (%1 ms); handlers run on").arg(spun));

    // Data binding.
    const bool first = innerOf(document, "bound") == "72 km/h";
    speed = "80 km/h";
    model.DirtyVariable("speed");
    context->Update();
    test.check(first && innerOf(document, "bound") == "80 km/h", "a data binding follows its value");

    // Compiled Lua never reaches Lua.
    const QString compiled = temporary.filePath("compiled.luac");
    {
        QFile file(compiled);
        if (file.open(QIODevice::WriteOnly))
            file.write(QByteArray("\x1bLuaU\0\x19\x93", 8));
    }
    Rml::FileHandle handle = Rml::GetFileInterface()->Open(compiled.toStdString());
    test.check(handle == 0, "compiled Lua is not served to documents");
    if (handle != 0)
        Rml::GetFileInterface()->Close(handle);

    // TSRE's HUD document: its labels' ids translated, its data, its Lua.
    {
        Rml::String hudSpeed = "0", hudUnit = "km/h", hudDistance = "0.0 km";
        int hudThrottle = 0, hudBrake = 0;
        Rml::Context *hud = Rml::CreateContext("hud-test", Rml::Vector2i(800, 600), &recorder);
        Rml::DataModelHandle hudModel;
        if (Rml::DataModelConstructor constructor = hud->CreateDataModel("train")) {
            constructor.Bind("speed", &hudSpeed);
            constructor.Bind("speedUnit", &hudUnit);
            constructor.Bind("distance", &hudDistance);
            constructor.Bind("throttle", &hudThrottle);
            constructor.Bind("brake", &hudBrake);
            hudModel = constructor.GetModelHandle();
        }
        Rml::ElementDocument *hudDocument = nullptr;
        {
            Script::State::Deadline deadline(Ui::Runtime::get()->lua());
            hudDocument = hud->LoadDocument((folder + "/hud.rml").toStdString());
            if (hudDocument != nullptr)
                hudDocument->Show();
            hud->Update();
        }
        auto send = [&](double speed, double time) {
            Script::State::Deadline deadline(Ui::Runtime::get()->lua());
            Rml::Dictionary parameters;
            parameters["speed"] = speed;
            parameters["distance"] = 0.0;
            parameters["throttle"] = 0.5;
            parameters["brake"] = 0.0;
            parameters["time"] = time;
            hudDocument->DispatchEvent("trainupdate", parameters);
            hud->Update();
        };
        bool rising = false, falling = false;
        QString label;
        if (hudDocument != nullptr) {
            label = QString::fromStdString(hudDocument->QuerySelector(".label")->GetInnerRML());
            send(10.0, 0.0);
            send(12.0, 1.0);
            Rml::Element *value = hudDocument->GetElementById("speedValue");
            rising = value->IsClassSet("rising") && !value->IsClassSet("falling");
            send(9.0, 2.0);
            falling = value->IsClassSet("falling") && !value->IsClassSet("rising");
        }
        test.check(hudDocument != nullptr && label == qtTrId("ui.hud.speed") && label != "ui.hud.speed",
                   QString("the HUD document loads, its labels translated (%1)").arg(label));
        test.check(rising && falling, "the HUD's Lua colours the speed by its trend from trainupdate");

        // Its CPU time a frame, every value changing every frame (the worst
        // case: its texts laid out again each frame).
        if (hudDocument != nullptr) {
            const int frames = 300;
            qint64 eventNs = 0, updateNs = 0, renderNs = 0;
            QElapsedTimer timer;
            for (int i = 0; i < frames; ++i) {
                hudSpeed = std::to_string(40 + i % 60);
                hudDistance = std::to_string(i) + ".5 km";
                hudThrottle = i % 100;
                hudBrake = (i * 7) % 100;
                for (const char *name : {"speed", "distance", "throttle", "brake"})
                    hudModel.DirtyVariable(name);
                timer.start();
                {
                    Script::State::Deadline deadline(Ui::Runtime::get()->lua());
                    Rml::Dictionary parameters;
                    parameters["speed"] = 10.0 + i % 7;
                    parameters["time"] = 10.0 + i * 0.3;
                    hudDocument->DispatchEvent("trainupdate", parameters);
                }
                eventNs += timer.nsecsElapsed();
                timer.start();
                {
                    Script::State::Deadline deadline(Ui::Runtime::get()->lua());
                    hud->Update();
                }
                updateNs += timer.nsecsElapsed();
                timer.start();
                recorder.draws.clear();
                hud->Render();
                renderNs += timer.nsecsElapsed();
            }
            const double event = eventNs / 1e3 / frames, update = updateNs / 1e3 / frames,
                         render = renderNs / 1e3 / frames;
            qInfo().noquote() << QString("[tests:rmlui] HUD CPU a frame, every value changing: event and Lua %1 us, "
                                         "update %2 us, render %3 us, all %4 us; %5 draws")
                                     .arg(event, 0, 'f', 1).arg(update, 0, 'f', 1).arg(render, 0, 'f', 1)
                                     .arg(event + update + render, 0, 'f', 1).arg(recorder.draws.size());
            test.check(event + update + render < 5000, "the HUD costs under 5 ms of CPU a frame");
        }
        Rml::RemoveContext("hud-test");

        // Every translation id TSRE's documents use is in the catalogues.
        QStringList missing;
        const QRegularExpression textId(">\\s*([a-z0-9]+(?:\\.[a-z0-9]+)+)\\s*<");
        for (const QString &name : QDir(folder).entryList({"*.rml"}, QDir::Files)) {
            QFile file(QDir(folder).filePath(name));
            if (!file.open(QIODevice::ReadOnly))
                continue;
            const QString text = QString::fromUtf8(file.readAll());
            for (auto match = textId.globalMatch(text); match.hasNext();) {
                const QString id = match.next().captured(1);
                if (qtTrId(id.toUtf8().constData()) == id)
                    missing << name + ": " + id;
            }
        }
        test.check(missing.isEmpty(), "translation ids of TSRE's documents in the catalogues" +
                                          (missing.isEmpty() ? QString() : " (missing: " + missing.join(", ") + ")"));
    }

    // Images through TexLib: ACE (plain and compressed) and PNG, their size
    // read from the header, their pixels loaded on TexLib's thread.
    {
        const int width = 48, height = 32;
        std::vector<unsigned char> pixels(size_t(width) * height * 4);
        for (size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i] = 200;
            pixels[i + 1] = 40;
            pixels[i + 2] = 40;
            pixels[i + 3] = (i / 4) % 2 ? 255 : 128;
        }
        AceWriteOptions options;
        options.encoding = AceEncoding::Rgba;
        AceDocument ace;
        QString error;
        const bool written = AceDocument::fromPixels(pixels.data(), qsizetype(pixels.size()), width, height, 4,
                                                     options, ace, error)
                && ace.write(temporary.filePath("plain.ace"), false, error)
                && ace.write(temporary.filePath("packed.ace"), true, error);
        QImage png(20, 10, QImage::Format_RGBA8888);
        png.fill(Qt::blue);
        png.save(temporary.filePath("image.png"));
        test.check(written && TexLib::imageSize(temporary.filePath("plain.ace")) == QSize(width, height)
                       && TexLib::imageSize(temporary.filePath("packed.ace")) == QSize(width, height)
                       && TexLib::imageSize(temporary.filePath("image.png")) == QSize(20, 10),
                   "image sizes read from their headers: ACE plain and compressed, PNG");

        const QString imagesPath = temporary.filePath("images.rml");
        {
            QFile file(imagesPath);
            if (file.open(QIODevice::WriteOnly))
                file.write(QString(R"(<rml><head></head><body><img id="ace" src="packed.ace"/><img id="png" src="image.png"/>)"
                                   R"(<img id="absolute" src="%1"/></body></rml>)")
                               .arg(temporary.filePath("plain.ace")).toUtf8());
        }
        Ui::RenderInterface drawing;
        Rml::Context *images = Rml::CreateContext("images-test", Rml::Vector2i(200, 100), &drawing);
        Rml::ElementDocument *imagesDocument = images->LoadDocument(imagesPath.toStdString());
        bool laidOut = false;
        size_t imageDraws = 0;
        std::vector<int> loading;
        if (imagesDocument != nullptr) {
            imagesDocument->Show();
            images->Update();
            drawing.beginFrame(200, 100);
            images->Render();
            laidOut = imagesDocument->GetElementById("ace")->GetOffsetWidth() == width
                      && imagesDocument->GetElementById("ace")->GetOffsetHeight() == height
                      && imagesDocument->GetElementById("png")->GetOffsetWidth() == 20
                      && imagesDocument->GetElementById("absolute")->GetOffsetWidth() == width;
            const UiDrawList &list = drawing.list();
            for (const UiDrawList::Draw &draw : list.draws)
                if (list.libraryTextures.count(draw.texture) != 0)
                    ++imageDraws;
            for (const auto &entry : list.libraryTextures)
                loading.push_back(entry.second);
        }
        test.check(laidOut && imageDraws == 3 && loading.size() == 3,
                   "a document's images (relative and absolute paths) laid out at their size at once and drawn "
                   "from TexLib's textures");
        // TexLib loads them on a thread of its own.
        QElapsedTimer waited;
        waited.start();
        bool loaded = false;
        while (!loaded && waited.elapsed() < 5000) {
            loaded = !loading.empty();
            for (int id : loading) {
                const auto found = TexLib::mtex.find(id);
                loaded = loaded && found != TexLib::mtex.end() && found->second->loaded;
            }
            if (!loaded)
                QThread::msleep(5);
        }
        test.check(loaded, QString("TexLib loads the images on its own thread (%1 ms)").arg(waited.elapsed()));
        // Its render interface goes with this block: RmlUi lets go of it
        // first.
        Rml::RemoveContext("images-test");
        Rml::ReleaseRenderManagers();
    }

    // Input: the router between the UI and the view.
    {
        const QString base = folder + "/base.rcss";
        const QString inputPath = temporary.filePath("input.rml");
        const QString menuPath = temporary.filePath("menu.rml");
        auto write = [](const QString &path, const QString &text) {
            QFile file(path);
            if (file.open(QIODevice::WriteOnly))
                file.write(text.toUtf8());
        };
        write(inputPath, QString(R"rml(<rml><head><link type="text/rcss" href="%1"/><style>
#panel { position: absolute; left: 100px; top: 100px; width: 200px; height: 100px; background-color: #333333; }
#bar { position: absolute; left: 0; top: 0; width: 100%; height: 20px; }
#go { position: absolute; left: 20px; top: 50px; width: 80px; height: 30px; }
#scroll { position: absolute; left: 400px; top: 100px; width: 100px; height: 60px; overflow: auto; pointer-events: auto; }
#field { position: absolute; left: 400px; top: 300px; width: 150px; height: 24px; }
</style><script>function pressed(element) element.inner_rml = "pressed" end</script></head><body>
<div id="panel"><handle id="bar" move_target="#parent"></handle><button id="go" onclick="pressed(element)">go</button></div>
<div id="scroll"><div style="height: 400px;">tall</div></div>
<input id="field" type="text"/>
</body></rml>)rml").arg(base));
        write(menuPath, QString(R"rml(<rml><head><link type="text/rcss" href="%1"/><style>
button { display: block; width: 100px; height: 30px; }
</style><script>function chose(element) element.inner_rml = "chosen" end</script></head><body>
<button id="first" onclick="chose(element)">first</button><button id="second" onclick="chose(element)">second</button>
</body></rml>)rml").arg(base));

        Ui::Screen screen;
        int taken = 0;
        screen.setKeysTaken([&] { ++taken; });
        const bool ready = screen.ready(800, 600, 1.0f, 1.0f);
        Rml::ElementDocument *page = ready ? screen.context()->LoadDocument(inputPath.toStdString()) : nullptr;
        Rml::ElementDocument *menu = ready ? screen.context()->LoadDocument(menuPath.toStdString()) : nullptr;
        test.check(ready && page != nullptr && menu != nullptr, "a screen over a view with a page and a menu");
        if (page != nullptr && menu != nullptr) {
            page->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
            screen.update();
            auto mouse = [&](QEvent::Type type, int x, int y, Qt::MouseButton button = Qt::LeftButton) {
                const Qt::MouseButtons held = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::MouseButtons(button);
                QMouseEvent event(type, QPointF(x, y), QPointF(x, y), type == QEvent::MouseMove ? Qt::NoButton : button,
                                  held, Qt::NoModifier);
                const bool took = type == QEvent::MouseButtonPress ? screen.mousePress(&event)
                                  : type == QEvent::MouseButtonRelease ? screen.mouseRelease(&event)
                                                                       : screen.mouseMove(&event);
                screen.update();
                return took;
            };
            auto key = [&](int code, const QString &text = QString(), Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
                QKeyEvent press(QEvent::KeyPress, code, modifiers, text);
                QKeyEvent release(QEvent::KeyRelease, code, modifiers, text);
                const bool took = screen.keyPress(&press);
                screen.keyRelease(&release);
                screen.update();
                return took;
            };
            auto wheel = [&](int x, int y) {
                QWheelEvent event(QPointF(x, y), QPointF(x, y), QPoint(), QPoint(0, -240), Qt::NoButton, Qt::NoModifier,
                                  Qt::NoScrollPhase, false);
                const bool took = screen.wheel(&event);
                screen.update();
                return took;
            };
            Rml::Element *panel = page->GetElementById("panel");

            // The mouse: the view's where nothing takes it.
            const bool viewPress = !mouse(QEvent::MouseButtonPress, 50, 50);
            const bool viewDragOver = !mouse(QEvent::MouseMove, 150, 110) && !mouse(QEvent::MouseMove, 160, 160);
            const bool viewRelease = !mouse(QEvent::MouseButtonRelease, 160, 160);
            test.check(viewPress && viewDragOver && viewRelease,
                       "a press where the document has nothing is the view's, its drag too, over a widget");
            // A widget dragged by its bar.
            const bool barPress = mouse(QEvent::MouseButtonPress, 150, 110);
            const bool barDrag = mouse(QEvent::MouseMove, 200, 140) && mouse(QEvent::MouseMove, 250, 160);
            const bool barRelease = mouse(QEvent::MouseButtonRelease, 250, 160);
            test.check(barPress && barDrag && barRelease && std::fabs(panel->GetAbsoluteOffset().x - 200) < 0.5f
                           && std::fabs(panel->GetAbsoluteOffset().y - 150) < 0.5f,
                       QString("a widget dragged by its handle, the drag the UI's (moved to %1, %2)")
                           .arg(panel->GetAbsoluteOffset().x).arg(panel->GetAbsoluteOffset().y));
            // A button clicked; the keys stay the view's.
            mouse(QEvent::MouseMove, 250, 215);
            const bool buttonPress = mouse(QEvent::MouseButtonPress, 250, 215);
            mouse(QEvent::MouseButtonRelease, 250, 215);
            test.check(buttonPress && innerOf(page, "go") == "pressed" && screen.keys() == Ui::Screen::Keys::View
                           && !key(Qt::Key_W) && taken == 0,
                       "a widget's button clicked, the keys still the view's");
            // The wheel: the UI's over what it scrolls.
            Rml::Element *scroll = page->GetElementById("scroll");
            const bool scrolled = wheel(450, 130) && scroll->GetScrollTop() > 0;
            test.check(scrolled && !wheel(50, 50), "the wheel scrolls a document's part, elsewhere it is the view's");

            // A text field: typing until Esc.
            mouse(QEvent::MouseButtonPress, 450, 310);
            mouse(QEvent::MouseButtonRelease, 450, 310);
            const bool typing = screen.keys() == Ui::Screen::Keys::Text && taken == 1;
            key(Qt::Key_A, "a");
            key(Qt::Key_B, "b");
            const QString typed = QString::fromStdString(page->GetElementById("field")->GetAttribute<Rml::String>("value", ""));
            key(Qt::Key_Escape);
            test.check(typing && typed == "ab" && screen.keys() == Ui::Screen::Keys::View,
                       QString("a text field clicked takes the keys for typing (\"%1\"), Esc gives them back").arg(typed));

            // A menu: every key the UI's, navigated by keys and actions.
            screen.showMenu(menu);
            const bool menuKeys = screen.keys() == Ui::Screen::Keys::Menu && taken == 2 && key(Qt::Key_W);
            const bool modalMouse = mouse(QEvent::MouseButtonPress, 700, 500);
            mouse(QEvent::MouseButtonRelease, 700, 500);
            Rml::Element *focus = screen.context()->GetFocusElement();
            const bool firstFocused = focus != nullptr && focus->GetId() == "first";
            key(Qt::Key_Down);
            focus = screen.context()->GetFocusElement();
            const bool secondFocused = focus != nullptr && focus->GetId() == "second";
            key(Qt::Key_Return);
            const bool chosen = innerOf(menu, "second") == "chosen";
            screen.action(Ui::Screen::Action::Up);
            screen.action(Ui::Screen::Action::Accept);
            const bool actions = innerOf(menu, "first") == "chosen";
            test.check(menuKeys && modalMouse, "a menu shown has every key and the mouse, the camera let go");
            test.check(firstFocused && secondFocused && chosen && actions,
                       "a menu navigated by keys (Down, Enter) and by actions (Up, Accept), as a gamepad will");
            key(Qt::Key_Escape);
            test.check(!menu->IsVisible() && screen.keys() == Ui::Screen::Keys::View, "Esc closes the menu: the keys the view's");

            // Esc with the view's keys: the game menu, when there is one.
            const bool viewEscape = !key(Qt::Key_Escape);
            bool asked = false;
            screen.setMenuHandler([&] {
                asked = true;
                screen.showMenu(menu);
                return true;
            });
            const bool gameMenu = key(Qt::Key_Escape) && asked && screen.keys() == Ui::Screen::Keys::Menu;
            key(Qt::Key_Escape);
            test.check(viewEscape && gameMenu && screen.keys() == Ui::Screen::Keys::View,
                       "Esc with the view's keys: the view's, or the game menu once there is one");
        }
    }

    // The debugger, for documents' authors.
    const size_t before = recorder.draws.size();
    recorder.draws.clear();
    context->Render();
    const size_t plain = recorder.draws.size();
    const bool debugger = Rml::Debugger::Initialise(context);
    Rml::Debugger::SetVisible(true);
    context->Update();
    recorder.draws.clear();
    context->Render();
    test.check(before > 0 && debugger && recorder.draws.size() > plain, "the debugger shows over the documents");

    Rml::RemoveContext("tests");
    Ui::Runtime::stop();
    test.check(Ui::Runtime::get() == nullptr, "RmlUi stopped");
    return test.finish();
}
