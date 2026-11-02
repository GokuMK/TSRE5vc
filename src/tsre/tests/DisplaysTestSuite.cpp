#include <tsre/tests/DisplaysTestSuite.h>
#include <tsre/tests/TokenTestSupport.h>
#include <tsre/renderer/OpenGL3Renderer.h>
#include <tsre/renderer/UiDrawList.h>
#include <tsre/renderer/rhi/RhiContext.h>
#include <tsre/renderer/rhi/RhiTextures.h>
#include <tsre/renderer/rhi/RhiUiDrawer.h>
#include <tsre/shape/SFileDocument.h>
#include <tsre/shape/ShapeDisplays.h>
#include <tsre/sim/SimulationFrame.h>
#include <tsre/sim/SimulationInput.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRitem.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TSection.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/ui/UiDisplay.h>
#include <tsre/ui/UiRouteDisplays.h>
#include <tsre/ui/UiRuntime.h>
#include <tsre/Game.h>

#include <RmlUi/Core.h>
#include <rhi/qrhi.h>
#include <QCoreApplication>
#include <QScopedValueRollback>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QThread>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QTemporaryDir>
#include <array>
#include <cstdlib>
#include <limits>

namespace {

// 64 x 32: red, its top left quarter blue, a bound colour at the bottom
// right (16 x 8).
const char *const Document = R"(<rml>
<head>
<style>
body, div { display: block; }
body { margin: 0; width: 64px; height: 32px; background-color: #ff0000; }
div { position: absolute; }
#corner { left: 0; top: 0; width: 32px; height: 16px; background-color: #0000ff; }
#value { left: 48px; top: 24px; width: 16px; height: 8px; }
</style>
</head>
<body data-model="sample"><div id="corner"></div><div id="value" data-style-background-color="colour"></div></body>
</rml>)";

// The same layout bound to the data model `display`: the corner a train's
// extra by key, the bound colour green while a key it has not reads as
// empty text, magenta once it has it.
const char *const ExtraDocument = R"(<rml>
<head>
<style>
body, div { display: block; }
body { margin: 0; width: 64px; height: 32px; background-color: #ff0000; }
div { position: absolute; }
#corner { left: 0; top: 0; width: 32px; height: 16px; }
#value { left: 48px; top: 24px; width: 16px; height: 8px; }
</style>
</head>
<body data-model="display"><div id="corner" data-style-background-color="departures[0].extra.colour"></div><div id="value" data-style-background-color="departures[0].extra.other == '' ? '#00ff00' : '#ff00ff'"></div></body>
</rml>)";

// The same for a car's display: the corner its train's field `operator`
// (a C++ keyword: bound by name), the bound colour green while its next
// station is Alpha and its stops are Beta, left, and Alpha, magenta else.
const char *const TrainDocument = R"(<rml>
<head>
<style>
body, div { display: block; }
body { margin: 0; width: 64px; height: 32px; background-color: #ff0000; }
div { position: absolute; }
#corner { left: 0; top: 0; width: 32px; height: 16px; }
#value { left: 48px; top: 24px; width: 16px; height: 8px; }
</style>
</head>
<body data-model="display"><div id="corner" data-style-background-color="train.operator"></div><div id="value" data-style-background-color="train.next == 'Alpha' &amp;&amp; train.stops.size == 2 &amp;&amp; train.stops[0].passed &amp;&amp; !train.stops[1].passed &amp;&amp; train.stops[1].station == 'Alpha' ? '#00ff00' : '#ff00ff'"></div></body>
</rml>)";

// TSRE's functions (step 10), eight cells of 8 x 8 along the top, each
// green when its expressions give what they should, magenta else; drawn at
// 14:00 on Sunday 2026-11-01, a row booked at 14:25, 5 minutes late, with
// no platform. The cycle's cell magenta at 14:00:04 (drawn again: the clock
// is in its expression).
const char *const FunctionsDocument = R"(<rml>
<head>
<style>
body, div { display: block; }
body { margin: 0; width: 64px; height: 32px; background-color: #ff0000; }
div { position: absolute; top: 0; width: 8px; height: 8px; }
</style>
</head>
<body data-model="display">
<div style="left: 0px;" data-style-background-color="(departures[0].time | format_time('h:mm AP')) == '2:25 PM' ? '#00ff00' : '#ff00ff'"></div>
<div style="left: 8px;" data-style-background-color="(date | format_date('dd.MM.yyyy')) == '01.11.2026' &amp;&amp; (date | format_date('dddd', 'pl')) == 'niedziela' ? '#00ff00' : '#ff00ff'"></div>
<div style="left: 16px;" data-style-background-color="(day | pad(2)) == '01' &amp;&amp; year == 2026 &amp;&amp; month == 11 &amp;&amp; clock == 50400 ? '#00ff00' : '#ff00ff'"></div>
<div style="left: 24px;" data-style-background-color="(departures[0].platform | default('-')) == '-' &amp;&amp; (departures[0].id | default('-')) == 'T1' ? '#00ff00' : '#ff00ff'"></div>
<div style="left: 32px;" data-style-background-color="(departures[0].time | add_minutes(departures[0].delay)) == '14:30' &amp;&amp; ('23:58' | add_minutes(5)) == '00:03' ? '#00ff00' : '#ff00ff'"></div>
<div style="left: 40px;" data-style-background-color="(departures[0].time | minutes_until(clock)) == 25 &amp;&amp; ('13:59' | minutes_until(clock)) == -1 ? '#00ff00' : '#ff00ff'"></div>
<div style="left: 48px;" data-style-background-color="(clock | cycle(4, true, false)) &amp;&amp; (clock | cycle(9, 'a', 'b', 'c')) == 'c' ? '#00ff00' : '#ff00ff'"></div>
<div style="left: 56px;" data-style-background-color="(departures[0].platform | format_time('HH:mm')) == '' &amp;&amp; (day | pad) == '' &amp;&amp; cycle(4, 'a', 'b') == '' ? '#00ff00' : '#ff00ff'"></div>
</body>
</rml>)";

// The same layout: the corner a picture from the route's TEXTURES (blue),
// the bound colour set by Lua on `displayupdate` (green before noon,
// magenta after).
const char *const PictureDocument = R"(<rml>
<head>
<style>
body, div, img { display: block; }
body { margin: 0; width: 64px; height: 32px; background-color: #ff0000; }
img, div { position: absolute; }
#corner { left: 0; top: 0; width: 32px; height: 16px; }
#value { left: 48px; top: 24px; width: 16px; height: 8px; background-color: #000000; }
</style>
</head>
<body data-model="display" ondisplayupdate="document:GetElementById('value').style['background-color'] = event.parameters.time >= 43200 and '#ff00ff' or '#00ff00'"><img id="corner" src="blue.png"/><div id="value"></div></body>
</rml>)";

using Pixel = std::array<int, 4>;

bool looks(const Pixel &pixel, int r, int g, int b) {
    return std::abs(pixel[0] - r) <= 40 && std::abs(pixel[1] - g) <= 40 && std::abs(pixel[2] - b) <= 40
            && pixel[3] >= 215;
}

QString text(const Pixel &pixel) {
    return QString("(%1 %2 %3 %4)").arg(pixel[0]).arg(pixel[1]).arg(pixel[2]).arg(pixel[3]);
}

// A pixel of RGBA8 rows, tightly packed, row 0 first.
Pixel at(const QByteArray &rows, int width, int x, int y) {
    const auto *p = reinterpret_cast<const unsigned char *>(rows.constData()) + (size_t(y) * width + x) * 4;
    return {p[0], p[1], p[2], p[3]};
}

// What the document shows where, the texture's first row its top.
bool laidOut(const QByteArray &rows, int width, int r, int g, int b, QString &seen) {
    const Pixel corner = at(rows, width, 4, 4), right = at(rows, width, 40, 4), low = at(rows, width, 4, 28),
                value = at(rows, width, 60, 28);
    seen = text(corner) + " " + text(right) + " " + text(low) + " " + text(value);
    return looks(corner, 0, 0, 255) && looks(right, 255, 0, 0) && looks(low, 255, 0, 0) && looks(value, r, g, b);
}

struct Sample {
    Rml::String colour = "#00ff00";
    Rml::DataModelHandle handle;
};

bool bind(Ui::Display &display, Sample &sample) {
    if (display.context() == nullptr)
        return false;
    Rml::DataModelConstructor constructor = display.context()->CreateDataModel("sample");
    if (!constructor)
        return false;
    constructor.Bind("colour", &sample.colour);
    sample.handle = constructor.GetModelHandle();
    return true;
}

// Level 0 (or another level) of a GL texture read through a framebuffer.
QByteArray readGl(QOpenGLExtraFunctions *f, unsigned int texture, int level, int width, int height) {
    GLuint framebuffer = 0;
    GLint previous = 0;
    f->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    f->glGenFramebuffers(1, &framebuffer);
    f->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, level);
    QByteArray rows(width * height * 4, 0);
    f->glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rows.data());
    f->glBindFramebuffer(GL_FRAMEBUFFER, GLuint(previous));
    f->glDeleteFramebuffers(1, &framebuffer);
    return rows;
}

