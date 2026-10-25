#include "RmlUiTestSuite.h"
#include "TokenTestSupport.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <cmath>
#include <map>
#include <tsre/Game.h>
#include <tsre/script/ScriptState.h>
#include <tsre/ui/UiRuntime.h>

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
