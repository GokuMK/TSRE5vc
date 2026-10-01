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
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopedValueRollback>
#include <QTextStream>
#include <QTimer>
#include <QDebug>

#include <algorithm>
#include <cmath>

#include <routeEditor/RouteEditorGLWidget.h>
#include <tsre/Game.h>
#include <tsre/renderer/RenderStats.h>
#include <tsre/renderer/SelectionId.h>
#include <tsre/world/Route.h>

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
};

struct Options {
    int width = 960;
    int height = 540;
    QString outputDir = "renderer-parity";
    SettleOptions settle;
    int pickColumns = 16;
    int pickRows = 9;
    int timingFrames = 3;
    int diffTolerance = 16;
    bool hud = false;
    bool compass = true;
    bool pointer = false;
    // -1 keeps the profile setting.
    int shadows = -1;
    Thresholds thresholds;
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
    options.compass = root.value("compass").toBool(options.compass);
    options.pointer = root.value("pointer").toBool(options.pointer);
    if (root.contains("shadows"))
        options.shadows = root.value("shadows").toBool() ? 1 : 0;

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

// Captures for one route live under <output>/<route>/<pipeline>/.
QString routeOutputDir(const Options &options) {
    return QDir(options.outputDir).absoluteFilePath(Game::route);
}

QString pipelineDirName(Game::RendererPipeline pipeline) {
    return pipeline == Game::RENDER_PIPELINE_GATHER ? "gather" : "legacy";
}

ImageDiff compareImages(const QImage &legacyImage, const QImage &gatherImage, int tolerance) {
    ImageDiff diff;
    const QImage a = legacyImage.convertToFormat(QImage::Format_RGB32);
    const QImage b = gatherImage.convertToFormat(QImage::Format_RGB32);
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
                // Red: gather differs; brightness follows the difference.
                out[x] = qRgb(128 + maxDiff / 2, 0, 0);
            } else {
                // Dimmed legacy image keeps the context readable.
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

int TsreTests::runRendererCaptureSuite(const QString &casesFile, bool verbose) {
    Q_UNUSED(verbose);
    Options options;
    QString error;
    if (!loadOptions(casesFile, options, error)) {
        qWarning() << CaptureLog << error;
        return 2;
    }
    const Game::RendererPipeline pipeline = Game::requestedRendererPipeline;
    if (pipeline != Game::RENDER_PIPELINE_LEGACY && pipeline != Game::RENDER_PIPELINE_GATHER) {
        qWarning() << CaptureLog << "set core.rendering.pipeline to legacy or gather, not"
                   << Game::RendererPipelineName(pipeline);
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

    const QString outputDir = QDir(routeOutputDir(options)).absoluteFilePath(
                pipelineDirName(pipeline));
    if (!QDir().mkpath(outputDir)) {
        qWarning() << CaptureLog << "cannot create output directory" << outputDir;
        return 2;
    }

    QScopedValueRollback<bool> restoreFpsHud(Game::editorFpsHudEnabled, false);
    QScopedValueRollback<bool> restoreHud(Game::hudEnabled, options.hud);
    QScopedValueRollback<bool> restoreCompass(Game::viewCompass, options.compass);
    QScopedValueRollback<bool> restorePointer(Game::viewPointer3d, options.pointer);
    QScopedValueRollback<bool> restoreHotSwap(Game::rendererPipelineHotSwap, false);
    QScopedValueRollback<int> restoreShadows(Game::shadowsEnabled);
    if (options.shadows >= 0)
        Game::shadowsEnabled = options.shadows > 0 ? std::max(1, Game::shadowsEnabled) : 0;
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
    widget.show();
    QApplication::processEvents();
    if (!widget.isValid()) {
        qWarning() << CaptureLog << "no valid OpenGL context";
        return 2;
    }
    Game::PixelRatio = widget.devicePixelRatioF();
    qInfo() << CaptureLog << Game::RendererPipelineName(pipeline) << "route" << Game::route
            << "loaded in" << loadTimer.elapsed() << "ms";

    // The pipeline must stay as started; a fallback to legacy invalidates the run.
    const auto pipelineUnchanged = [&]() {
        return Game::requestedRendererPipeline == pipeline
                && Game::activeRendererPipeline == pipeline;
    };

    int startTileX = 0, startTileZ = 0;
    float startPos[3];
    float startRot[2];
    widget.diagnosticView(startTileX, startTileZ, startPos, startRot[0], startRot[1]);

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
        widget.setDiagnosticView(tileX, tileZ, pos[0], pos[1], pos[2], rot[0], rot[1]);

        QElapsedTimer settleTimer;
        settleTimer.start();
        QByteArray lastHash;
        int stableCount = 0;
        int frames = 0;
        bool settled = false;
        while (frames < options.settle.maxFrames
               && settleTimer.elapsed() < qint64(options.settle.maxSeconds) * 1000) {
            pumpEvents(options.settle.frameIntervalMs);
            const QByteArray hash = imageHash(widget.grabFramebuffer());
            frames++;
            stableCount = hash == lastHash ? stableCount + 1 : 0;
            lastHash = hash;
            if (frames >= options.settle.minFrames && stableCount >= options.settle.stableFrames) {
                settled = true;
                break;
            }
        }

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
            stats = RenderStats::lastFrame(pipeline);
            if (minCpuMs < 0.0 || stats.cpuMs < minCpuMs)
                minCpuMs = stats.cpuMs;
        }

        QVector<QPoint> points;
        for (int row = 0; row < options.pickRows; ++row)
            for (int column = 0; column < options.pickColumns; ++column)
                points.push_back(QPoint((2 * column + 1) * image.width() / (2 * options.pickColumns),
                                        (2 * row + 1) * image.height() / (2 * options.pickRows)));
        const QVector<quint32> picks = widget.probeSelectionIds(points);

        if (!pipelineUnchanged()) {
            qWarning() << CaptureLog << "pipeline changed during capture to"
                       << Game::RendererPipelineName(Game::activeRendererPipeline);
            return 2;
        }

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
        view["stats"] = statsJson;
        viewReports.append(view);

        qInfo().noquote() << CaptureLog << spec.name
                          << (settled ? "settled" : "did not settle") << "after" << frames
                          << "frames; draws" << stats.drawCalls
                          << "items created" << stats.renderItemsCreated
                          << "matrix clones" << stats.matrixClones;
    }

    QJsonObject report;
    report["route"] = Game::route;
    report["root"] = Game::root;
    report["pipeline"] = Game::RendererPipelineName(pipeline);
    report["generated"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    report["glRenderer"] = QString::fromLatin1(reinterpret_cast<const char *>(
                                                   widget.context()->functions()->glGetString(GL_RENDERER)));
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

int TsreTests::runRendererCompareSuite(const QString &casesFile, bool verbose) {
    Options options;
    QString error;
    if (!loadOptions(casesFile, options, error)) {
        qWarning() << CompareLog << error;
        return 2;
    }
    const QDir routeDir(routeOutputDir(options));
    const QJsonObject legacy = readJsonObject(routeDir.filePath("legacy/capture.json"), error);
    if (legacy.isEmpty()) {
        qWarning() << CompareLog << error;
        return 2;
    }
    const QJsonObject gather = readJsonObject(routeDir.filePath("gather/capture.json"), error);
    if (gather.isEmpty()) {
        qWarning() << CompareLog << error;
        return 2;
    }
    if (legacy["pipeline"].toString() != "legacy" || gather["pipeline"].toString() != "gather") {
        qWarning() << CompareLog << "captures were not made with the legacy and gather pipelines";
        return 2;
    }

    QHash<QString, QJsonObject> gatherViews;
    for (const QJsonValue &value : gather["views"].toArray())
        gatherViews[value.toObject()["name"].toString()] = value.toObject();

    QJsonArray viewReports;
    QStringList markdownRows;
    QStringList notes;
    bool thresholdsFailed = false;
    if (legacy["shadowsEnabled"].toInt() != gather["shadowsEnabled"].toInt())
        notes << "Shadow settings differ between the two captures.";

    for (const QJsonValue &legacyValue : legacy["views"].toArray()) {
        const QJsonObject l = legacyValue.toObject();
        const QString name = l["name"].toString();
        if (!gatherViews.contains(name)) {
            notes << QString("%1: missing from the gather capture.").arg(name);
            continue;
        }
        const QJsonObject g = gatherViews[name];
        if (l["tile"] != g["tile"] || l["pos"] != g["pos"] || l["rot"] != g["rot"])
            notes << QString("%1: camera differs between captures.").arg(name);

        const QImage legacyImage(routeDir.filePath("legacy/" + l["image"].toString()));
        const QImage gatherImage(routeDir.filePath("gather/" + g["image"].toString()));
        const ImageDiff diff = compareImages(legacyImage, gatherImage, options.diffTolerance);
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
                mismatch["legacy"] = describeSelection(a);
                mismatch["gather"] = describeSelection(b);
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
            notes << QString("%1: did not settle (legacy: %2, gather: %3); streaming or "
                             "animation may affect the comparison.")
                     .arg(name).arg(l["settled"].toBool() ? "yes" : "no")
                     .arg(g["settled"].toBool() ? "yes" : "no");
        if (!l["stableAcrossTimingFrames"].toBool() || !g["stableAcrossTimingFrames"].toBool())
            notes << QString("%1: consecutive frames differed during capture.").arg(name);
        if (jsonCount(ls, "shadow", "primitives") > 0 && jsonCount(gs, "shadow", "primitives") == 0)
            notes << QString("%1: gather renders no shadow pass.").arg(name);

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
        view["stats"] = QJsonObject{{"legacy", ls}, {"gather", gs}};
        QJsonArray failureList;
        for (const QString &failure : failures)
            failureList.append(failure);
        view["thresholdFailures"] = failureList;
        viewReports.append(view);

        markdownRows << QString("| %1 | %2 / %3 | %4 | %5% | %6/%7 | %8 / %9 | %10 / %11 | %12 | %13 / %14 | %15 / %16 | %17 |")
                        .arg(name)
                        .arg(l["settled"].toBool() ? "yes" : "no")
                        .arg(g["settled"].toBool() ? "yes" : "no")
                        .arg(number(diff.rmse))
                        .arg(number(diffRatio * 100.0))
                        .arg(pickMismatches).arg(lIds.size())
                        .arg(jsonCount(ls, "sceneTotal", "primitives"))
                        .arg(jsonCount(gs, "sceneTotal", "primitives"))
                        .arg(jsonCount(ls, "sceneTotal", "samples"))
                        .arg(jsonCount(gs, "sceneTotal", "samples"))
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
                                  << "legacy:" << m["legacy"].toString()
                                  << "gather:" << m["gather"].toString();
            }
        }
    }

    QJsonObject report;
    report["route"] = legacy["route"];
    report["generated"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    report["glRenderer"] = QJsonObject{{"legacy", legacy["glRenderer"]}, {"gather", gather["glRenderer"]}};
    report["views"] = viewReports;
    report["notes"] = QJsonArray::fromStringList(notes);
    QFile jsonFile(routeDir.filePath("report.json"));
    if (jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        jsonFile.write(QJsonDocument(report).toJson(QJsonDocument::Indented));

    QFile markdownFile(routeDir.filePath("report.md"));
    if (markdownFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream out(&markdownFile);
        out << "# Renderer parity: " << legacy["route"].toString() << "\n\n";
        out << "Legacy and gather were captured in separate processes, each started with "
               "its pipeline; neither switched pipelines at runtime.\n\n";
        out << "GL renderer: " << legacy["glRenderer"].toString() << "\n\n";
        out << "Scene primitives and samples are GPU query totals for the scene phase "
               "(terrain, world, water, overlays, pointer).\n\n";
        out << "| View | Settled L / G | RMSE | Diff px | Pick mismatches "
               "| Scene prims L / G | Scene samples L / G | Gather draws "
               "| Items created L / G | Matrix clones L / G | Thresholds |\n";
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