void checkOpenGl(TokenTest::Suite &test, const QString &path, const QString &extraPath, const QString &trainPath,
                 const QString &functionsPath, const QString &picturePath) {
    QOpenGLContext context;
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    context.setFormat(format);
    QOffscreenSurface surface;
    if (!context.create()) {
        test.check(false, "OpenGL: an offscreen context");
        return;
    }
    surface.setFormat(context.format());
    surface.create();
    if (!context.makeCurrent(&surface)) {
        test.check(false, "OpenGL: an offscreen context");
        return;
    }
    QOpenGLExtraFunctions *f = context.extraFunctions();
    {
        OpenGL3Renderer renderer;
        Ui::Display display(64, 32);
        Sample sample;
        const bool loaded = bind(display, sample) && display.load(path);
        display.update();
        const unsigned int texture = display.draw(renderer);
        QString seen;
        const bool first = texture != 0 && laidOut(readGl(f, texture, 0, 64, 32), 64, 0, 255, 0, seen);
        test.check(loaded && first, "OpenGL: the document drawn into a texture, its top row first: " + seen);

        // Its mipmaps: level 1 blue at its top left, red to the right.
        const QByteArray half = readGl(f, texture, 1, 32, 16);
        test.check(texture != 0 && looks(at(half, 32, 2, 2), 0, 0, 255) && looks(at(half, 32, 20, 2), 255, 0, 0),
                   "OpenGL: its mipmaps made: " + text(at(half, 32, 2, 2)) + " " + text(at(half, 32, 20, 2)));

        sample.colour = "#ffff00";
        sample.handle.DirtyVariable("colour");
        display.update();
        const unsigned int again = display.draw(renderer);
        const bool changed = again == texture && laidOut(readGl(f, texture, 0, 64, 32), 64, 255, 255, 0, seen);
        test.check(changed, "OpenGL: a bound colour changed, drawn again into the same texture: " + seen);
        display.release(renderer);
        test.check(display.texture() == 0 && !f->glIsTexture(texture), "OpenGL: the texture freed");
    }
    {
        // A board's rows: `row.extra.<key>`, a key a train has not empty.
        OpenGL3Renderer renderer;
        Ui::DisplayDocument document(extraPath, QSize(64, 32), 0.0f, ::ShapeDisplays::Display::Input::Platform);
        Ui::DisplayDocument::Data data;
        data.departures.resize(1);
        data.departures[0].extra.insert("colour", "#0000ff");
        const QDateTime now(QDate(2026, 11, 1), QTime(12, 0));
        document.draw(renderer, now, data);
        QString seen;
        const bool missing = document.texture() != 0
                && laidOut(readGl(f, document.texture(), 0, 64, 32), 64, 0, 255, 0, seen);
        data.departures[0].extra.insert("other", "x");
        document.draw(renderer, now, data);
        QString seenAgain;
        const bool set = document.texture() != 0
                && laidOut(readGl(f, document.texture(), 0, 64, 32), 64, 255, 0, 255, seenAgain);
        test.check(missing && set, "OpenGL: a row's train's extras by key (row.extra.colour), a key it has not empty: "
                                           + seen + " | " + seenAgain);
        document.release(renderer);
    }
    {
        // Out of sight a while (RouteDisplays::frame, displayKeepUnseen):
        // its texture freed, its document still loaded and due at once,
        // drawn into a new texture as before. Not seen yet: never out of
        // sight.
        OpenGL3Renderer renderer;
        Ui::DisplayDocument document(extraPath, QSize(64, 32), 0.0f, ::ShapeDisplays::Display::Input::Platform);
        Ui::DisplayDocument::Data data;
        data.departures.resize(1);
        data.departures[0].extra.insert("colour", "#0000ff");
        const QDateTime now(QDate(2026, 11, 1), QTime(12, 0));
        const bool unseen = !document.unseenFor(0);
        document.see(1, 10.0f);
        document.draw(renderer, now, data);
        const unsigned int first = document.texture();
        QThread::msleep(30);
        const bool aWhile = document.unseenFor(20) && !document.unseenFor(10000);
        document.see(2, 10.0f);
        const bool seenAgain = !document.unseenFor(20);
        document.release(renderer);
        const bool freed = document.texture() == 0 && !f->glIsTexture(first) && !document.unloaded()
                && document.due();
        document.draw(renderer, now, data);
        QString seen;
        const bool drawn = document.texture() != 0 && !document.due()
                && laidOut(readGl(f, document.texture(), 0, 64, 32), 64, 0, 255, 0, seen);
        test.check(first != 0 && unseen && aWhile && seenAgain && freed && drawn,
                   QString("OpenGL: a document out of sight: its texture freed, the document kept, drawn again "
                           "(%1 %2 %3 %4 %5): ").arg(unseen).arg(aWhile).arg(seenAgain).arg(freed).arg(drawn) + seen);
        document.release(renderer);
    }
    {
        // A car's display (Train): its train's data, a field, its stops.
        OpenGL3Renderer renderer;
        Ui::DisplayDocument document(trainPath, QSize(64, 32), 0.0f, ::ShapeDisplays::Display::Input::Train);
        Ui::DisplayDocument::Data data;
        data.train.next = "Alpha";
        data.train.fields[TrainFields::Operator] = "#0000ff";
        data.train.stops = {{"Beta", "2", "", "10:00", true}, {"Alpha", "1", "10:10", "", false}};
        const QDateTime now(QDate(2026, 11, 1), QTime(12, 0));
        document.draw(renderer, now, data);
        QString seen, seenAgain;
        const bool at = document.texture() != 0 && laidOut(readGl(f, document.texture(), 0, 64, 32), 64, 0, 255, 0, seen);
        data.train.next = "Beta";
        document.draw(renderer, now, data);
        const bool on = document.texture() != 0
                && laidOut(readGl(f, document.texture(), 0, 64, 32), 64, 255, 0, 255, seenAgain);
        test.check(at && on, "OpenGL: a car's display draws its train (train.operator, train.next, train.stops): "
                                     + seen + " | " + seenAgain);
        document.release(renderer);
    }
    {
        // TSRE's functions: each cell green.
        OpenGL3Renderer renderer;
        Ui::DisplayDocument document(functionsPath, QSize(64, 32), 0.0f, ::ShapeDisplays::Display::Input::Platform);
        Ui::DisplayDocument::Data data;
        data.departures.resize(1);
        data.departures[0].time = "14:25";
        data.departures[0].id = "T1";
        data.departures[0].delay = "5";
        document.draw(renderer, QDateTime(QDate(2026, 11, 1), QTime(14, 0)), data);
        const QStringList names = {"format_time", "format_date", "pad, year, month, clock", "default", "add_minutes",
                                   "minutes_until", "cycle", "empty in, wrong arguments"};
        QStringList failed;
        const QByteArray rows = document.texture() != 0 ? readGl(f, document.texture(), 0, 64, 32) : QByteArray();
        for (int cell = 0; cell < names.size(); ++cell)
            if (rows.isEmpty() || !looks(at(rows, 64, cell * 8 + 4, 4), 0, 255, 0))
                failed << names[cell] + (rows.isEmpty() ? QString() : " " + text(at(rows, 64, cell * 8 + 4, 4)));
        document.draw(renderer, QDateTime(QDate(2026, 11, 1), QTime(14, 0, 4)), data);
        const QByteArray later = document.texture() != 0 ? readGl(f, document.texture(), 0, 64, 32) : QByteArray();
        if (later.isEmpty() || !looks(at(later, 64, 6 * 8 + 4, 4), 255, 0, 255))
            failed << "cycle at 14:00:04" + (later.isEmpty() ? QString() : " " + text(at(later, 64, 6 * 8 + 4, 4)));
        test.check(failed.isEmpty(), "OpenGL: TSRE's functions in a document's expressions (format_time, format_date, "
                                     "pad, default, add_minutes, minutes_until, cycle, the next turn drawn again); "
                                     "failed: " + failed.join(", "));
        document.release(renderer);
    }
    {
        // A route's document: its picture from the route's TEXTURES (drawn
        // again until TexLib has loaded it), its Lua on `displayupdate`.
        // TexLib uploads for the backend the app runs (QRhi by default).
        const QScopedValueRollback<QString> backend(Game::renderBackend, QString("opengl"));
        OpenGL3Renderer renderer;
        Ui::DisplayDocument document(picturePath, QSize(64, 32), 0.0f, ::ShapeDisplays::Display::Input::None);
        const QDateTime afternoon(QDate(2026, 11, 1), QTime(14, 0));
        document.draw(renderer, afternoon, Ui::DisplayDocument::Data());
        int draws = 1;
        QElapsedTimer waited;
        waited.start();
        while (waited.elapsed() < 10000 && document.waitsForPictures()) {
            QThread::msleep(50);
            QCoreApplication::processEvents();
            if (document.due()) {
                document.draw(renderer, afternoon, Ui::DisplayDocument::Data());
                ++draws;
            }
        }
        QString seen;
        const bool drawn = document.texture() != 0
                && laidOut(readGl(f, document.texture(), 0, 64, 32), 64, 255, 0, 255, seen);
        test.check(drawn && !document.waitsForPictures() && !document.due(), QString("OpenGL: a picture from the route's TEXTURES (drawn %1 times "
                                                     "until loaded), the Lua's displayupdate: ").arg(draws) + seen);
        document.release(renderer);
    }
    context.doneCurrent();
}

