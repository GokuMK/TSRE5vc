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
#include <QDir>
#include <QFile>
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

void checkOpenGl(TokenTest::Suite &test, const QString &path, const QString &extraPath) {
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
                "\tESD_TSREDisplay ( Document ( \"c.rml\" ) Image ( \"Other.ace\" ) Surface ( glass ) )\r\n)\r\n";
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
    in.trains[0].extra.insert("number", "46140");
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
            text << row.time + " " + row.train + " " + row.origin + ">" + row.destination + " " + row.platform
                            + (row.delay.isEmpty() ? QString() : " +" + row.delay);
        return text.join(", ");
    };
    const Ui::DisplayDocument::Data ten = Ui::RouteDisplays::data(Input::Platform, nearA, tracks.db, &in, nullptr, 36000);
    test.check(ten.station == "Alpha" && ten.platform == "1"
                       && rows(ten.departures) == "10:00 R 1 >Beta 1, 10:12 R 2 Beta>Beta 1"
                       && rows(ten.arrivals) == "10:10 R 2 Beta>Beta 1, 11:00 R 3 > 1",
               "a platform's departures and arrivals at 10:00, by time: " + rows(ten.departures) + " | "
                       + rows(ten.arrivals));
    test.check(ten.departures.size() == 2 && ten.departures[0].extra.value("number") == "46140"
                       && ten.departures[0].extra.size() == 1 && ten.departures[1].extra.isEmpty(),
               "a row has its train's extras (#tsre.extra.<key>)");
    // Timetable trains ending at Alpha: IR 4 booked to leave it (two times:
    // it runs on beyond the route), a departure there without a
    // destination; R 5 one time, an arrival only.
    Sim::SimulationInput onward;
    onward.trains.resize(2);
    onward.trains[0].service = "IR 4";
    onward.trains[0].timetableTrain = true;
    onward.trains[0].startTime = 36000;
    onward.trains[0].stops = {stop(3, -1, 36000), stop(1, 37000, 37120)};
    onward.trains[1].service = "R 5";
    onward.trains[1].timetableTrain = true;
    onward.trains[1].startTime = 36000;
    onward.trains[1].stops = {stop(3, -1, 36000), stop(1, 37200, 37200)};
    const Ui::DisplayDocument::Data runsOn = Ui::RouteDisplays::data(Input::Platform, nearA, tracks.db, &onward, nullptr, 36000);
    test.check(rows(runsOn.departures) == "10:18 IR 4 Beta> 1" && rows(runsOn.arrivals) == "10:16 IR 4 Beta> 1, 10:20 R 5 Beta> 1",
               "a timetable's train booked to leave its last stop a departure there: " + rows(runsOn.departures) + " | "
                       + rows(runsOn.arrivals));
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
    const Ui::DisplayDocument::Data station = Ui::RouteDisplays::data(Input::Station, nearB, tracks.db, &in, nullptr, 36000);
    const Ui::DisplayDocument::Data clock = Ui::RouteDisplays::data(Input::Clock, nearB, tracks.db, &in, nullptr, 36000);
    test.check(station.station == "Beta" && station.platform.isEmpty() && station.departures.isEmpty()
                       && rows(station.arrivals) == "10:16 R 1 Alpha> 2, 10:33 R 2 Beta> 2"
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

    checkSdBlocks(test, temporary.path());
    checkRouteData(test);
    checkRedraws(test);
    checkOpenGl(test, path, extraPath);
    checkRhi(test, path);

    Ui::Runtime::stop();
    RhiContext::shutdown();
    return test.finish();
}
