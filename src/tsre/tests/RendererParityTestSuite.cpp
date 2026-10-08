/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tests/RendererParityTestSuite.h>

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopedValueRollback>
#include <QSet>
#include <QTextStream>
#include <QTimer>
#include <QDebug>

#include <algorithm>
#include <cmath>

#include <routeEditor/RouteEditorGLWidget.h>
#include <shapeViewer/ShapeViewerGLWidget.h>
#include <tsre/camera/CameraConsist.h>
#include <tsre/camera/CameraRot.h>
#include <tsre/trains/ConLib.h>
#include <tsre/trains/EngLib.h>
#include <tsre/Game.h>
#include <tsre/world/TerrainLib.h>
#include <tsre/renderer/RenderStats.h>
#include <tsre/renderer/SelectionId.h>
#include <tsre/world/Ref.h>
#include <tsre/world/Route.h>
#include <tsre/world/TerrainLib.h>
#include <tsre/shape/ShapeLoader.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/ogl/OglObj.h>
#include <tsre/procedural/OrtsTrackProfile.h>
#include <tsre/procedural/OrtsTrackProfileRenderer.h>
#include <tsre/tdb/TSection.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>

namespace {

const char *CaptureLog = "[tests:renderer-capture]";
const char *CompareLog = "[tests:renderer-compare]";

struct SettleOptions {
    int minFrames = 20;
    int stableFrames = 4;
    int maxFrames = 600;
    int maxSeconds = 240;
    int frameIntervalMs = 20;
};

struct Thresholds {
    double maxRmse = -1.0;
    double maxDiffPixelRatio = -1.0;
    int maxPickMismatches = -1;
};

struct ViewSpec {
    QString name;
    bool hasTile = false;
    int tileX = 0;
    int tileZ = 0;
    bool hasPos = false;
    float pos[3] = {0, 0, 0};
    bool hasOffset = false;
    float offset[3] = {0, 0, 0};
    bool hasRot = false;
    float rot[2] = {0, 0};
    // Map mode (task editor 04): centred on the view's ground position.
    bool map = false;
    float metresPerPixel = 2.0f;
    float bearing = 0.0f;
    bool fadedTerrain = false;
    // Edit the distant terrain (TerrainLib's current tree) for this view.
    bool editDistant = false;
    // Height above the terrain, replacing the view's own height.
    bool hasAboveGround = false;
    float aboveGround = 0.0f;
};

// A static object placed for the capture only (not saved): a shape of the
// route's SHAPES directory, at an offset (x, height above the terrain, z)
// from the start view's position, turned by yaw degrees.
struct PlacedObject {
    QString file;
    float offset[3] = {0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;
};

struct Options {
    QVector<PlacedObject> objects;
    int width = 960;
    int height = 540;
    QString outputDir = "renderer-parity";
    SettleOptions settle;
    int pickColumns = 16;
    int pickRows = 9;
    int timingFrames = 3;
    int diffTolerance = 16;
    bool hud = false;
    // The Route Editor's FPS counter, painted over the frame.
    bool fpsHud = false;
    bool compass = true;
    bool pointer = false;
    // -1 keeps the profile setting.
    int shadows = -1;
    // Editor view toggles that have no profile setting.
    bool pivotPoints = false;
    bool snapable = false;
    Thresholds thresholds;
    // Map mode layers: an activity and a path to select, by file name.
    QString activity;
    QString path;
    QVector<ViewSpec> views;
};

struct ImageDiff {
    bool valid = false;
    double rmse = 0.0;
    qint64 diffPixels = 0;
    qint64 pixels = 0;
    int maxChannelDiff = 0;
    QImage heatmap;
};

void pumpEvents(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

QByteArray imageHash(const QImage &image) {
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    return QCryptographicHash::hash(
                QByteArray::fromRawData(reinterpret_cast<const char *>(rgb.constBits()),
                                        rgb.sizeInBytes()),
                QCryptographicHash::Sha1);
}

bool readFloatArray(const QJsonValue &value, float *out, int count) {
    const QJsonArray array = value.toArray();
    if (array.size() != count)
        return false;
    for (int i = 0; i < count; ++i)
        out[i] = static_cast<float>(array[i].toDouble());
    return true;
}

bool loadOptions(const QString &casesFile, Options &options, QString &error) {
    if (casesFile.isEmpty()) {
        ViewSpec start;
        start.name = "start";
        options.views.push_back(start);
        return true;
    }
    QFile file(casesFile);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QString("cannot open cases file %1").arg(casesFile);
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        error = QString("invalid cases JSON: %1").arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();
    options.width = root.value("width").toInt(options.width);
    options.height = root.value("height").toInt(options.height);
    options.outputDir = root.value("output").toString(options.outputDir);
    options.timingFrames = std::max(1, root.value("timingFrames").toInt(options.timingFrames));
    options.diffTolerance = root.value("diffTolerance").toInt(options.diffTolerance);
    options.hud = root.value("hud").toBool(options.hud);
    options.fpsHud = root.value("fpsHud").toBool(options.fpsHud);
    options.compass = root.value("compass").toBool(options.compass);
    options.pointer = root.value("pointer").toBool(options.pointer);
    if (root.contains("shadows"))
        options.shadows = root.value("shadows").toBool() ? 1 : 0;
    options.pivotPoints = root.value("pivotPoints").toBool(options.pivotPoints);
    options.snapable = root.value("snapable").toBool(options.snapable);

    const QJsonObject settle = root.value("settle").toObject();
    options.settle.minFrames = settle.value("minFrames").toInt(options.settle.minFrames);
    options.settle.stableFrames = settle.value("stableFrames").toInt(options.settle.stableFrames);
    options.settle.maxFrames = settle.value("maxFrames").toInt(options.settle.maxFrames);
    options.settle.maxSeconds = settle.value("maxSeconds").toInt(options.settle.maxSeconds);

    const QJsonArray pickGrid = root.value("pickGrid").toArray();
    if (pickGrid.size() == 2) {
        options.pickColumns = std::max(0, pickGrid[0].toInt());
        options.pickRows = std::max(0, pickGrid[1].toInt());
    }

    const QJsonObject thresholds = root.value("thresholds").toObject();
    options.thresholds.maxRmse = thresholds.value("maxRmse").toDouble(-1.0);
    options.thresholds.maxDiffPixelRatio = thresholds.value("maxDiffPixelRatio").toDouble(-1.0);
    options.thresholds.maxPickMismatches = thresholds.value("maxPickMismatches").toInt(-1);

    options.activity = root.value("activity").toString();
    options.path = root.value("path").toString();

    const QJsonArray objects = root.value("objects").toArray();
    for (const QJsonValue &value : objects) {
        const QJsonObject object = value.toObject();
        PlacedObject placed;
        placed.file = object.value("file").toString();
        readFloatArray(object.value("offset"), placed.offset, 3);
        placed.yaw = float(object.value("yaw").toDouble(0.0));
        if (placed.file.isEmpty()) {
            error = "placed object without a file";
            return false;
        }
        options.objects.push_back(placed);
    }

    const QJsonArray views = root.value("views").toArray();
    for (int i = 0; i < views.size(); ++i) {
        const QJsonObject object = views[i].toObject();
        ViewSpec view;
        view.name = object.value("name").toString(QString("view%1").arg(i));
        const QJsonArray tile = object.value("tile").toArray();
        if (tile.size() == 2) {
            view.hasTile = true;
            view.tileX = tile[0].toInt();
            view.tileZ = tile[1].toInt();
        }
        view.hasPos = readFloatArray(object.value("pos"), view.pos, 3);
        view.hasOffset = readFloatArray(object.value("offset"), view.offset, 3);
        view.hasRot = readFloatArray(object.value("rot"), view.rot, 2);
        view.map = object.value("mode").toString() == "map";
        view.metresPerPixel = float(object.value("metresPerPixel").toDouble(2.0));
        view.bearing = float(object.value("bearing").toDouble(0.0));
        view.fadedTerrain = object.value("fadedTerrain").toBool(false);
        view.editDistant = object.value("editDistant").toBool(false);
        view.hasAboveGround = object.contains("aboveGround");
        view.aboveGround = float(object.value("aboveGround").toDouble(0.0));
        if (view.hasTile != view.hasPos) {
            error = QString("view %1 needs both tile and pos").arg(view.name);
            return false;
        }
        options.views.push_back(view);
    }
    if (options.views.isEmpty()) {
        ViewSpec start;
        start.name = "start";
        options.views.push_back(start);
    }
    return true;
}

// Captures for one route live under <output>/<route>/<label>/.
QString routeOutputDir(const Options &options) {
    return QDir(options.outputDir).absoluteFilePath(Game::route);
}

// A label names a capture directory, so it must be a single path component.
bool validLabel(const QString &label) {
    return !label.isEmpty() && label != "." && label != ".."
            && !label.contains('/') && !label.contains('\\');
}

ImageDiff compareImages(const QImage &baselineImage, const QImage &currentImage, int tolerance) {
    ImageDiff diff;
    const QImage a = baselineImage.convertToFormat(QImage::Format_RGB32);
    const QImage b = currentImage.convertToFormat(QImage::Format_RGB32);
    if (a.size() != b.size() || a.isNull())
        return diff;
    diff.valid = true;
    diff.heatmap = QImage(a.size(), QImage::Format_RGB32);
    double squared = 0.0;
    for (int y = 0; y < a.height(); ++y) {
        const QRgb *rowA = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const QRgb *rowB = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        QRgb *out = reinterpret_cast<QRgb *>(diff.heatmap.scanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            const int dr = std::abs(qRed(rowA[x]) - qRed(rowB[x]));
            const int dg = std::abs(qGreen(rowA[x]) - qGreen(rowB[x]));
            const int db = std::abs(qBlue(rowA[x]) - qBlue(rowB[x]));
            const int maxDiff = std::max(dr, std::max(dg, db));
            squared += dr * dr + dg * dg + db * db;
            diff.maxChannelDiff = std::max(diff.maxChannelDiff, maxDiff);
            if (maxDiff > tolerance) {
                diff.diffPixels++;
                // Red: the current capture differs; brightness follows the difference.
                out[x] = qRgb(128 + maxDiff / 2, 0, 0);
            } else {
                // Dimmed baseline image keeps the context readable.
                const int gray = qGray(rowA[x]) / 4;
                out[x] = qRgb(gray, gray, gray);
            }
        }
    }
    diff.pixels = qint64(a.width()) * a.height();
    diff.rmse = std::sqrt(squared / (3.0 * diff.pixels));
    return diff;
}

QString kindName(SelectionIdCodec::Kind kind) {
    switch (kind) {
    case SelectionIdCodec::Kind::None: return "none";
    case SelectionIdCodec::Kind::WorldObject: return "world";
    case SelectionIdCodec::Kind::Terrain: return "terrain";
    case SelectionIdCodec::Kind::ActivityObject: return "activity";
    case SelectionIdCodec::Kind::DatabaseItem: return "tdb-item";
    case SelectionIdCodec::Kind::ActivityService: return "service";
    default: return "unknown";
    }
}

QString describeSelection(quint32 id) {
    const SelectionIdCodec::DecodedSelection decoded = SelectionIdCodec::decode(id);
    if (!decoded.valid)
        return QString("invalid(0x%1)").arg(id, 8, 16, QChar('0'));
    switch (decoded.kind) {
    case SelectionIdCodec::Kind::None:
        return "none";
    case SelectionIdCodec::Kind::WorldObject:
        return QString("world tile(%1,%2) obj %3 part %4")
                .arg(decoded.tileXOffset).arg(decoded.tileZOffset)
                .arg(decoded.primaryId).arg(decoded.part);
    case SelectionIdCodec::Kind::Terrain:
        return QString("terrain tile(%1,%2) patch %3")
                .arg(decoded.tileXOffset).arg(decoded.tileZOffset).arg(decoded.patchId);
    case SelectionIdCodec::Kind::DatabaseItem:
        return QString("tdb-item db %1 item %2")
                .arg(static_cast<int>(decoded.databaseKind)).arg(decoded.databaseItemId);
    default:
        return QString("%1 %2 part %3").arg(kindName(decoded.kind))
                .arg(decoded.primaryId).arg(decoded.part);
    }
}

// Same selectable thing, ignoring the sub-part.
bool sameSelectionTarget(quint32 a, quint32 b) {
    const SelectionIdCodec::DecodedSelection da = SelectionIdCodec::decode(a);
    const SelectionIdCodec::DecodedSelection db = SelectionIdCodec::decode(b);
    if (da.valid != db.valid || da.kind != db.kind)
        return false;
    return da.tileXOffset == db.tileXOffset && da.tileZOffset == db.tileZOffset
            && da.primaryId == db.primaryId && da.patchId == db.patchId
            && da.databaseKind == db.databaseKind && da.databaseItemId == db.databaseItemId;
}

QString number(double value, int precision = 2) {
    return QString::number(value, 'f', precision);
}

QJsonObject readJsonObject(const QString &path, QString &error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QString("cannot open %1").arg(path);
        return QJsonObject();
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        error = QString("invalid JSON in %1: %2").arg(path, parseError.errorString());
        return QJsonObject();
    }
    return document.object();
}

quint64 jsonCount(const QJsonObject &stats, const QString &phase, const QString &field) {
    return quint64(stats["phases"].toObject()[phase].toObject()[field].toDouble());
}

} // namespace

int TsreTests::runRendererCaptureSuite(const QString &casesFile, const QString &label,
                                        bool verbose) {
    Q_UNUSED(verbose);
    Options options;
    QString error;
    if (!loadOptions(casesFile, options, error)) {
        qWarning() << CaptureLog << error;
        return 2;
    }
    if (!validLabel(label)) {
        qWarning() << CaptureLog << "invalid capture label:" << label;
        return 2;
    }
    if (!Game::checkRoot(Game::root)) {
        qWarning() << CaptureLog << "invalid MSTS root:" << Game::root;
        return 2;
    }
    if (Game::route.trimmed().isEmpty() || !Game::checkRoute(Game::route)) {
        qWarning() << CaptureLog << "route not found:" << Game::route;
        return 2;
    }

    const QString outputDir = QDir(routeOutputDir(options)).absoluteFilePath(label);
    if (!QDir().mkpath(outputDir)) {
        qWarning() << CaptureLog << "cannot create output directory" << outputDir;
        return 2;
    }

    QScopedValueRollback<bool> restoreFpsHud(Game::editorFpsHudEnabled, options.fpsHud);
    // Animated shading (water waves) must stand still to settle.
    QScopedValueRollback<bool> restoreAnimation(Game::animationFrozen, true);
    QScopedValueRollback<bool> restoreHud(Game::hudEnabled, options.hud);
    QScopedValueRollback<bool> restoreCompass(Game::viewCompass, options.compass);
    QScopedValueRollback<bool> restorePointer(Game::viewPointer3d, options.pointer);
    QScopedValueRollback<int> restoreShadows(Game::shadowsEnabled);
    if (options.shadows >= 0)
        Game::shadowsEnabled = options.shadows > 0 ? std::max(1, Game::shadowsEnabled) : 0;
    QScopedValueRollback<bool> restorePivots(Game::showWorldObjPivotPoints, options.pivotPoints);
    QScopedValueRollback<bool> restoreSnapable(Game::viewSnapable, options.snapable);
    RenderStats::setEnabled(true);

    RouteEditorGLWidget widget;
    widget.setAttribute(Qt::WA_DontShowOnScreen);
    widget.resize(options.width, options.height);
    QElapsedTimer loadTimer;
    loadTimer.start();
    if (!widget.initRoute()) {
        qWarning() << CaptureLog << "route failed to load:" << Game::route;
        return 2;
    }
    // Traffic and animation advance with wall-clock time, which separate
    // processes do not share.
    widget.setSimulationPaused(true);
    widget.show();
    QApplication::processEvents();
    if (!widget.isValid()) {
        qWarning() << CaptureLog << "no valid OpenGL context";
        return 2;
    }
    Game::PixelRatio = widget.devicePixelRatioF();
    qInfo() << CaptureLog << label << "route" << Game::route
            << "loaded in" << loadTimer.elapsed() << "ms";

    int startTileX = 0, startTileZ = 0;
    float startPos[3];
    float startRot[2];
    widget.diagnosticView(startTileX, startTileZ, startPos, startRot[0], startRot[1]);
    for (const PlacedObject &placed : options.objects) {
        Ref::RefItem item;
        item.type = "static";
        item.filename.push_back(placed.file);
        item.currentFilename = placed.file;
        int tileX = startTileX, tileZ = startTileZ;
        float position[3] = {startPos[0] + placed.offset[0], 0.0f, startPos[2] + placed.offset[2]};
        Game::check_coords(tileX, tileZ, position);
        position[1] = Game::terrainLib->getHeight(tileX, tileZ, position[0], position[2]) + placed.offset[1];
        float rotation[4];
        float up[3] = {0.0f, 1.0f, 0.0f};
        Quat::setAxisAngle(rotation, up, placed.yaw * float(M_PI) / 180.0f);
        if (widget.currentRoute() == nullptr
                || widget.currentRoute()->placeObject(tileX, tileZ, position, rotation, 0.0f, &item) == nullptr)
            qWarning() << CaptureLog << "could not place" << placed.file;
    }

    widget.setDiagnosticActivity(options.activity, options.path);
    QJsonArray viewReports;
    for (const ViewSpec &spec : options.views) {
        int tileX = spec.hasTile ? spec.tileX : startTileX;
        int tileZ = spec.hasTile ? spec.tileZ : startTileZ;
        float pos[3] = {startPos[0], startPos[1], startPos[2]};
        float rot[2] = {startRot[0], startRot[1]};
        if (spec.hasPos)
            std::copy(spec.pos, spec.pos + 3, pos);
        if (spec.hasOffset)
            for (int i = 0; i < 3; ++i)
                pos[i] += spec.offset[i];
        if (spec.hasRot)
            std::copy(spec.rot, spec.rot + 2, rot);
        if (spec.hasAboveGround) {
            int groundX = tileX, groundZ = tileZ;
            float ground[3] = {pos[0], 0.0f, pos[2]};
            Game::check_coords(groundX, groundZ, ground);
            pos[1] = Game::terrainLib->getHeight(groundX, groundZ, ground[0], ground[2]) + spec.aboveGround;
        }
        widget.setDiagnosticView(tileX, tileZ, pos[0], pos[1], pos[2], rot[0], rot[1]);
        if (spec.map) {
            widget.setDiagnosticMapView(tileX, tileZ, pos[0], pos[2], spec.metresPerPixel,
                                        spec.bearing);
            widget.setMapLayerVisible(MapLayer::FadedTerrain, spec.fadedTerrain);
            if (spec.editDistant)
                Game::terrainLib->setDistantAsCurrent();
            else
                Game::terrainLib->setDetailedAsCurrent();
        }

        QElapsedTimer settleTimer;
        settleTimer.start();
        QByteArray lastHash;
        int stableCount = 0;
        unsigned lastShapeProgress = ShapeLoader::progress();
        int frames = 0;
        bool settled = false;
        while (frames < options.settle.maxFrames
               && settleTimer.elapsed() < qint64(options.settle.maxSeconds) * 1000) {
            pumpEvents(options.settle.frameIntervalMs);
            const QByteArray hash = imageHash(widget.grabFramebuffer());
            frames++;
            stableCount = hash == lastHash ? stableCount + 1 : 0;
            lastHash = hash;
            // A frame can repeat while shapes still load on the workers.
            const unsigned shapeProgress = ShapeLoader::progress();
            if (shapeProgress != lastShapeProgress)
                stableCount = 0;
            lastShapeProgress = shapeProgress;
            if (frames >= options.settle.minFrames && stableCount >= options.settle.stableFrames
                    && !ShapeLoader::busy()) {
                settled = true;
                break;
            }
        }
        const qint64 settleMs = settleTimer.elapsed();

        // Timing frames run without events, so they see one simulation state.
        QImage image;
        QByteArray previousHash;
        bool stable = true;
        double minCpuMs = -1.0;
        RenderStats::FrameStats stats;
        for (int i = 0; i < options.timingFrames; ++i) {
            image = widget.grabFramebuffer();
            const QByteArray hash = imageHash(image);
            if (i > 0 && hash != previousHash)
                stable = false;
            previousHash = hash;
            stats = RenderStats::lastFrame();
            if (minCpuMs < 0.0 || stats.cpuMs < minCpuMs)
                minCpuMs = stats.cpuMs;
        }

        QVector<QPoint> points;
        for (int row = 0; row < options.pickRows; ++row)
            for (int column = 0; column < options.pickColumns; ++column)
                points.push_back(QPoint((2 * column + 1) * image.width() / (2 * options.pickColumns),
                                        (2 * row + 1) * image.height() / (2 * options.pickRows)));
        const QVector<quint32> picks = widget.probeSelectionIds(points);
        // The 3D pointer under the centre, from the depth read there.
        const QVector3D pointer = widget.probePointer(QPoint(image.width() / 2, image.height() / 2));

        const QString imageName = spec.name + ".png";
        image.save(QDir(outputDir).filePath(imageName));

        QJsonArray pointList;
        QJsonArray idList;
        for (int i = 0; i < points.size(); ++i) {
            pointList.append(QJsonArray{points[i].x(), points[i].y()});
            idList.append(i < picks.size() ? double(picks[i]) : -1.0);
        }
        QJsonObject picking;
        picking["valid"] = picks.size() == points.size();
        picking["points"] = pointList;
        picking["ids"] = idList;

        QJsonObject statsJson = RenderStats::toJson(stats);
        statsJson["minCpuMs"] = minCpuMs;

        QJsonObject view;
        view["name"] = spec.name;
        view["tile"] = QJsonArray{tileX, tileZ};
        view["pos"] = QJsonArray{pos[0], pos[1], pos[2]};
        view["rot"] = QJsonArray{rot[0], rot[1]};
        view["settled"] = settled;
        view["settleFrames"] = frames;
        view["settleMs"] = double(settleTimer.elapsed());
        view["stableAcrossTimingFrames"] = stable;
        view["image"] = imageName;
        view["picking"] = picking;
        view["pointer"] = QJsonArray{pointer.x(), pointer.y(), pointer.z()};
        view["stats"] = statsJson;
        viewReports.append(view);

        qInfo().noquote() << CaptureLog << spec.name
                          << (settled ? "settled" : "did not settle") << "after" << frames
                          << "frames," << settleMs << "ms; draws" << stats.drawCalls
                          << "items created" << stats.renderItemsCreated
                          << "matrix clones" << stats.matrixClones;
    }

    QJsonObject report;
    report["route"] = Game::route;
    report["root"] = Game::root;
    report["label"] = label;
    report["generated"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    report["glRenderer"] = widget.graphicsInfo();
    report["shadowsEnabled"] = Game::shadowsEnabled;
    report["views"] = viewReports;
    QFile jsonFile(QDir(outputDir).filePath("capture.json"));
    if (!jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << CaptureLog << "cannot write" << jsonFile.fileName();
        return 2;
    }
    jsonFile.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    qInfo() << CaptureLog << "capture written to" << outputDir;

    widget.makeCurrent();
    RenderStats::setEnabled(false);
    return 0;
}

int TsreTests::runRendererCompareSuite(const QString &casesFile, const QString &baselineLabel,
                                        const QString &label, bool verbose) {
    Options options;
    QString error;
    if (!loadOptions(casesFile, options, error)) {
        qWarning() << CompareLog << error;
        return 2;
    }
    if (!validLabel(baselineLabel) || !validLabel(label) || baselineLabel == label) {
        qWarning() << CompareLog << "need two different capture labels, not"
                   << baselineLabel << "and" << label;
        return 2;
    }
    const QDir routeDir(routeOutputDir(options));
    const QJsonObject baseline = readJsonObject(
                routeDir.filePath(baselineLabel + "/capture.json"), error);
    if (baseline.isEmpty()) {
        qWarning() << CompareLog << error;
        return 2;
    }
    const QJsonObject current = readJsonObject(routeDir.filePath(label + "/capture.json"), error);
    if (current.isEmpty()) {
        qWarning() << CompareLog << error;
        return 2;
    }

    QHash<QString, QJsonObject> currentViews;
    for (const QJsonValue &value : current["views"].toArray())
        currentViews[value.toObject()["name"].toString()] = value.toObject();

    QJsonArray viewReports;
    QStringList markdownRows;
    QStringList notes;
    bool thresholdsFailed = false;
    if (baseline["shadowsEnabled"].toInt() != current["shadowsEnabled"].toInt())
        notes << "Shadow settings differ between the two captures.";

    for (const QJsonValue &baselineValue : baseline["views"].toArray()) {
        const QJsonObject l = baselineValue.toObject();
        const QString name = l["name"].toString();
        if (!currentViews.contains(name)) {
            notes << QString("%1: missing from capture %2.").arg(name, label);
            continue;
        }
        const QJsonObject g = currentViews[name];
        if (l["tile"] != g["tile"] || l["pos"] != g["pos"] || l["rot"] != g["rot"])
            notes << QString("%1: camera differs between captures.").arg(name);

        const QImage baselineImage(routeDir.filePath(baselineLabel + "/" + l["image"].toString()));
        const QImage currentImage(routeDir.filePath(label + "/" + g["image"].toString()));
        const ImageDiff diff = compareImages(baselineImage, currentImage, options.diffTolerance);
        if (!diff.valid)
            notes << QString("%1: images are missing or differ in size.").arg(name);
        if (diff.valid)
            diff.heatmap.save(routeDir.filePath(name + "-diff.png"));
        const double diffRatio = diff.pixels > 0 ? double(diff.diffPixels) / diff.pixels : 1.0;

        const QJsonObject lp = l["picking"].toObject();
        const QJsonObject gp = g["picking"].toObject();
        const QJsonArray lIds = lp["ids"].toArray();
        const QJsonArray gIds = gp["ids"].toArray();
        const QJsonArray points = lp["points"].toArray();
        const bool picksValid = lp["valid"].toBool() && gp["valid"].toBool()
                && lIds.size() == gIds.size() && lp["points"] == gp["points"];
        int exactMatches = 0;
        int targetMatches = 0;
        QJsonArray mismatches;
        if (picksValid) {
            for (int i = 0; i < lIds.size(); ++i) {
                const quint32 a = quint32(lIds[i].toDouble());
                const quint32 b = quint32(gIds[i].toDouble());
                if (a == b) {
                    exactMatches++;
                    continue;
                }
                const bool sameTarget = sameSelectionTarget(a, b);
                if (sameTarget)
                    targetMatches++;
                QJsonObject mismatch;
                mismatch["point"] = points[i];
                mismatch["baseline"] = describeSelection(a);
                mismatch["current"] = describeSelection(b);
                mismatch["sameTarget"] = sameTarget;
                mismatches.append(mismatch);
            }
        }
        const int pickMismatches = picksValid ? lIds.size() - exactMatches - targetMatches : -1;

        QStringList failures;
        if (options.thresholds.maxRmse >= 0.0 && (!diff.valid || diff.rmse > options.thresholds.maxRmse))
            failures << QString("rmse %1 > %2").arg(number(diff.rmse)).arg(options.thresholds.maxRmse);
        if (options.thresholds.maxDiffPixelRatio >= 0.0
                && diffRatio > options.thresholds.maxDiffPixelRatio)
            failures << QString("diff ratio %1 > %2").arg(number(diffRatio, 4))
                        .arg(options.thresholds.maxDiffPixelRatio);
        if (options.thresholds.maxPickMismatches >= 0
                && (pickMismatches < 0 || pickMismatches > options.thresholds.maxPickMismatches))
            failures << QString("pick mismatches %1 > %2").arg(pickMismatches)
                        .arg(options.thresholds.maxPickMismatches);
        thresholdsFailed = thresholdsFailed || !failures.isEmpty();

        const QJsonObject ls = l["stats"].toObject();
        const QJsonObject gs = g["stats"].toObject();
        if (!l["settled"].toBool() || !g["settled"].toBool())
            notes << QString("%1: did not settle (%2: %3, %4: %5); streaming or "
                             "animation may affect the comparison.")
                     .arg(name, baselineLabel).arg(l["settled"].toBool() ? "yes" : "no").arg(label)
                     .arg(g["settled"].toBool() ? "yes" : "no");
        if (!l["stableAcrossTimingFrames"].toBool() || !g["stableAcrossTimingFrames"].toBool())
            notes << QString("%1: consecutive frames differed during capture.").arg(name);
        if (jsonCount(ls, "shadow", "primitives") > 0 && jsonCount(gs, "shadow", "primitives") == 0)
            notes << QString("%1: %2 renders no shadow pass.").arg(name, label);

        QJsonObject image;
        image["valid"] = diff.valid;
        image["rmse"] = diff.rmse;
        image["diffPixels"] = double(diff.diffPixels);
        image["diffPixelRatio"] = diffRatio;
        image["maxChannelDiff"] = diff.maxChannelDiff;
        image["tolerance"] = options.diffTolerance;
        QJsonObject picking;
        picking["samples"] = lIds.size();
        picking["valid"] = picksValid;
        picking["exact"] = exactMatches;
        picking["sameTargetOtherPart"] = targetMatches;
        picking["mismatches"] = pickMismatches;
        picking["details"] = mismatches;
        QJsonObject view;
        view["name"] = name;
        view["image"] = image;
        view["picking"] = picking;
        view["stats"] = QJsonObject{{"baseline", ls}, {"current", gs}};
        QJsonArray failureList;
        for (const QString &failure : failures)
            failureList.append(failure);
        view["thresholdFailures"] = failureList;
        viewReports.append(view);

        markdownRows << QString("| %1 | %2 / %3 | %4 | %5% | %6/%7 | %8 / %9 | %10 / %11 | %12 / %13 | %14 / %15 | %16 / %17 | %18 |")
                        .arg(name)
                        .arg(l["settled"].toBool() ? "yes" : "no")
                        .arg(g["settled"].toBool() ? "yes" : "no")
                        .arg(number(diff.rmse))
                        .arg(number(diffRatio * 100.0))
                        .arg(pickMismatches).arg(lIds.size())
                        .arg(jsonCount(ls, "scene", "primitives"))
                        .arg(jsonCount(gs, "scene", "primitives"))
                        .arg(jsonCount(ls, "scene", "samples"))
                        .arg(jsonCount(gs, "scene", "samples"))
                        .arg(ls["drawCalls"].toDouble())
                        .arg(gs["drawCalls"].toDouble())
                        .arg(ls["renderItemsCreated"].toDouble())
                        .arg(gs["renderItemsCreated"].toDouble())
                        .arg(ls["matrixClones"].toDouble())
                        .arg(gs["matrixClones"].toDouble())
                        .arg(failures.isEmpty() ? "ok" : failures.join("; "));

        qInfo().noquote() << CompareLog << name << "rmse" << number(diff.rmse)
                          << "diff" << number(diffRatio * 100.0) + "%"
                          << "pick mismatches" << pickMismatches << "/" << lIds.size();
        if (verbose) {
            for (const QJsonValue &value : mismatches) {
                const QJsonObject m = value.toObject();
                const QJsonArray point = m["point"].toArray();
                qInfo().noquote() << CompareLog << "  pick" << point[0].toInt() << point[1].toInt()
                                  << baselineLabel + ":" << m["baseline"].toString()
                                  << label + ":" << m["current"].toString();
            }
        }
    }

    QJsonObject report;
    report["route"] = baseline["route"];
    report["baseline"] = baselineLabel;
    report["current"] = label;
    report["generated"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    report["glRenderer"] = QJsonObject{{"baseline", baseline["glRenderer"]},
                                       {"current", current["glRenderer"]}};
    report["views"] = viewReports;
    report["notes"] = QJsonArray::fromStringList(notes);
    QFile jsonFile(routeDir.filePath("report.json"));
    if (jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        jsonFile.write(QJsonDocument(report).toJson(QJsonDocument::Indented));

    QFile markdownFile(routeDir.filePath("report.md"));
    if (markdownFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream out(&markdownFile);
        out << "# Renderer comparison: " << baseline["route"].toString() << "\n\n";
        out << "Baseline (B): " << baselineLabel << "; current (C): " << label << ".\n\n";
        out << "GL renderer: " << current["glRenderer"].toString() << "\n\n";
        out << "Scene primitives and samples are GPU query totals for the scene phase "
               "(terrain, world, water, overlays, pointer).\n\n";
        out << "| View | Settled B / C | RMSE | Diff px | Pick mismatches "
               "| Scene prims B / C | Scene samples B / C | Draws B / C "
               "| Items created B / C | Matrix clones B / C | Thresholds |\n";
        out << "|---|---|---|---|---|---|---|---|---|---|---|\n";
        for (const QString &row : markdownRows)
            out << row << "\n";
        if (!notes.isEmpty()) {
            out << "\n## Notes\n\n";
            for (const QString &note : notes)
                out << "- " << note << "\n";
        }
    }
    qInfo() << CompareLog << "report written to" << routeDir.absolutePath();
    return thresholdsFailed ? 1 : 0;
}

namespace {

const char *ViewerCaptureLog = "[tests:shape-viewer-capture]";
const char *ViewerCompareLog = "[tests:shape-viewer-compare]";

// One item shown in the Shape Viewer: a shape, an engine or a consist.
// Paths are relative to the cases file's itemRoot, or the game root.
struct ViewerItem {
    QString name;
    QString type;
    QString path;
    QString file;
    QString textures;
    // Degrees the model is turned from the default view, and tilted
    // (positive: seen from above); shapes and profiles.
    double yaw = 0.0;
    double pitch = 0.0;
    // Camera closer than the fitted view by this factor.
    double zoom = 1.0;
    // Profiles: the track path, a preset ("straight", "curve", "long") or
    // sections, each [length in metres, radius in metres] with radius 0 for
    // a straight; a negative radius curves the other way.
    QString track = "straight";
    QVector<QPair<double, double>> sections;
};

struct ViewerOptions {
    int width = 800;
    int height = 500;
    QString outputDir = "build/shape-viewer-parity";
    int diffTolerance = 16;
    SettleOptions settle;
    // Directory item paths are relative to: the game root, or a directory
    // under it when relative.
    QString itemRoot;
    // Background colour (0..1), or the viewer's own when not set.
    QVector<float> background;
    QVector<ViewerItem> items;
};

bool loadViewerOptions(const QString &casesFile, ViewerOptions &options, QString &error) {
    QFile file(casesFile);
    if (casesFile.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        error = QString("cannot open cases file %1").arg(casesFile);
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    options.width = root.value("width").toInt(options.width);
    options.height = root.value("height").toInt(options.height);
    options.outputDir = root.value("output").toString(options.outputDir);
    options.diffTolerance = root.value("diffTolerance").toInt(options.diffTolerance);
    for (const QJsonValue &channel : root.value("background").toArray())
        options.background.push_back(float(channel.toDouble()));
    if (!options.background.isEmpty() && options.background.size() != 3) {
        error = "background needs three channels";
        return false;
    }
    // A leading $NAME takes an environment variable, for content outside the
    // game root such as the Khronos glTF sample models.
    options.itemRoot = root.value("itemRoot").toString();
    if (options.itemRoot.startsWith('$')) {
        const int end = options.itemRoot.indexOf('/');
        const QString name = options.itemRoot.mid(1, end < 0 ? -1 : end - 1);
        const QString value = qEnvironmentVariable(name.toLatin1().constData());
        if (value.isEmpty()) {
            error = QString("set %1 for the item root").arg(name);
            return false;
        }
        options.itemRoot = value + (end < 0 ? QString() : options.itemRoot.mid(end));
    }
    const QJsonObject settle = root.value("settle").toObject();
    options.settle.minFrames = settle.value("minFrames").toInt(10);
    options.settle.stableFrames = settle.value("stableFrames").toInt(3);
    options.settle.maxFrames = settle.value("maxFrames").toInt(200);
    options.settle.maxSeconds = settle.value("maxSeconds").toInt(120);
    for (const QJsonValue &value : root.value("items").toArray()) {
        const QJsonObject object = value.toObject();
        ViewerItem item;
        item.name = object.value("name").toString();
        item.type = object.value("type").toString();
        item.path = object.value("path").toString();
        item.file = object.value("file").toString();
        item.textures = object.value("textures").toString();
        item.yaw = object.value("yaw").toDouble(0.0);
        // Track is seen from above by default, so ties and ballast show.
        item.pitch = object.value("pitch").toDouble(item.type == "profile" ? 25.0 : 0.0);
        item.track = object.value("track").toString(item.track);
        item.zoom = object.value("zoom").toDouble(1.0);
        for (const QJsonValue &section : object.value("sections").toArray()) {
            const QJsonArray pair = section.toArray();
            item.sections.push_back({pair.at(0).toDouble(), pair.at(1).toDouble()});
        }
        if (item.name.isEmpty() || item.path.isEmpty()) {
            error = "every item needs a name and a path";
            return false;
        }
        options.items.push_back(item);
    }
    if (options.items.isEmpty()) {
        error = "no items in cases file";
        return false;
    }
    return true;
}

// The path a profile is built along, as track sections.
bool profilePath(const ViewerItem &item, QVector<TSection> &sections, QString &error) {
    QVector<QPair<double, double>> parts = item.sections;
    if (parts.isEmpty()) {
        if (item.track == "straight")
            parts = {{10.0, 0.0}, {10.0, 0.0}};
        else if (item.track == "curve")
            parts = {{10.0, 0.0}, {20.0, 100.0}};
        else if (item.track == "long")
            parts = {{100.0, 0.0}, {100.0, 500.0}};
        else {
            error = QString("unknown track preset %1").arg(item.track);
            return false;
        }
    }
    for (const auto &part : parts) {
        if (!(part.first > 0.0)) {
            error = "section lengths must be positive";
            return false;
        }
        if (part.second == 0.0)
            sections.append(TSection(sections.size(), 0, float(part.first), 0.0f));
        else
            sections.append(TSection(sections.size(), 1, float(part.first / part.second),
                                     float(std::abs(part.second))));
    }
    return true;
}

// The route a profile file belongs to, for its textures: the parent of a
// TRACKPROFILES directory, otherwise the file's own directory.
QString profileRoute(const QString &profileFile) {
    QDir directory = QFileInfo(profileFile).absoluteDir();
    if (directory.dirName().compare("TRACKPROFILES", Qt::CaseInsensitive) == 0)
        directory.cdUp();
    return directory.absolutePath();
}

// Builds a profile of a file along the item's path. Reports the file's
// profiles and every diagnostic; objects stay empty when nothing was built.
void buildProfile(const ViewerItem &item, const QString &file, const QString &route,
                  QVector<OglObj*> &objects, QStringList &diagnostics) {
    QStringList parseDiagnostics;
    const auto profiles = OrtsTrackProfileParser::parseFileProfiles(file, &parseDiagnostics);
    for (const QString &line : parseDiagnostics)
        diagnostics << "parse: " + line;
    QSharedPointer<OrtsTrackProfile> chosen;
    QStringList names;
    for (const auto &profile : profiles) {
        names << profile->name + (profile->valid ? "" : " (invalid)");
        if (chosen == nullptr && (item.file.isEmpty() || profile->name == item.file
                                  || profile->id == item.file))
            chosen = profile;
    }
    qInfo().noquote() << ViewerCaptureLog << item.name << "profiles in file:"
                      << (names.isEmpty() ? QString("none") : names.join(", "));
    if (chosen == nullptr) {
        diagnostics << QString("no profile %1 in %2").arg(item.file, file);
        return;
    }
    for (const QString &line : chosen->diagnostics)
        diagnostics << "profile: " + line;
    QVector<TSection> sections;
    QString error;
    if (!profilePath(item, sections, error)) {
        diagnostics << error;
        return;
    }
    QStringList buildDiagnostics;
    OrtsTrackProfileRenderer::generate(*chosen, sections, objects, route, &buildDiagnostics);
    for (const QString &line : buildDiagnostics)
        diagnostics << "build: " + line;
}

} // namespace

int TsreTests::runShapeViewerCaptureSuite(const QString &casesFile, const QString &label,
                                           bool verbose) {
    Q_UNUSED(verbose);
    ViewerOptions options;
    QString error;
    if (!loadViewerOptions(casesFile, options, error)) {
        qWarning() << ViewerCaptureLog << error;
        return 2;
    }
    if (!validLabel(label)) {
        qWarning() << ViewerCaptureLog << "invalid capture label:" << label;
        return 2;
    }
    if (!Game::checkRoot(Game::root)) {
        qWarning() << ViewerCaptureLog << "invalid MSTS root:" << Game::root;
        return 2;
    }
    const QString outputDir = QDir(options.outputDir).absoluteFilePath(label);
    if (!QDir().mkpath(outputDir)) {
        qWarning() << ViewerCaptureLog << "cannot create" << outputDir;
        return 2;
    }

    EngLib engines;
    QScopedValueRollback<EngLib*> restoreEngines(Game::currentEngLib, &engines);
    QScopedValueRollback<bool> restoreAnimation(Game::animationFrozen, true);
    QScopedValueRollback<ShapeLib*> restoreShapes(Game::currentShapeLib);

    // The same cameras as the Shape Viewer and Consist Editor windows.
    CameraRot shapeCamera;
    shapeCamera.setPos(0, 2.5, 0);
    shapeCamera.setPlayerRot(M_PI / 2.0, 0);
    CameraConsist consistCamera;
    consistCamera.setPos(-100, 2.5, 42);
    consistCamera.setPlayerRot(M_PI / 2.0, 0);

    ShapeViewerGLWidget widget;
    widget.setAttribute(Qt::WA_DontShowOnScreen);
    widget.resize(options.width, options.height);
    widget.setCamera(&shapeCamera);
    if (!options.background.isEmpty())
        widget.setBackgroundGlColor(options.background[0], options.background[1],
                                    options.background[2]);
    widget.show();
    QApplication::processEvents();
    if (!widget.isValid()) {
        qWarning() << ViewerCaptureLog << "no valid OpenGL context";
        return 2;
    }

    QJsonArray items;
    for (const ViewerItem &item : options.items) {
        // A relative item root is under the game root.
        const QDir itemRoot(options.itemRoot.isEmpty() ? Game::root
                            : QDir(Game::root).absoluteFilePath(options.itemRoot));
        const QString path = itemRoot.absoluteFilePath(item.path);
        bool shown = true;
        widget.setFrameZoom(float(item.zoom));
        // Profiles: what the parser and generator reported, and the textures
        // the generated parts use.
        QStringList diagnostics;
        QSet<int> textureIds;
        if (item.type == "shape") {
            widget.setCamera(&shapeCamera);
            widget.setMode("rot");
            // Without textures, follow the Shape Viewer window's rule.
            const QString textures = item.textures.isEmpty()
                    ? ShapeViewerGLWidget::textureDirectory(path)
                    : itemRoot.absoluteFilePath(item.textures);
            widget.showShape(path, textures);
            widget.setModelRotation(float(item.yaw * M_PI / 180.0),
                                    float(item.pitch * M_PI / 180.0));
        } else if (item.type == "profile") {
            const QString route = item.textures.isEmpty() ? profileRoute(path)
                                                          : itemRoot.absoluteFilePath(item.textures);
            QVector<OglObj*> objects;
            buildProfile(item, path, route, objects, diagnostics);
            for (OglObj *object : objects)
                if (object != NULL && object->getTexId() >= 0)
                    textureIds.insert(object->getTexId());
            widget.setCamera(&shapeCamera);
            widget.setMode("rot");
            widget.showGenerated(objects);
            widget.setModelRotation(float(item.yaw * M_PI / 180.0),
                                    float(item.pitch * M_PI / 180.0));
            shown = !objects.isEmpty();
        } else if (item.type == "eng") {
            widget.setCamera(&shapeCamera);
            widget.setMode("rot");
            widget.showEng(path, item.file);
        } else if (item.type == "consist") {
            const int id = ConLib::addCon(path, item.file);
            if (id < 0) {
                shown = false;
            } else {
                widget.setCamera(&consistCamera);
                widget.setMode("");
                widget.showCon(id);
            }
        } else {
            shown = false;
        }
        for (const QString &line : diagnostics)
            qWarning().noquote() << ViewerCaptureLog << item.name << line;
        if (!shown) {
            qWarning() << ViewerCaptureLog << item.name << "could not be shown";
            items.append(QJsonObject{{"name", item.name}, {"shown", false},
                                     {"diagnostics", QJsonArray::fromStringList(diagnostics)}});
            continue;
        }

        QElapsedTimer settleTimer;
        settleTimer.start();
        QByteArray lastHash;
        int stableCount = 0, frames = 0;
        unsigned lastShapeProgress = ShapeLoader::progress();
        bool settled = false;
        QImage image;
        while (frames < options.settle.maxFrames
               && settleTimer.elapsed() < qint64(options.settle.maxSeconds) * 1000) {
            pumpEvents(options.settle.frameIntervalMs);
            image = widget.grabFramebuffer();
            const QByteArray hash = imageHash(image);
            frames++;
            stableCount = hash == lastHash ? stableCount + 1 : 0;
            lastHash = hash;
            // A frame can repeat while shapes still load on the workers.
            const unsigned shapeProgress = ShapeLoader::progress();
            if (shapeProgress != lastShapeProgress)
                stableCount = 0;
            lastShapeProgress = shapeProgress;
            if (frames >= options.settle.minFrames && stableCount >= options.settle.stableFrames
                    && !ShapeLoader::busy()) {
                settled = true;
                break;
            }
        }
        image.save(QDir(outputDir).filePath(item.name + ".png"));
        QJsonObject entry{{"name", item.name}, {"settled", settled}, {"frames", frames}};
        if (item.type == "profile") {
            // Textures the profile names but that did not load.
            QStringList missing;
            for (int id : std::as_const(textureIds)) {
                auto found = TexLib::mtex.find(id);
                if (found != TexLib::mtex.end() && found->second != nullptr
                        && (found->second->missing || found->second->error))
                    missing << found->second->pathid;
            }
            missing.sort();
            for (const QString &texture : missing)
                qWarning().noquote() << ViewerCaptureLog << item.name << "texture missing:" << texture;
            entry["diagnostics"] = QJsonArray::fromStringList(diagnostics);
            entry["missingTextures"] = QJsonArray::fromStringList(missing);
        }
        items.append(entry);
        qInfo().noquote() << ViewerCaptureLog << item.name
                          << (settled ? "settled" : "did not settle") << "after" << frames << "frames";
    }

    QJsonObject report{{"label", label}, {"items", items}};
    QFile json(QDir(outputDir).filePath("capture.json"));
    if (json.open(QIODevice::WriteOnly | QIODevice::Truncate))
        json.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
    widget.makeCurrent();
    return 0;
}

int TsreTests::runShapeViewerCompareSuite(const QString &casesFile,
                                           const QString &baselineLabel,
                                           const QString &label, bool verbose) {
    Q_UNUSED(verbose);
    ViewerOptions options;
    QString error;
    if (!loadViewerOptions(casesFile, options, error)) {
        qWarning() << ViewerCompareLog << error;
        return 2;
    }
    if (!validLabel(baselineLabel) || !validLabel(label) || baselineLabel == label) {
        qWarning() << ViewerCompareLog << "need two different capture labels, not"
                   << baselineLabel << "and" << label;
        return 2;
    }
    const QDir dir(QDir(options.outputDir).absolutePath());
    QStringList rows;
    for (const ViewerItem &item : options.items) {
        const QImage baseline(dir.filePath(baselineLabel + "/" + item.name + ".png"));
        const QImage current(dir.filePath(label + "/" + item.name + ".png"));
        const ImageDiff diff = compareImages(baseline, current, options.diffTolerance);
        if (!diff.valid) {
            rows << QString("| %1 | missing | | |").arg(item.name);
            continue;
        }
        diff.heatmap.save(dir.filePath(item.name + "-diff.png"));
        const double ratio = double(diff.diffPixels) / diff.pixels;
        rows << QString("| %1 | %2 | %3% | %4 |").arg(item.name).arg(number(diff.rmse))
                .arg(number(ratio * 100.0)).arg(diff.maxChannelDiff);
        qInfo().noquote() << ViewerCompareLog << item.name << "rmse" << number(diff.rmse)
                          << "diff" << number(ratio * 100.0) + "%";
    }
    QFile markdown(dir.filePath("report.md"));
    if (markdown.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream out(&markdown);
        out << "# Shape Viewer comparison\n\nBaseline: " << baselineLabel << "; current: "
            << label << ".\n\n| Item | RMSE | Diff px | Max channel diff |\n|---|---|---|---|\n";
        for (const QString &row : rows)
            out << row << "\n";
    }
    return 0;
}