void checkRhi(TokenTest::Suite &test, const QString &path) {
    // On OpenGL (core.rendering.rhiApi=opengl) QRhi takes GLSL 330 shaders,
    // TSRE's, only from a core profile context; the default format the app
    // sets asks for none.
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    RhiContext *rhiContext = RhiContext::instance();
    QRhi *rhi = rhiContext != nullptr ? rhiContext->rhi() : nullptr;
    if (rhi == nullptr || rhi->backend() == QRhi::Null) {
        qInfo().noquote() << "[tests:displays] no QRhi to draw with: its checks skipped";
        return;
    }
    const QString backend = QString::fromLatin1(rhi->backendName());
    Ui::Display display(64, 32);
    Sample sample;
    const bool loaded = bind(display, sample) && display.load(path);
    const unsigned int handle = RhiTextures::createTarget(64, 32);
    QRhiTexture *texture = RhiTextures::texture(handle);
    std::unique_ptr<QRhiTextureRenderTarget> target(
            texture != nullptr ? rhi->newTextureRenderTarget({QRhiColorAttachment(texture)}) : nullptr);
    std::unique_ptr<QRhiRenderPassDescriptor> pass(target ? target->newCompatibleRenderPassDescriptor() : nullptr);
    if (target) {
        target->setRenderPassDescriptor(pass.get());
        target->create();
    }
    auto drawer = std::make_unique<RhiUiDrawer>(rhi);
    // Drawn as the QRhi renderer draws one (drawUiTextures), then read back.
    const auto draw = [&](QByteArray &rows) {
        display.update();
        UiDrawList &list = display.render();
        QRhiCommandBuffer *cb = nullptr;
        if (!target || rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
            return false;
        if (QRhiResourceUpdateBatch *textures = RhiTextures::takeUpdates())
            cb->resourceUpdate(textures);
        QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
        drawer->prepare(list, batch, pass.get(), QSize(64, 32), [](int, bool &) { return nullptr; }, true);
        cb->beginPass(target.get(), QColor::fromRgbF(0.0f, 0.0f, 0.0f, 0.0f), {1.0f, 0}, batch);
        drawer->record(cb);
        cb->endPass();
        QRhiResourceUpdateBatch *mipmaps = rhi->nextResourceUpdateBatch();
        mipmaps->generateMips(texture);
        cb->resourceUpdate(mipmaps);
        rhi->endOffscreenFrame();
        return RhiTextures::readBack(handle, rows) == RhiTextures::Contents::Rgba8;
    };
    QByteArray rows;
    QString seen;
    const bool first = draw(rows) && laidOut(rows, 64, 0, 255, 0, seen);
    test.check(loaded && first, "QRhi " + backend + ": the document drawn into a texture, its top row first: " + seen);
    sample.colour = "#ffff00";
    sample.handle.DirtyVariable("colour");
    const bool changed = draw(rows) && laidOut(rows, 64, 255, 255, 0, seen);
    test.check(changed, "QRhi " + backend + ": a bound colour changed, drawn again: " + seen);
    drawer.reset();
    target.reset();
    pass.reset();
    RhiTextures::release(handle);
}

// A shape's .sd blocks (ShapeDisplays::read): a texture replaced and a
// square, a block without a document left out, a surface only where given;
// both MSTS headers; the same from the parsed .sd (the complex backend's);
// the documents' folders.
void checkSdBlocks(TokenTest::Suite &test, const QString &root) {
    const QString shapes = root + "/ROUTES/R/SHAPES";
    QDir().mkpath(shapes);
    const auto writeSd = [](const QString &path, const QString &header) {
        QFile file(path);
        file.open(QIODevice::WriteOnly);
        QString text = header + "\r\n\r\nShape ( d.s\r\n\tESD_Detail_Level ( 0 )\r\n"
                "\tESD_TSREDisplay (\r\n\t\tDocument ( \"a.rml\" )\r\n\t\tImage ( \"Screen.ACE\" )\r\n"
                "\t\tRefreshRate ( 1 )\r\n\t\tInput ( Platform )\r\n\t)\r\n"
                "\tESD_TSREDisplay (\r\n\t\tDocument ( \"b.rml\" )\r\n\t\tShapeHierarchy ( \"MAIN\" )\r\n"
                "\t\tPosition ( -0.2 0.4 0.001 )\r\n\t\tRotation ( 8.4 -90 0 )\r\n\t\tSize ( 1.389 0.67 0 )\r\n"
                "\t\tResolution ( 512 256 )\r\n\t\tSurface ( Paper )\r\n\t)\r\n"
                "\tESD_TSREDisplay ( Image ( \"x.ace\" ) )\r\n"
                "\tESD_TSREDisplay ( Document ( \"c.rml\" ) Image ( \"Other.ace\" ) Surface ( glass ) Input ( train ) )\r\n)\r\n";
        QByteArray bytes("\xff\xfe", 2);
        bytes += QByteArray(reinterpret_cast<const char *>(text.utf16()), text.size() * 2);
        file.write(bytes);
    };
    // Every value, to compare the two readers.
    const auto values = [](const QVector<ShapeDisplays::Display> &displays) {
        QStringList out;
        for (const ShapeDisplays::Display &d : displays)
            out << QString("%1 %2 %3 %4,%5,%6 %7,%8,%9 %10,%11,%12 %13x%14 %15 %16 %17 %18")
                           .arg(d.document, d.image, d.hierarchy).arg(d.position[0]).arg(d.position[1])
                           .arg(d.position[2]).arg(d.rotation[0]).arg(d.rotation[1]).arg(d.rotation[2])
                           .arg(d.size[0]).arg(d.size[1]).arg(d.size[2]).arg(d.resolution[0]).arg(d.resolution[1])
                           .arg(int(d.surface)).arg(double(d.refreshRate)).arg(int(d.input)).arg(d.square);
        return out.join(" | ");
    };
    bool read = true, parsed = true;
    QString seen;
    for (const QString &header : {QString("SIMISA@@@@@@@@@@JINX0t1t______"), QString("SIMISA@@@@@@@@@@JINX0s1t______")}) {
        writeSd(shapes + "/d.sd", header);
        const QVector<ShapeDisplays::Display> displays = ShapeDisplays::read(shapes + "/d.s");
        seen += QString(" %1").arg(displays.size());
        SFileDetail::Document document;
        const QVector<ShapeDisplays::Display> fromTree = document.read(shapes + "/d.sd")
                ? ShapeDisplays::read(document.root) : QVector<ShapeDisplays::Display>();
        parsed = parsed && !displays.isEmpty() && values(fromTree) == values(displays);
        if (values(fromTree) != values(displays))
            qWarning() << "[tests:displays] parsed .sd:" << values(fromTree) << "read:" << values(displays);
        read = read && displays.size() == 3;
        if (displays.size() != 3)
            continue;
        const ShapeDisplays::Display &a = displays[0], &b = displays[1], &c = displays[2];
        read = read && a.document == "a.rml" && a.image == "Screen.ACE" && !a.square && a.refreshRate == 1.0f
                && a.input == ShapeDisplays::Display::Input::Platform && a.surface == ShapeDisplays::Display::Surface::Shape
                && c.document == "c.rml" && c.surface == ShapeDisplays::Display::Surface::Glass
                && c.input == ShapeDisplays::Display::Input::Train
                && b.document == "b.rml" && b.square && b.hierarchy == "MAIN" && std::abs(b.position[0] + 0.2f) < 1e-5f
                && std::abs(b.rotation[0] - 8.4f) < 1e-5f && std::abs(b.rotation[1] + 90.0f) < 1e-5f
                && std::abs(b.size[0] - 1.389f) < 1e-5f && b.size[2] == 0.0f && b.resolution[0] == 512
                && b.resolution[1] == 256 && b.surface == ShapeDisplays::Display::Surface::Paper
                && b.refreshRate == 0.0f && b.input == ShapeDisplays::Display::Input::None;
    }
    test.check(read, "the .sd's displays read, a texture and a square (both headers):" + seen);
    test.check(parsed, "the parsed .sd's displays the same (the complex backend's)");
    test.check(ShapeDisplays::documentsFolder("/g/ROUTES/PRUDNIK/SHAPES/x.s") == "/g/ROUTES/PRUDNIK/DISPLAYS"
                       && ShapeDisplays::documentsFolder("/g/TRAINS/TRAINSET/CD130/CABVIEW3D/c.s")
                               == "/g/TRAINS/TRAINSET/CD130/DISPLAYS"
                       && ShapeDisplays::documentsFolder("/g/GLOBAL/SHAPES/y.s").isEmpty(),
               "documents in the route's or the trainset's DISPLAYS folder");
    test.check(ShapeDisplays::picturesFolder("/g/ROUTES/PRUDNIK/DISPLAYS/odjazdy.rml") == "/g/ROUTES/PRUDNIK/TEXTURES"
                       && ShapeDisplays::picturesFolder("/g/ROUTES/PRUDNIK/DISPLAYS/boards/a.rml")
                               == "/g/ROUTES/PRUDNIK/TEXTURES"
                       && ShapeDisplays::picturesFolder("/g/TRAINS/TRAINSET/KS_EN75/DISPLAYS/en75.rml")
                               == "/g/TRAINS/TRAINSET/KS_EN75"
                       && ShapeDisplays::picturesFolder("/g/appdata/ui/hud.rml").isEmpty()
                       && ShapeDisplays::isPicture("a/Logo.PNG") && ShapeDisplays::isPicture("x.ace")
                       && !ShapeDisplays::isPicture("z.lua") && !ShapeDisplays::isPicture("s.rcss"),
               "a document's pictures in its asset's texture folder (the route's TEXTURES, the trainset's own)");
}

// Two platform lines on two tracks (TdbEditingTestSuite's way: straight
// 10 m sections), Alpha's and Beta's: 5 m to 25 m along each.
class Database : public TDB {
public:
    using TDB::TDB;
    using TDB::addItemToTrNode;
};

struct Tracks {
    TSectionDAT definitions{false, false};
    TSection straight{10, 0, 10.0f, 0.0f};
    Database db{&definitions, false};
    int nodes[2] = {-1, -1};
    Tracks() {
        definitions.sekcja[10] = &straight;
        line(0, 0.0f, 1, "Alpha", "1");
        line(1, 200.0f, 3, "Beta", "2");
    }
    // A place on a track's node (x, z, MSTS's).
    bool place(int node, float distance, double *out) {
        float p[7];
        if (!db.getDrawPositionOnTrNode(p, node, distance))
            return false;
        out[0] = double(p[5]) * 2048.0 + p[0];
        out[1] = double(p[6]) * 2048.0 + p[2];
        return true;
    }
    void line(int index, float at, int id, const QString &station, const QString &platform) {
        float position[3] = {at, 0.0f, at};
        float frame[3] = {0.0f, 0.0f, 0.0f};
        int ends[2] = {0, 1};
        int last = db.newTrack(0, 0, position, frame, ends, 20, 10, 100 + id * 10);
        const int vector = db.trackNodes[last]->pins[0].link;
        for (int i = 1; i < 3; ++i)
            last = db.appendTrack(last, ends, 20, 10, 100 + id * 10 + i);
        nodes[index] = vector;
        for (int k = 0; k < 2; ++k) {
            TRitem *item = TRitem::newPlatformItem(id + k, k == 0 ? 5.0f : 25.0f);
            item->platformTrItemData[1] = unsigned(id + 1 - k);
            item->stationName = station;
            item->platformName = platform;
            float p[7];
            db.getDrawPositionOnTrNode(p, vector, item->getTrackPosition());
            item->trItemRData = new float[5]{p[0], p[1], p[2], p[5], p[6]};
            db.trackItems[id + k] = item;
            db.iTRitems = std::max(db.iTRitems, id + k + 1);
            db.addItemToTrNode(vector, id + k);
        }
    }
};

// A route's displays (step 4): the platform line nearest a display along
// its track, within its reach; the departures and arrivals at a platform
// and a station from the trains' plans, a stop passed left out by its
// booked time or by the train's state, the delays.
void checkRouteData(TokenTest::Suite &test) {
    Tracks tracks;
    Ui::RouteDisplays &displays = Ui::RouteDisplays::instance();
    double a[2], b[2];
    const bool placed = tracks.place(tracks.nodes[0], 15.0f, a) && tracks.place(tracks.nodes[1], 15.0f, b);
    const Ui::RouteDisplays::Platform nearA = displays.nearestPlatform(tracks.db, a[0] + 2.0, a[1]);
    const Ui::RouteDisplays::Platform nearB = displays.nearestPlatform(tracks.db, b[0], b[1] - 3.0);
    const Ui::RouteDisplays::Platform none = displays.nearestPlatform(tracks.db, a[0] - 500.0, a[1] - 500.0);
    test.check(placed && nearA.start == 1 && nearA.end == 2 && nearA.station == "Alpha" && nearA.name == "1"
                       && nearB.start == 3 && nearB.station == "Beta" && none.start == -1,
               QString("the nearest platform line along its track, none beyond 300 m: %1 %2 %3")
                       .arg(nearA.start).arg(nearB.start).arg(none.start));

    // R 1 leaves Alpha at 10:00 for Beta; R 2 comes from Beta at 10:10 and
    // leaves at 10:12 for Beta; R 3 ends at Alpha at 11:00, from elsewhere.
    Sim::SimulationInput in;
    auto stop = [](int item, int arrival, int departure) {
        Sim::StopPlan s;
        s.platformItem = item;
        s.arrival = arrival;
        s.departure = departure;
        return s;
    };
    in.trains.resize(3);
    in.trains[0].service = "R 1";
    in.trains[0].fields[TrainFields::Number] = "46140";
    in.trains[0].extra.insert("kind", "aglo");
    in.trains[0].startTime = 36000;
    in.trains[0].stops = {stop(1, -1, 36000), stop(3, 37000, -1)};
    in.trains[1].service = "R 2";
    in.trains[1].startTime = 34000;
    in.trains[1].stops = {stop(4, -1, 35000), stop(2, 36600, 36720), stop(3, 38000, -1)};
    in.trains[2].service = "R 3";
    in.trains[2].startTime = 38000;
    in.trains[2].stops = {stop(1, 39600, -1)};
    using Input = ::ShapeDisplays::Display::Input;
    auto rows = [](const QVector<Ui::DisplayDocument::Row> &list) {
        QStringList text;
        for (const Ui::DisplayDocument::Row &row : list)
            text << row.time + " " + row.id + " " + row.fields[TrainFields::Origin] + ">"
                            + row.fields[TrainFields::Destination] + " " + row.platform
                            + (row.delay.isEmpty() ? QString() : " +" + row.delay);
        return text.join(", ");
    };
    const Ui::DisplayDocument::Data ten = Ui::RouteDisplays::data(Input::Platform, nearA, tracks.db, &in, nullptr, 36000);
    test.check(ten.station == "Alpha" && ten.platform == "1"
                       && rows(ten.departures) == "10:00 R 1 >Beta 1, 10:12 R 2 Beta>Beta 1"
                       && rows(ten.arrivals) == "10:10 R 2 Beta>Beta 1, 11:00 R 3 > 1",
               "a platform's departures and arrivals at 10:00, by time: " + rows(ten.departures) + " | "
                       + rows(ten.arrivals));
    test.check(ten.departures.size() == 2 && ten.departures[0].fields[TrainFields::Number] == "46140"
                       && ten.departures[0].extra.value("kind") == "aglo" && ten.departures[0].extra.size() == 1
                       && ten.departures[1].fields[TrainFields::Number].isEmpty() && ten.departures[1].extra.isEmpty(),
               "a row has its train's fields (#tsre.<field>) and extras (#tsre.extra.<key>)");
    // Timetable trains ending at Alpha: IR 4 booked to leave it (two times:
    // it runs on beyond the route), a departure there, its own destination
    // Opole; R 5 one time, an arrival only, its own origin Delta.
    Sim::SimulationInput onward;
    onward.trains.resize(2);
    onward.trains[0].service = "IR 4";
    onward.trains[0].timetableTrain = true;
    onward.trains[0].startTime = 36000;
    onward.trains[0].stops = {stop(3, -1, 36000), stop(1, 37000, 37120)};
    onward.trains[0].fields[TrainFields::Destination] = "Opole";
    onward.trains[1].service = "R 5";
    onward.trains[1].timetableTrain = true;
    onward.trains[1].startTime = 36000;
    onward.trains[1].stops = {stop(3, -1, 36000), stop(1, 37200, 37200)};
    onward.trains[1].fields[TrainFields::Origin] = "Delta";
    const Ui::DisplayDocument::Data runsOn = Ui::RouteDisplays::data(Input::Platform, nearA, tracks.db, &onward, nullptr, 36000);
    test.check(rows(runsOn.departures) == "10:18 IR 4 Beta>Opole 1"
                       && rows(runsOn.arrivals) == "10:16 IR 4 Beta>Opole 1, 10:20 R 5 Delta> 1",
               "a timetable's train booked to leave its last stop a departure there; a train's own origin and "
               "destination before its stops': " + rows(runsOn.departures) + " | " + rows(runsOn.arrivals));
    const Ui::DisplayDocument::Data later = Ui::RouteDisplays::data(Input::Platform, nearA, tracks.db, &in, nullptr, 36700);
    test.check(rows(later.departures) == "10:12 R 2 Beta>Beta 1" && rows(later.arrivals) == "11:00 R 3 > 1",
               "without a simulation, a stop passed by its booked time: " + rows(later.departures) + " | "
                       + rows(later.arrivals));
    // At 10:16:40, R 1 gone on, R 2 still standing at Alpha (4 minutes
    // late leaving), R 3 on its way.
    Sim::SimulationFrame frame;
    frame.trains.resize(3);
    frame.trains[0].state.status = Sim::TrainStatus::Running;
    frame.trains[0].state.nextStop = 1;
    frame.trains[1].state.status = Sim::TrainStatus::Stopped;
    frame.trains[1].state.stoppedAt = Sim::StopKind::Station;
    frame.trains[1].state.nextStop = 2;
    frame.trains[2].state.status = Sim::TrainStatus::Running;
    const Ui::DisplayDocument::Data standing = Ui::RouteDisplays::data(Input::Platform, nearA, tracks.db, &in, &frame, 37000);
    test.check(rows(standing.departures) == "10:12 R 2 Beta>Beta 1 +4" && rows(standing.arrivals) == "11:00 R 3 > 1",
               "with a simulation, by the trains' states: a train standing late, one arrived, one gone: "
                       + rows(standing.departures) + " | " + rows(standing.arrivals));
    // A car's train (Train): R 2 from Beta by Alpha to Beta, its own
    // destination Opole. Without a simulation no state, its first stop
    // next; waiting to start; standing at Alpha 4 minutes late; running on
    // to Beta, 1000 m away, then 300 m (approaching); its stops, those left
    // marked; no train, nothing.
    in.trains[1].fields[TrainFields::Line] = "S1";
    in.trains[1].fields[TrainFields::Destination] = "Opole";
    in.trains[1].stops[2].front = 2000.0;
    const auto train = [&](const Sim::SimulationFrame *state, double now) {
        const Ui::DisplayDocument::Train t = Ui::RouteDisplays::trainData(1, tracks.db, &in, state, now).train;
        QStringList stops;
        for (const Ui::DisplayDocument::Stop &s : t.stops)
            stops << s.station + (s.passed ? "*" : "");
        return QString("%1 %2>%3 [%4] at %5 next %6 prev %7 p%8 dep %9 arr %10 d%11 +%12 %13 (%14)")
                .arg(t.id, t.fields[TrainFields::Origin], t.fields[TrainFields::Destination], t.state, t.at, t.next,
                     t.previous, t.platform, t.departs)
                .arg(t.arrives, t.distance, t.delay, t.fields[TrainFields::Line], stops.join(" "));
    };
    Sim::SimulationFrame waiting = frame, running = frame;
    waiting.trains[1].state.status = Sim::TrainStatus::Waiting;
    waiting.trains[1].state.nextStop = 0;
    running.trains[1].state.status = Sim::TrainStatus::Running;
    running.trains[1].state.front = 1000.0;
    Sim::SimulationFrame closing = running;
    closing.trains[1].state.front = 1700.0;
    const QStringList seen = {train(nullptr, 34000), train(&waiting, 34100), train(&frame, 37000),
                              train(&running, 37000), train(&closing, 37000)};
    const QStringList expected = {
        "R 2 Beta>Opole [] at  next Beta prev  p2 dep 09:43 arr  d + S1 (Beta Alpha Beta)",
        "R 2 Beta>Opole [waiting] at  next Beta prev  p2 dep 09:43 arr  d +1 S1 (Beta Alpha Beta)",
        "R 2 Beta>Opole [standing] at Alpha next Beta prev Beta p1 dep 10:12 arr 10:33 d +4 S1 (Beta* Alpha Beta)",
        "R 2 Beta>Opole [running] at  next Beta prev Alpha p2 dep  arr 10:33 d1000 + S1 (Beta* Alpha* Beta)",
        "R 2 Beta>Opole [approaching] at  next Beta prev Alpha p2 dep  arr 10:33 d300 + S1 (Beta* Alpha* Beta)"};
    const Ui::DisplayDocument::Train standingTrain = Ui::RouteDisplays::trainData(1, tracks.db, &in, &frame, 37000).train;
    const bool stopTimes = standingTrain.stops.size() == 3 && standingTrain.stops[1].platform == "1"
            && standingTrain.stops[1].arrives == "10:10" && standingTrain.stops[1].departs == "10:12"
            && standingTrain.stops[0].arrives.isEmpty() && standingTrain.stops[2].departs.isEmpty();
    const Ui::DisplayDocument::Train noTrain = Ui::RouteDisplays::trainData(7, tracks.db, &in, &frame, 37000).train;
    test.check(seen == expected && stopTimes && noTrain.id.isEmpty(),
               "a car's train: its own destination before its stops', its state, where it stands, its next and "
               "previous stops, their booked times, the distance, its delay, its fields, its stops (left marked, "
               "their platforms and times): " + seen.join(" | "));
    const Ui::DisplayDocument::Data station = Ui::RouteDisplays::data(Input::Station, nearB, tracks.db, &in, nullptr, 36000);
    const Ui::DisplayDocument::Data clock = Ui::RouteDisplays::data(Input::Clock, nearB, tracks.db, &in, nullptr, 36000);
    test.check(station.station == "Beta" && station.platform.isEmpty() && station.departures.isEmpty()
                       && rows(station.arrivals) == "10:16 R 1 Alpha> 2, 10:33 R 2 Beta>Opole 2"
                       && clock.departures.isEmpty() && clock.arrivals.isEmpty(),
               "a station's lists (none for a clock): " + rows(station.departures) + " | " + rows(station.arrivals));
}

// The redraws of a frame (step 5): at most the budget and one load, never
// drawn first, then the longest overdue, a farther display as if due later.
void checkRedraws(TokenTest::Suite &test) {
    using Due = Ui::RouteDisplays::Due;
    const qint64 never = std::numeric_limits<qint64>::max() / 2;
    const std::vector<Due> due = {{100, 0.0f, false}, {never, 300.0f, true}, {500, 10.0f, false},
                                  {1500, 200.0f, false}, {never, 20.0f, true}};
    const std::vector<int> two = Ui::RouteDisplays::redraws(due, 2);
    const std::vector<int> all = Ui::RouteDisplays::redraws(due, 10);
    test.check(two == std::vector<int>{4, 2} && all == std::vector<int>{4, 2, 0, 3},
               "a frame's redraws: the budget, one load (the nearer), the overdue, the nearer");
}

}

