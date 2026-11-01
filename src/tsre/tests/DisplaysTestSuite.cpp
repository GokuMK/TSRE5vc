#include <tsre/tests/DisplaysTestSuite.h>
#include <tsre/tests/TokenTestSupport.h>
#include <tsre/renderer/OpenGL3Renderer.h>
#include <tsre/renderer/UiDrawList.h>
#include <tsre/renderer/rhi/RhiContext.h>
#include <tsre/renderer/rhi/RhiTextures.h>
#include <tsre/renderer/rhi/RhiUiDrawer.h>
#include <tsre/shape/ShapeDisplays.h>
#include <tsre/ui/UiDisplay.h>
#include <tsre/ui/UiRuntime.h>
#include <tsre/Game.h>

#include <RmlUi/Core.h>
#include <rhi/qrhi.h>
#include <QDir>
#include <QFile>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QTemporaryDir>
#include <array>
#include <cstdlib>

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

void checkOpenGl(TokenTest::Suite &test, const QString &path) {
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
// square, a block without a document left out; both MSTS headers; the
// documents' folders.
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
                "\tESD_TSREDisplay ( Image ( \"x.ace\" ) )\r\n)\r\n";
        QByteArray bytes("\xff\xfe", 2);
        bytes += QByteArray(reinterpret_cast<const char *>(text.utf16()), text.size() * 2);
        file.write(bytes);
    };
    bool read = true;
    QString seen;
    for (const QString &header : {QString("SIMISA@@@@@@@@@@JINX0t1t______"), QString("SIMISA@@@@@@@@@@JINX0s1t______")}) {
        writeSd(shapes + "/d.sd", header);
        const QVector<ShapeDisplays::Display> displays = ShapeDisplays::read(shapes + "/d.s");
        seen += QString(" %1").arg(displays.size());
        read = read && displays.size() == 2;
        if (displays.size() != 2)
            continue;
        const ShapeDisplays::Display &a = displays[0], &b = displays[1];
        read = read && a.document == "a.rml" && a.image == "Screen.ACE" && !a.square && a.refreshRate == 1.0f
                && a.input == ShapeDisplays::Display::Input::Platform && a.surface == ShapeDisplays::Display::Surface::Glass
                && b.document == "b.rml" && b.square && b.hierarchy == "MAIN" && std::abs(b.position[0] + 0.2f) < 1e-5f
                && std::abs(b.rotation[0] - 8.4f) < 1e-5f && std::abs(b.rotation[1] + 90.0f) < 1e-5f
                && std::abs(b.size[0] - 1.389f) < 1e-5f && b.size[2] == 0.0f && b.resolution[0] == 512
                && b.resolution[1] == 256 && b.surface == ShapeDisplays::Display::Surface::Paper
                && b.refreshRate == 0.0f && b.input == ShapeDisplays::Display::Input::None;
    }
    test.check(read, "the .sd's displays read, a texture and a square (both headers):" + seen);
    test.check(ShapeDisplays::documentsFolder("/g/ROUTES/PRUDNIK/SHAPES/x.s") == "/g/ROUTES/PRUDNIK/DISPLAYS"
                       && ShapeDisplays::documentsFolder("/g/TRAINS/TRAINSET/CD130/CABVIEW3D/c.s")
                               == "/g/TRAINS/TRAINSET/CD130/DISPLAYS"
                       && ShapeDisplays::documentsFolder("/g/GLOBAL/SHAPES/y.s").isEmpty(),
               "documents in the route's or the trainset's DISPLAYS folder");
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

    checkSdBlocks(test, temporary.path());
    checkOpenGl(test, path);
    checkRhi(test, path);

    Ui::Runtime::stop();
    RhiContext::shutdown();
    return test.finish();
}