int TsreTests::runDisplaysSuite(bool verbose) {
    TokenTest::Suite test{"[tests:displays]", verbose};
    const QString folder = QDir::current().filePath(QString("appdata/") + Game::AppDataVersion + "/ui");
    const bool started = Ui::Runtime::start(folder);
    test.check(started, "RmlUi starts with TSRE's UI folder");
    if (!started)
        return test.finish();
    QTemporaryDir temporary;
    const QString path = temporary.filePath("display.rml");
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(Document);
    file.close();
    const QString extraPath = temporary.filePath("extra.rml");
    QFile extraFile(extraPath);
    extraFile.open(QIODevice::WriteOnly);
    extraFile.write(ExtraDocument);
    extraFile.close();
    const QString trainPath = temporary.filePath("train.rml");
    QFile trainFile(trainPath);
    trainFile.open(QIODevice::WriteOnly);
    trainFile.write(TrainDocument);
    trainFile.close();
    const QString functionsPath = temporary.filePath("functions.rml");
    QFile functionsFile(functionsPath);
    functionsFile.open(QIODevice::WriteOnly);
    functionsFile.write(FunctionsDocument);
    functionsFile.close();
    // A route's document and its picture, in their folders.
    QDir(temporary.path()).mkpath("ROUTES/R/DISPLAYS");
    QDir(temporary.path()).mkpath("ROUTES/R/TEXTURES");
    QImage blue(8, 8, QImage::Format_RGBA8888);
    blue.fill(QColor(0, 0, 255));
    blue.save(temporary.filePath("ROUTES/R/TEXTURES/blue.png"));
    const QString picturePath = temporary.filePath("ROUTES/R/DISPLAYS/picture.rml");
    QFile pictureFile(picturePath);
    pictureFile.open(QIODevice::WriteOnly);
    pictureFile.write(PictureDocument);
    pictureFile.close();

    checkSdBlocks(test, temporary.path());
    checkRouteData(test);
    checkRedraws(test);
    checkOpenGl(test, path, extraPath, trainPath, functionsPath, picturePath);
    checkRhi(test, path);

    Ui::Runtime::stop();
    RhiContext::shutdown();
    return test.finish();
}
