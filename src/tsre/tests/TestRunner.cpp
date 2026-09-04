/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tests/TestRunner.h>

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QTextStream>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>

#include <tsre/Game.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/math3d/Flex.h>
#include <tsre/math3d/GLMatrix.h>
#include <tsre/math3d/Vector2f.h>
#include <tsre/math3d/Vector3f.h>
#include <tsre/procedural/ProceduralTrackPolicy.h>
#include <tsre/procedural/OrtsTrackProfile.h>
#include <tsre/procedural/OrtsTrackProfileRenderer.h>
#include <tsre/tdb/TDB.h>
#include <tsre/tdb/TRnode.h>
#include <tsre/tdb/TrackShape.h>
#include <tsre/tdb/TSection.h>
#include <tsre/tdb/TSectionDAT.h>
#include <tsre/tests/RouteLoadTestSuite.h>
#include <tsre/tests/SettingsTestSuite.h>
#include <tsre/tests/TdbLoadTestSuite.h>
#include <tsre/tests/TerrainRawBenchmark.h>
#include <tsre/texture/Brush.h>
#include <tsre/world/TerrainActionRaster.h>
#include <tsre/world/Terrain.h>
#include <tsre/world/TFile.h>
#include <tsre/world/TerrainGridLayout.h>
#include <tsre/world/TerrainMeshBackend.h>
#include <tsre/world/TerrainInfo.h>
#include <tsre/world/Trk.h>
#include <tsre/world/Ref.h>
#include <tsre/world/objects/DynTrackObj.h>

namespace {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

static Vec2 add(Vec2 a, Vec2 b) {
    return {a.x + b.x, a.y + b.y};
}

static Vec2 sub(Vec2 a, Vec2 b) {
    return {a.x - b.x, a.y - b.y};
}

static Vec2 scale(Vec2 a, float s) {
    return {a.x * s, a.y * s};
}

static float dot(Vec2 a, Vec2 b) {
    return a.x * b.x + a.y * b.y;
}

static float length(Vec2 a) {
    return std::sqrt(dot(a, a));
}

static float wrapPi(float a) {
    a = std::fmod(a + (float)M_PI, 2.0f * (float)M_PI);
    if (a < 0.0f)
        a += 2.0f * (float)M_PI;
    return a - (float)M_PI;
}

// Local solver frame: +Y forward, +X right; heading is CCW from +Y (so +heading = left).
static Vec2 forwardLeft(float heading) {
    return {-std::sin(heading), std::cos(heading)};
}

static Vec2 rightLeft(float heading) {
    return {std::cos(heading), std::sin(heading)};
}

static Vec2 curveDispLeft(float angle, float radius) {
    float a = std::fabs(angle);
    if (a < 1e-6f || radius <= 0.0f)
        return {0.0f, 0.0f};
    int s = (angle >= 0.0f) ? 1 : -1;
    float dx = -((float)s) * radius * (1.0f - std::cos(a));
    float dy = radius * std::sin(a);
    return {dx, dy};
}

struct Pose2 {
    Vec2 pos;
    float heading = 0.0f; // + = left (CCW from +Y)
};

static void applyStraight(Pose2 &pose, float len) {
    if (len <= 0.0f)
        return;
    pose.pos = add(pose.pos, scale(forwardLeft(pose.heading), len));
}

static void applyCurve(Pose2 &pose, float angle, float radius) {
    if (std::fabs(angle) < 1e-6f || radius <= 0.0f)
        return;
    Vec2 local = curveDispLeft(angle, radius);
    pose.pos = add(pose.pos, add(scale(rightLeft(pose.heading), local.x), scale(forwardLeft(pose.heading), local.y)));
    pose.heading = wrapPi(pose.heading + angle);
}

static Pose2 simulateLocalLeft(const float *sections10Left) {
    Pose2 pose;
    pose.pos = {0.0f, 0.0f};
    pose.heading = 0.0f;
    applyStraight(pose, sections10Left[0]);
    applyCurve(pose, sections10Left[2], sections10Left[3]);
    applyStraight(pose, sections10Left[4]);
    applyCurve(pose, sections10Left[6], sections10Left[7]);
    applyStraight(pose, sections10Left[8]);
    return pose;
}

static float centerlineLength(const float *sections10Dyntrack) {
    float sum = 0.0f;
    // 0,4,8 are straights; 2,6 are curves (angle,radius).
    sum += std::max(0.0f, sections10Dyntrack[0]);
    sum += (sections10Dyntrack[3] > 0.0f) ? (std::fabs(sections10Dyntrack[2]) * sections10Dyntrack[3]) : 0.0f;
    sum += std::max(0.0f, sections10Dyntrack[4]);
    sum += (sections10Dyntrack[7] > 0.0f) ? (std::fabs(sections10Dyntrack[6]) * sections10Dyntrack[7]) : 0.0f;
    sum += std::max(0.0f, sections10Dyntrack[8]);
    return sum;
}

struct FlexCase {
    int id = -1;
    int x1 = 0;
    int z1 = 0;
    float p1[3] = {0};
    float q1[4] = {0};
    int x2 = 0;
    int z2 = 0;
    float p2[3] = {0};
    float q2[4] = {0};
    float preferredMinCurveRadius = 0.0f;
    bool preferNiceRadii = true;
    float minimumCurveRadius = 5.0f;
    bool expectedSuccess = false;
    bool hasExpectedSections = false;
    float expectedSections[10] = {0};
};

static QString suiteNameNormalized(QString s) {
    return s.trimmed().toLower();
}

static bool parseFloatArrayFixed(const QJsonValue &v, int count, float *out, QString &err) {
    if (!v.isArray()) {
        err = "expected array";
        return false;
    }
    const QJsonArray arr = v.toArray();
    if (arr.size() != count) {
        err = QString("expected array of size %1").arg(count);
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (!arr[i].isDouble() && !arr[i].isNull()) {
            err = QString("expected number (or null) at index %1").arg(i);
            return false;
        }
        out[i] = (float)arr[i].toDouble(0.0);
    }
    return true;
}

static bool parseFlexCase(const QJsonObject &obj, FlexCase &out, QString &err) {
    if (obj.value("type").toString() != "flex_case") {
        err = "not a flex_case";
        return false;
    }

    out.id = obj.value("id").toInt(-1);
    out.x1 = obj.value("x1").toInt();
    out.z1 = obj.value("z1").toInt();
    out.x2 = obj.value("x2").toInt();
    out.z2 = obj.value("z2").toInt();

    if (!parseFloatArrayFixed(obj.value("p1"), 3, out.p1, err))
        return false;
    if (!parseFloatArrayFixed(obj.value("q1"), 4, out.q1, err))
        return false;
    if (!parseFloatArrayFixed(obj.value("p2"), 3, out.p2, err))
        return false;
    if (!parseFloatArrayFixed(obj.value("q2"), 4, out.q2, err))
        return false;

    out.preferredMinCurveRadius = (float)obj.value("preferredMinCurveRadius").toDouble(0.0);
    out.preferNiceRadii = obj.value("preferNiceRadii").toBool(true);
    out.minimumCurveRadius = (float)obj.value("minimumCurveRadius").toDouble(5.0);
    out.expectedSuccess = obj.value("success").toBool(false);

    const QJsonValue sectionsV = obj.value("sections");
    if (out.expectedSuccess) {
        if (!sectionsV.isArray()) {
            err = "expected sections array for success=true";
            return false;
        }
        if (!parseFloatArrayFixed(sectionsV, 10, out.expectedSections, err))
            return false;
        out.hasExpectedSections = true;
    }

    return true;
}

static bool nearlyEqual(float a, float b, float absTol = 1e-3f, float relTol = 1e-5f) {
    if (a == b)
        return true;
    if (!std::isfinite(a) || !std::isfinite(b))
        return false;
    float diff = std::fabs(a - b);
    if (diff <= absTol)
        return true;
    float scale = std::max(std::fabs(a), std::fabs(b));
    return diff <= relTol * scale;
}

struct FlexEndCheck {
    Vec2 expectedWorldEnd;
    Vec2 gotWorldEnd;
    float expectedYaw = 0.0f;
    float gotYaw = 0.0f;
    float posErr = 0.0f;
    float yawErr = 0.0f;
    float centerLen = 0.0f;
    float gotP2LocalX = 0.0f;
    float gotP2LocalZ = 0.0f;
};

static FlexEndCheck checkFlexEndPose(const FlexCase &c, const float *sectionsDyntrack10) {
    // Match `Flex::NewFlex(...)` 2D world mapping (XZ -> XY with Z flipped, plus tile offsets).
    Vec2 P0 = {c.p1[0], -c.p1[2]};
    Vec2 P1 = {
        c.p2[0] + 2048.0f * (float)(c.x2 - c.x1),
        -c.p2[2] - 2048.0f * (float)(c.z2 - c.z1),
    };

    float yaw0 = c.q1[1];
    float yaw1 = c.q2[1];

    // World basis vectors for yaw (+ = right, as in Flex.cpp visualization).
    Vec2 f0 = {std::sin(yaw0), std::cos(yaw0)};
    Vec2 r0 = {std::cos(yaw0), -std::sin(yaw0)};

    // Convert returned dyntrack sections (+curve = right) into our left-positive simulator.
    float simLeft[10];
    for (int i = 0; i < 10; i++)
        simLeft[i] = sectionsDyntrack10[i];
    simLeft[2] = -simLeft[2];
    simLeft[6] = -simLeft[6];

    Pose2 endLocal = simulateLocalLeft(simLeft);

    Vec2 endWorld = add(P0, add(scale(r0, endLocal.pos.x), scale(f0, endLocal.pos.y)));
    float yawEnd = wrapPi(yaw0 - endLocal.heading);

    FlexEndCheck out;
    out.expectedWorldEnd = P1;
    out.gotWorldEnd = endWorld;
    out.expectedYaw = yaw1;
    out.gotYaw = yawEnd;
    out.posErr = length(sub(endWorld, P1));
    out.yawErr = std::fabs(wrapPi(yawEnd - yaw1));
    out.centerLen = centerlineLength(sectionsDyntrack10);

    // Convert back into target tile-local coordinates (for more readable debug output).
    out.gotP2LocalX = endWorld.x - 2048.0f * (float)(c.x2 - c.x1);
    out.gotP2LocalZ = -(endWorld.y + 2048.0f * (float)(c.z2 - c.z1));
    return out;
}

static int runFlexSuite(QString casesFile, bool verbose) {
    const bool explicitCasesFile = !casesFile.trimmed().isEmpty();
    if (!explicitCasesFile)
        casesFile = "features/tests/cases/flex.jsonl";

    if (!explicitCasesFile && !QFile::exists(casesFile)) {
        qInfo() << "[tests:flex] SKIP: no captured baseline:" << casesFile;
        return 0;
    }

    QFile f(casesFile);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[tests:flex] cannot open cases file:" << casesFile;
        return 2;
    }

    QTextStream in(&f);
    int lineNo = 0;
    int casesFound = 0;
    int passed = 0;
    int failed = 0;
    float maxPosErr = 0.0f;
    float maxYawErr = 0.0f;

    while (!in.atEnd()) {
        const QString rawLine = in.readLine();
        lineNo++;

        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith("#"))
            continue;

        QJsonParseError jerr;
        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &jerr);
        if (jerr.error != QJsonParseError::NoError || !doc.isObject()) {
            if (verbose)
                qWarning() << "[tests:flex] invalid JSON at" << casesFile << ":" << lineNo << "-" << jerr.errorString();
            continue;
        }

        const QJsonObject obj = doc.object();
        if (obj.value("type").toString() != "flex_case")
            continue; // ignore flex_candidate, session markers, etc.

        FlexCase c;
        QString perr;
        if (!parseFlexCase(obj, c, perr)) {
            qWarning() << "[tests:flex] bad flex_case at" << casesFile << ":" << lineNo << "-" << perr;
            failed++;
            continue;
        }
        casesFound++;

        float got[10] = {0};
        const bool ok = Flex::NewFlex(
                c.x1, c.z1, c.p1, c.q1,
                c.x2, c.z2, c.p2, c.q2,
                got,
                c.preferredMinCurveRadius,
                c.preferNiceRadii,
                c.minimumCurveRadius);
        bool caseOk = (ok == c.expectedSuccess);

        // If we succeeded, verify that the produced section path actually ends at p2 (and matches q2 heading).
        FlexEndCheck endCheck;
        if (caseOk && ok) {
            endCheck = checkFlexEndPose(c, got);
            maxPosErr = std::max(maxPosErr, endCheck.posErr);
            maxYawErr = std::max(maxYawErr, endCheck.yawErr);

            const float posTol = 0.2f;   // meters
            const float yawTol = 0.02f;  // radians (~1.15 deg)
            if (!(endCheck.posErr < posTol && endCheck.yawErr < yawTol))
                caseOk = false;
        }

        if (caseOk) {
            passed++;
            if (verbose && ok) {
                qInfo() << "[tests:flex] PASS id=" << c.id << "line=" << lineNo
                        << "posErr=" << endCheck.posErr
                        << "yawErrDeg=" << (endCheck.yawErr * 180.0f / (float)M_PI)
                        << "len=" << endCheck.centerLen;
            } else if (verbose) {
                qInfo() << "[tests:flex] PASS id=" << c.id << "line=" << lineNo;
            }
        } else {
            failed++;
            qWarning() << "[tests:flex] FAIL id=" << c.id << "line=" << lineNo << "expectedSuccess=" << c.expectedSuccess << "gotSuccess=" << ok;
            if (ok) {
                endCheck = checkFlexEndPose(c, got);
                qWarning() << "[tests:flex] end posErr=" << endCheck.posErr
                           << "yawErrDeg=" << (endCheck.yawErr * 180.0f / (float)M_PI)
                           << "len=" << endCheck.centerLen;
                if (verbose) {
                    qInfo() << "[tests:flex] expected p2:" << c.p2[0] << c.p2[1] << c.p2[2]
                            << "tile:" << c.x2 << c.z2;
                    qInfo() << "[tests:flex] got p2:" << endCheck.gotP2LocalX << c.p2[1] << endCheck.gotP2LocalZ
                            << "tile:" << c.x2 << c.z2;
                }
            }

            // Keep the old "sections diff" as a debugging aid, but do not use it as the primary pass/fail.
            if (verbose && ok && c.expectedSuccess && c.hasExpectedSections) {
                QString diffs;
                for (int i = 0; i < 10; i++) {
                    if (!nearlyEqual(got[i], c.expectedSections[i])) {
                        diffs += QString(" [%1 exp=%2 got=%3]").arg(i).arg(c.expectedSections[i], 0, 'g', 8).arg(got[i], 0, 'g', 8);
                    }
                }
                if (!diffs.isEmpty())
                    qInfo().noquote() << "[tests:flex] sections diffs:" << diffs;
            }
        }
    }

    qInfo() << "[tests:flex] cases=" << casesFound << "passed=" << passed << "failed=" << failed
            << "maxPosErr=" << maxPosErr
            << "maxYawErrDeg=" << (maxYawErr * 180.0f / (float)M_PI);
    if (casesFound == 0) {
        qWarning() << "[tests:flex] no flex_case records found in:" << casesFile;
        return 2;
    }
    return (failed == 0) ? 0 : 1;
}

static int runFlexPointSuite(bool verbose) {
    struct PointCase {
        const char *name;
        int x1;
        int z1;
        float p1[3];
        float yaw;
        int x2;
        int z2;
        float p2[3];
        bool expectedSuccess;
        int expectedKind; // 0 = empty, 1 = straight, 2 = curve
        bool trimmed;
    };

    const PointCase cases[] = {
        {"zero", 0, 0, {0, 0, 0}, 0, 0, 0, {0, 0, 0}, true, 0, false},
        {"straight", 0, 0, {0, 0, 0}, 0, 0, 0, {0, 0, -100}, true, 1, false},
        {"near-axis-straight", 0, 0, {0, 0, 0}, 0, 0, 0, {0.04f, 0, -100}, true, 1, false},
        {"outside-straight-tolerance", 0, 0, {0, 0, 0}, 0, 0, 0, {0.06f, 0, -100}, true, 2, false},
        {"shallow-right", 0, 0, {0, 0, 0}, 0, 0, 0,
            {10000.0f * (1.0f - std::cos(0.005f)), 0, -10000.0f * std::sin(0.005f)}, true, 2, false},
        {"shallow-left", 0, 0, {0, 0, 0}, 0, 0, 0,
            {-10000.0f * (1.0f - std::cos(0.005f)), 0, -10000.0f * std::sin(0.005f)}, true, 2, false},
        {"right-quarter", 0, 0, {0, 0, 0}, 0, 0, 0, {20, 0, -20}, true, 2, false},
        {"left-quarter", 0, 0, {0, 0, 0}, 0, 0, 0, {-20, 0, -20}, true, 2, false},
        {"semicircle", 0, 0, {0, 0, 0}, 0, 0, 0, {40, 0, 0}, true, 2, false},
        {"behind", 0, 0, {0, 0, 0}, 0, 0, 0, {0, 0, 20}, false, 0, false},
        {"too-tight", 0, 0, {0, 0, 0}, 0, 0, 0, {10, 0, 0}, false, 0, false},
        {"rotated-straight", 0, 0, {0, 0, 0}, (float)M_PI / 2.0f, 0, 0, {100, 0, 0}, true, 1, false},
        {"tile-boundary", 0, 0, {1000, 0, 0}, (float)M_PI / 2.0f, 1, 0, {-1000, 0, 0}, true, 1, false},
        {"trimmed-straight", 0, 0, {0, 0, 0}, 0, 0, -1, {0, 0, -952}, true, 1, true},
    };

    int failed = 0;
    int passed = 0;
    for (const PointCase &c : cases) {
        float got[10] = {0};
        float p1[3] = {c.p1[0], c.p1[1], c.p1[2]};
        float p2[3] = {c.p2[0], c.p2[1], c.p2[2]};
        const bool ok = Flex::NewFlexToPoint(c.x1, c.z1, p1, c.yaw, c.x2, c.z2, p2, got);
        bool caseOk = (ok == c.expectedSuccess);

        if (caseOk && ok) {
            const bool hasStraight = got[0] > 0.01f || got[4] > 0.01f || got[8] > 0.01f;
            const bool hasCurve = (std::fabs(got[2]) > 1e-6f && got[3] > 0.1f)
                    || (std::fabs(got[6]) > 1e-6f && got[7] > 0.1f);
            if (c.expectedKind == 0)
                caseOk = !hasStraight && !hasCurve;
            else if (c.expectedKind == 1)
                caseOk = hasStraight && !hasCurve;
            else if (c.expectedKind == 2)
                caseOk = hasCurve && !hasStraight;

            if (caseOk && !c.trimmed) {
                FlexCase poseCase;
                poseCase.x1 = c.x1;
                poseCase.z1 = c.z1;
                poseCase.x2 = c.x2;
                poseCase.z2 = c.z2;
                std::copy(c.p1, c.p1 + 3, poseCase.p1);
                std::copy(c.p2, c.p2 + 3, poseCase.p2);
                poseCase.q1[1] = c.yaw;
                const FlexEndCheck check = checkFlexEndPose(poseCase, got);
                caseOk = check.posErr < 0.1f;
                if (verbose)
                    qInfo() << "[tests:flex-point]" << c.name << "posErr=" << check.posErr;

                float startQ[4] = {
                    0.0f,
                    std::sin(-c.yaw * 0.5f),
                    0.0f,
                    std::cos(-c.yaw * 0.5f)
                };
                int endTileX = 0;
                int endTileZ = 0;
                float endPosition[3] = {0, 0, 0};
                float endQ[4] = {0, 0, 0, 1};
                caseOk = Flex::DyntrackEndpoint(
                        c.x1,
                        c.z1,
                        c.p1,
                        startQ,
                        got,
                        endTileX,
                        endTileZ,
                        endPosition,
                        endQ);
                if(caseOk) {
                    const float dx = endTileX * 2048.0f + endPosition[0]
                            - (c.x2 * 2048.0f + c.p2[0]);
                    const float dz = endTileZ * 2048.0f + endPosition[2]
                            - (c.z2 * 2048.0f + c.p2[2]);
                    const float endpointError = std::sqrt(dx * dx + dz * dz);
                    const float expectedEndYaw = wrapPi(c.yaw + got[2] + got[6]);
                    const float endpointYaw = Flex::TdbYawFromTrackQuaternion(endQ);
                    caseOk = endpointError < 0.1f
                            && std::fabs(wrapPi(endpointYaw - expectedEndYaw)) < 1e-4f;
                    if(verbose)
                        qInfo() << "[tests:flex-point]" << c.name
                                << "chainPosErr=" << endpointError
                                << "chainYawErr="
                                << std::fabs(wrapPi(endpointYaw - expectedEndYaw));
                }
            }

            if (caseOk && c.trimmed)
                caseOk = nearlyEqual(centerlineLength(got), 2048.0f, 0.1f);

            if (caseOk && hasCurve)
                caseOk = std::fabs(got[2]) <= (float)M_PI + 1e-4f
                        && std::fabs(got[6]) <= (float)M_PI + 1e-4f;
        }

        if (caseOk) {
            passed++;
        } else {
            failed++;
            qWarning() << "[tests:flex-point] failed:" << c.name
                       << "expectedSuccess=" << c.expectedSuccess
                       << "actualSuccess=" << ok;
        }
    }

    struct YawCase {
        const char *name;
        float objectYaw;
    };
    const YawCase yawCases[] = {
        {"yaw-zero", 0.0f},
        {"yaw-right", (float)M_PI / 2.0f},
        {"yaw-left", -(float)M_PI / 2.0f},
        {"yaw-reverse", (float)M_PI},
    };
    for (const YawCase &c : yawCases) {
        float q[4] = {
            0.0f,
            std::sin(c.objectYaw * 0.5f),
            0.0f,
            std::cos(c.objectYaw * 0.5f)
        };
        const float gotYaw = Flex::TdbYawFromTrackQuaternion(q);
        const bool caseOk = std::fabs(wrapPi(gotYaw + c.objectYaw)) < 1e-4f;
        if (caseOk) {
            passed++;
        } else {
            failed++;
            qWarning() << "[tests:flex-point] failed:" << c.name
                       << "objectYaw=" << c.objectYaw << "tdbYaw=" << gotYaw;
        }
    }

    int guardCaseCount = 0;
    {
        guardCaseCount++;
        float p1[3] = {0, 0, 0};
        float p2[3] = {0, 0, -10};
        p2[0] = std::numeric_limits<float>::quiet_NaN();
        float sections[10];
        std::fill(sections, sections + 10, 123.0f);
        const bool rejected = !Flex::NewFlexToPoint(
                0, 0, p1, 0, 0, 0, p2, sections);
        bool cleared = true;
        for(float value : sections)
            cleared = cleared && value == 0.0f;
        if(rejected && cleared)
            passed++;
        else {
            failed++;
            qWarning() << "[tests:flex-point] failed: non-finite point guard";
        }
    }
    {
        guardCaseCount++;
        float p[3] = {0, 0, 0};
        float q[4] = {0, 0, 0, 1};
        float sections[10] = {0};
        sections[0] = std::numeric_limits<float>::quiet_NaN();
        int endTileX = 0;
        int endTileZ = 0;
        float endPosition[3] = {0, 0, 0};
        float endQ[4] = {0, 0, 0, 1};
        if(!Flex::DyntrackEndpoint(
                0, 0, p, q, sections,
                endTileX, endTileZ, endPosition, endQ))
            passed++;
        else {
            failed++;
            qWarning() << "[tests:flex-point] failed: non-finite endpoint guard";
        }
    }

    struct OffsetPoseCase {
        const char *name;
        int sourceTileX;
        int sourceTileZ;
        float sourcePosition[3];
        float tdbYaw;
        float rightOffset;
        int expectedTileX;
        int expectedTileZ;
        float expectedPosition[3];
    };
    const OffsetPoseCase offsetCases[] = {
        {"offset-identity", 5, -2, {10, 3, -20}, 0, 4,
            5, -2, {14, 3, -20}},
        {"offset-heading-positive-x", 0, 0, {0, 7, 0},
            (float)M_PI / 2.0f, 4,
            0, 0, {0, 7, 4}},
        {"offset-heading-negative-x", 0, 0, {0, 7, 0},
            -(float)M_PI / 2.0f, 4,
            0, 0, {0, 7, -4}},
        {"offset-heading-positive-z", 0, 0, {0, 7, 0},
            (float)M_PI, 4,
            0, 0, {-4, 7, 0}},
        {"offset-positive-tile-crossing", 0, 0, {1023, 0, 0}, 0, 4,
            1, 0, {-1021, 0, 0}},
        {"offset-negative-tile-crossing", 0, 0, {-1023, 0, 0}, 0, -4,
            -1, 0, {1021, 0, 0}},
        {"offset-large-negative-tiles", -5306, -14961,
            {-313.659821f, 0.983149f, -91.258560f}, 0, -4,
            -5306, -14961,
            {-317.659821f, 0.983149f, -91.258560f}},
    };
    for(const OffsetPoseCase &c : offsetCases) {
        const float objectYaw = -c.tdbYaw;
        const float pitch = 7.0f * (float)M_PI / 180.0f;
        const float sx = std::sin(pitch * 0.5f);
        const float cx = std::cos(pitch * 0.5f);
        const float sy = std::sin(objectYaw * 0.5f);
        const float cy = std::cos(objectYaw * 0.5f);
        float sourceQ[4] = {
            cy * sx,
            sy * cx,
            -sy * sx,
            cy * cx
        };
        int targetTileX = 0;
        int targetTileZ = 0;
        float targetPosition[3] = {0, 0, 0};
        float targetQ[4] = {0, 0, 0, 1};
        const bool ok = Flex::OffsetWorldPose(
                c.sourceTileX,
                c.sourceTileZ,
                c.sourcePosition,
                sourceQ,
                c.rightOffset,
                targetTileX,
                targetTileZ,
                targetPosition,
                targetQ);
        bool caseOk = ok
                && targetTileX == c.expectedTileX
                && targetTileZ == c.expectedTileZ;
        for(int i = 0; i < 3; i++)
            caseOk = caseOk
                    && nearlyEqual(targetPosition[i], c.expectedPosition[i], 1e-4f);
        for(int i = 0; i < 4; i++)
            caseOk = caseOk && nearlyEqual(targetQ[i], sourceQ[i], 1e-6f);
        if(caseOk) {
            passed++;
        } else {
            failed++;
            qWarning() << "[tests:flex-point] failed:" << c.name
                    << "tile=" << targetTileX << targetTileZ
                    << "position=" << targetPosition[0]
                    << targetPosition[1] << targetPosition[2];
        }
    }

    int endpointOrientationCaseCount = 0;
    {
        endpointOrientationCaseCount++;
        float startPosition[3] = {1000, 5, -1000};
        const float pitch = 5.0f * (float)M_PI / 180.0f;
        const float objectYaw = 35.0f * (float)M_PI / 180.0f;
        const float sx = std::sin(pitch * 0.5f);
        const float cx = std::cos(pitch * 0.5f);
        const float sy = std::sin(objectYaw * 0.5f);
        const float cy = std::cos(objectYaw * 0.5f);
        float startQ[4] = {
            cy * sx, sy * cx, -sy * sx, cy * cx
        };
        float sections[10] = {
            10, 0,
            (float)M_PI / 2.0f, 20,
            5, 0,
            -(float)M_PI / 4.0f, 30,
            8, 0
        };
        int mainEndTileX = 0;
        int mainEndTileZ = 0;
        float mainEndPosition[3] = {0, 0, 0};
        float mainEndQ[4] = {0, 0, 0, 1};
        bool caseOk = Flex::DyntrackEndpoint(
                0, 0, startPosition, startQ, sections,
                mainEndTileX, mainEndTileZ, mainEndPosition, mainEndQ);
        const float expectedEndTdbYaw = wrapPi(
                -objectYaw + sections[2] + sections[6]);
        const float actualEndTdbYaw =
                Flex::TdbYawFromTrackQuaternion(mainEndQ);
        float endForward[3] = {0, 0, 1};
        Vec3::transformQuat(endForward, endForward, mainEndQ);
        const float actualPitch = std::atan2(
                -endForward[1],
                std::sqrt(endForward[0] * endForward[0]
                    + endForward[2] * endForward[2]));
        caseOk = caseOk
                && std::fabs(wrapPi(actualEndTdbYaw - expectedEndTdbYaw)) < 1e-5f
                && std::fabs(actualPitch - pitch) < 1e-5f;
        if(caseOk) {
            passed++;
        } else {
            failed++;
            qWarning() << "[tests:flex-point] failed: pitched endpoint orientation"
                    << "expectedYaw=" << expectedEndTdbYaw
                    << "actualYaw=" << actualEndTdbYaw
                    << "expectedPitch=" << pitch
                    << "actualPitch=" << actualPitch;
        }
    }
    {
        endpointOrientationCaseCount++;
        float startPosition[3] = {
            -313.659821f, 0.983149f, -91.258560f
        };
        float startQ[4] = {0, 0, 0, 1};
        float sections[10] = {10, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        int endTileX = 0;
        int endTileZ = 0;
        float endPosition[3] = {0, 0, 0};
        float endQ[4] = {0, 0, 0, 1};
        const bool caseOk = Flex::DyntrackEndpoint(
                -5306, -14961,
                startPosition, startQ, sections,
                endTileX, endTileZ, endPosition, endQ)
                && endTileX == -5306
                && endTileZ == -14961
                && nearlyEqual(endPosition[0], startPosition[0], 1e-4f)
                && nearlyEqual(
                    endPosition[2], startPosition[2] - 10.0f, 1e-4f);
        if(caseOk) {
            passed++;
        } else {
            failed++;
            qWarning() << "[tests:flex-point] failed: large-tile endpoint precision"
                    << "tile=" << endTileX << endTileZ
                    << "position=" << endPosition[0] << endPosition[2];
        }
    }

    int parallelCaseCount = 0;
    {
        float mainSections[10] = {
            0, 0, (float)M_PI / 2.0f, 20, 0, 0, 0, 0, 0, 0
        };
        const float startYaws[] = {0.0f, (float)M_PI / 2.0f};
        const float offsets[] = {-4.0f, 4.0f};
        for(float startYaw : startYaws) {
            const float objectYaw = -startYaw;
            float startQ[4] = {
                0,
                std::sin(objectYaw * 0.5f),
                0,
                std::cos(objectYaw * 0.5f)
            };
            float mainStartPosition[3] = {100, 5, -200};
            int mainEndTileX = 0;
            int mainEndTileZ = 0;
            float mainEndPosition[3] = {0, 0, 0};
            float mainEndQ[4] = {0, 0, 0, 1};
            const bool mainOk = Flex::DyntrackEndpoint(
                    0, 0, mainStartPosition, startQ, mainSections,
                    mainEndTileX, mainEndTileZ,
                    mainEndPosition, mainEndQ);

            for(float offset : offsets) {
                parallelCaseCount++;
                int sideStartTileX = 0;
                int sideStartTileZ = 0;
                float sideStartPosition[3] = {0, 0, 0};
                float sideStartQ[4] = {0, 0, 0, 1};
                int targetEndTileX = 0;
                int targetEndTileZ = 0;
                float targetEndPosition[3] = {0, 0, 0};
                float targetEndQ[4] = {0, 0, 0, 1};
                float sideSections[10] = {0};
                bool caseOk = mainOk
                        && Flex::OffsetWorldPose(
                            0, 0, mainStartPosition, startQ, offset,
                            sideStartTileX, sideStartTileZ,
                            sideStartPosition, sideStartQ)
                        && Flex::OffsetWorldPose(
                            mainEndTileX, mainEndTileZ,
                            mainEndPosition, mainEndQ, offset,
                            targetEndTileX, targetEndTileZ,
                            targetEndPosition, targetEndQ)
                        && Flex::ParallelDyntrackSections(
                            mainSections, offset, sideSections);

                int sideEndTileX = 0;
                int sideEndTileZ = 0;
                float sideEndPosition[3] = {0, 0, 0};
                float sideEndQ[4] = {0, 0, 0, 1};
                caseOk = caseOk && Flex::DyntrackEndpoint(
                        sideStartTileX, sideStartTileZ,
                        sideStartPosition, sideStartQ, sideSections,
                        sideEndTileX, sideEndTileZ,
                        sideEndPosition, sideEndQ);
                if(caseOk) {
                    const float dx = (sideEndTileX - targetEndTileX) * 2048.0f
                            + sideEndPosition[0] - targetEndPosition[0];
                    const float dz = (sideEndTileZ - targetEndTileZ) * 2048.0f
                            + sideEndPosition[2] - targetEndPosition[2];
                    caseOk = std::sqrt(dx * dx + dz * dz) < 0.001f
                            && nearlyEqual(
                                sideStartPosition[2] - mainStartPosition[2],
                                offset * std::sin(startYaw),
                                1e-4f);
                }
                if(caseOk) {
                    passed++;
                } else {
                    failed++;
                    qWarning() << "[tests:flex-point] failed: parallel axis case"
                            << "startYaw=" << startYaw
                            << "offset=" << offset;
                }
            }
        }
    }
    {
        // Parallel curves must choose their pitch from their own local
        // forward depth. Using the horizontal endpoint chord leaves inner
        // and outer tracks at different heights.
        float mainSections[10] = {
            0, 0, (float)M_PI / 3.0f, 100, 0, 0, 0, 0, 0, 0
        };
        constexpr int startTileX = -5306;
        constexpr int startTileZ = -14961;
        constexpr float startYaw = 0.7f;
        constexpr float mainGrade = 100.0f;
        float mainStartPosition[3] = {1019.0f, 12.0f, -1018.0f};
        float mainStartQ[4] = {0, 0, 0, 1};
        Quat::rotateY(mainStartQ, mainStartQ, -startYaw);
        Quat::rotateX(mainStartQ, mainStartQ,
                std::asin(mainGrade / 1000.0f));
        int mainEndTileX = 0;
        int mainEndTileZ = 0;
        float mainEndPosition[3] = {0, 0, 0};
        float mainEndQ[4] = {0, 0, 0, 1};
        const bool mainOk = Flex::DyntrackEndpoint(
                startTileX, startTileZ,
                mainStartPosition, mainStartQ, mainSections,
                mainEndTileX, mainEndTileZ,
                mainEndPosition, mainEndQ);

        const float offsets[] = {-4.0f, 4.0f};
        for(float offset : offsets) {
            parallelCaseCount++;
            int sideStartTileX = 0;
            int sideStartTileZ = 0;
            float sideStartPosition[3] = {0, 0, 0};
            float sideStartQ[4] = {0, 0, 0, 1};
            int targetEndTileX = 0;
            int targetEndTileZ = 0;
            float targetEndPosition[3] = {0, 0, 0};
            float targetEndQ[4] = {0, 0, 0, 1};
            float sideSections[10] = {0};
            float sideGrade = 0.0f;
            bool caseOk = mainOk
                    && Flex::OffsetWorldPose(
                        startTileX, startTileZ,
                        mainStartPosition, mainStartQ, offset,
                        sideStartTileX, sideStartTileZ,
                        sideStartPosition, sideStartQ)
                    && Flex::OffsetWorldPose(
                        mainEndTileX, mainEndTileZ,
                        mainEndPosition, mainEndQ, offset,
                        targetEndTileX, targetEndTileZ,
                        targetEndPosition, targetEndQ)
                    && Flex::ParallelDyntrackSections(
                        mainSections, offset, sideSections)
                    && Flex::RigidElevationForEndpointHeight(
                        sideSections,
                        targetEndPosition[1] - sideStartPosition[1],
                        sideGrade);

            float solvedSideQ[4] = {0, 0, 0, 1};
            if(caseOk) {
                Quat::rotateY(solvedSideQ, solvedSideQ, -startYaw);
                Quat::rotateX(solvedSideQ, solvedSideQ,
                        std::asin(sideGrade / 1000.0f));
            }
            int sideEndTileX = 0;
            int sideEndTileZ = 0;
            float sideEndPosition[3] = {0, 0, 0};
            float sideEndQ[4] = {0, 0, 0, 1};
            caseOk = caseOk && Flex::DyntrackEndpoint(
                    sideStartTileX, sideStartTileZ,
                    sideStartPosition, solvedSideQ, sideSections,
                    sideEndTileX, sideEndTileZ,
                    sideEndPosition, sideEndQ);
            if(caseOk) {
                const double dx = (sideEndTileX - targetEndTileX) * 2048.0
                        + (double)sideEndPosition[0]
                        - (double)targetEndPosition[0];
                const double dz = (sideEndTileZ - targetEndTileZ) * 2048.0
                        + (double)sideEndPosition[2]
                        - (double)targetEndPosition[2];
                caseOk = nearlyEqual(
                            sideEndPosition[1], targetEndPosition[1], 1e-4f)
                        && std::hypot(dx, dz) < 0.03
                        && ((offset > 0.0f && sideGrade > mainGrade)
                            || (offset < 0.0f && sideGrade < mainGrade));
            }
            if(caseOk) {
                passed++;
            } else {
                failed++;
                qWarning() << "[tests:flex-point] failed: elevated parallel endpoint"
                        << "offset=" << offset
                        << "grade=" << sideGrade
                        << "expectedY=" << targetEndPosition[1]
                        << "actualY=" << sideEndPosition[1];
            }
        }
    }

    int exactRadiusCaseCount = 0;
    {
        exactRadiusCaseCount++;
        const float expectedRadius = 10000.0f;
        const float turn = 0.005f;
        float p1[3] = {0, 0, 0};
        float p2[3] = {
            expectedRadius * (1.0f - std::cos(turn)),
            0,
            -expectedRadius * std::sin(turn)
        };
        float q1[4] = {0, 0, 0, 1};
        float q2[4] = {0, turn, 0, 1};
        float sections[10] = {0};
        const bool solved = Flex::NewFlex(
                0, 0, p1, q1,
                0, 0, p2, q2,
                sections, 0.0f, false);
        const bool caseOk = solved
                && nearlyEqual(sections[2], turn, 1e-5f)
                && nearlyEqual(sections[3], expectedRadius, 0.5f);
        if(caseOk) {
            passed++;
        } else {
            failed++;
            qWarning() << "[tests:flex-point] failed: shallow snapped radius"
                    << "solved=" << solved
                    << "sections=" << sections[0] << sections[2]
                    << sections[3] << sections[4]
                    << sections[6] << sections[7] << sections[8];
        }
    }
    {
        exactRadiusCaseCount++;
        const float expectedRadius = 333.0f;
        const float turn = 0.6f;
        float p1[3] = {0, 0, 0};
        float p2[3] = {
            expectedRadius * (1.0f - std::cos(turn)),
            0,
            -expectedRadius * std::sin(turn)
        };
        float q1[4] = {0, 0, 0, 1};
        float q2[4] = {0, turn, 0, 1};
        float sections[10] = {0};
        const bool solved = Flex::NewFlex(
                0, 0, p1, q1,
                0, 0, p2, q2,
                sections, 0.0f, false);
        const bool caseOk = solved
                && std::fabs(sections[0]) < 0.01f
                && std::fabs(sections[4]) < 0.01f
                && std::fabs(sections[8]) < 0.01f
                && nearlyEqual(sections[2], turn, 1e-4f)
                && nearlyEqual(sections[3], expectedRadius, 0.01f)
                && std::fabs(sections[6]) < 0.01f;
        if(caseOk) {
            passed++;
        } else {
            failed++;
            qWarning() << "[tests:flex-point] failed: exact snapped radius"
                    << "solved=" << solved
                    << "sections=" << sections[0] << sections[2]
                    << sections[3] << sections[4]
                    << sections[6] << sections[7] << sections[8];
        }
    }
    {
        exactRadiusCaseCount++;
        const float expectedRadius = 333.0f;
        const float turn = 0.6f;
        const float endStraight = 3.0f;
        float p1[3] = {0, 0, 0};
        float p2[3] = {
            expectedRadius * (1.0f - std::cos(turn))
                    + endStraight * std::sin(turn),
            0,
            -(expectedRadius * std::sin(turn)
                    + endStraight * std::cos(turn))
        };
        float q1[4] = {0, 0, 0, 1};
        float q2[4] = {0, turn, 0, 1};
        float sections[10] = {0};
        const bool solved = Flex::NewFlex(
                0, 0, p1, q1,
                0, 0, p2, q2,
                sections, 0.0f, false);
        const bool caseOk = solved
                && std::fabs(sections[0]) < 0.01f
                && nearlyEqual(sections[2], turn, 0.001f)
                && nearlyEqual(sections[3], expectedRadius, 0.05f)
                && nearlyEqual(sections[4], endStraight, 0.3f)
                && std::fabs(sections[6]) < 0.01f
                && std::fabs(sections[8]) < 0.01f;
        if(caseOk) {
            passed++;
        } else {
            failed++;
            qWarning() << "[tests:flex-point] failed: exact snapped L+C+L boundary radius"
                    << "solved=" << solved
                    << "sections=" << sections[0] << sections[2]
                    << sections[3] << sections[4]
                    << sections[6] << sections[7] << sections[8];
        }
    }
    {
        guardCaseCount++;
        const float roadRadius = 6.0f;
        float p1[3] = {0, 0, 0};
        float p2[3] = {roadRadius, 0, -roadRadius};
        float defaultSections[10] = {0};
        float roadSections[10] = {0};
        const bool defaultRejected = !Flex::NewFlexToPoint(
                0, 0, p1, 0.0f,
                0, 0, p2, defaultSections,
                15.0f);
        const bool roadSolved = Flex::NewFlexToPoint(
                0, 0, p1, 0.0f,
                0, 0, p2, roadSections,
                roadRadius);
        const bool caseOk = defaultRejected
                && roadSolved
                && nearlyEqual(roadSections[3], roadRadius, 0.001f);
        if(caseOk)
            passed++;
        else {
            failed++;
            qWarning() << "[tests:flex-point] failed: road free-point minimum radius"
                    << "defaultRejected=" << defaultRejected
                    << "roadSolved=" << roadSolved
                    << "radius=" << roadSections[3];
        }
    }
    {
        guardCaseCount++;
        const float roadRadius = 6.0f;
        const float turn = 0.6f;
        float p1[3] = {0, 0, 0};
        float p2[3] = {
            roadRadius * (1.0f - std::cos(turn)),
            0,
            -roadRadius * std::sin(turn)
        };
        float q1[4] = {0, 0, 0, 1};
        float q2[4] = {0, turn, 0, 1};
        float roadSections[10] = {0};
        float strictSections[10] = {0};
        const bool roadSolved = Flex::NewFlex(
                0, 0, p1, q1,
                0, 0, p2, q2,
                roadSections, 0.0f, false, roadRadius);
        const bool strictSolved = Flex::NewFlex(
                0, 0, p1, q1,
                0, 0, p2, q2,
                strictSections, 0.0f, false, 7.0f);
        bool strictFloorRespected = true;
        if(strictSolved) {
            for(int section = 1; section < 5; section += 2) {
                const int angleIndex = section * 2;
                const int radiusIndex = angleIndex + 1;
                if(std::fabs(strictSections[angleIndex]) > 1e-6f
                        && strictSections[radiusIndex] < 7.0f - 0.001f)
                    strictFloorRespected = false;
            }
        }
        const bool caseOk = roadSolved
                && nearlyEqual(roadSections[3], roadRadius, 0.01f)
                && strictFloorRespected;
        if(caseOk)
            passed++;
        else {
            failed++;
            qWarning() << "[tests:flex-point] failed: snapped hard minimum radius"
                    << "roadSolved=" << roadSolved
                    << "roadRadius=" << roadSections[3]
                    << "strictSolved=" << strictSolved
                    << "strictFloorRespected=" << strictFloorRespected;
        }
    }
    {
        guardCaseCount++;
        float p1[3] = {0, 0, 0};
        float p2[3] = {0.17f, 0, -100.0f};
        float sections[10] = {0};
        const float gridTolerance = 0.25f / std::sqrt(2.0f);
        const bool solved = Flex::NewFlexToPoint(
                0, 0, p1, 0.0f,
                0, 0, p2, sections,
                15.0f, gridTolerance);
        const bool caseOk = solved
                && sections[0] > 99.9f
                && std::fabs(sections[2]) < 1e-6f
                && std::fabs(sections[6]) < 1e-6f;
        if(caseOk)
            passed++;
        else {
            failed++;
            qWarning() << "[tests:flex-point] failed: grid-aware straight tolerance";
        }
    }
    {
        guardCaseCount++;
        Vector2f inner(0.7175f, 0.0f);
        Vector2f outer(0.8675f, 0.0f);
        inner.rotate(0.001f, 200000000.0f);
        outer.rotate(0.001f, 200000000.0f);
        const float dx = outer.x - inner.x;
        const float dy = outer.y - inner.y;
        const float separation = std::sqrt(dx * dx + dy * dy);
        if(nearlyEqual(separation, 0.15f, 1e-4f))
            passed++;
        else {
            failed++;
            qWarning() << "[tests:flex-point] failed: large-radius rail separation"
                    << "separation=" << separation;
        }
    }
    {
        guardCaseCount++;
        DynTrackObj dyntrack;
        dyntrack.load(0, 0);
        float sections[10] = {0, 0, 0.005f, 10000.0f, 0, 0, 0, 0, 0, 0};
        dyntrack.set(QStringLiteral("dyntrackdata"), sections);
        if(dyntrack.sections[1].sectIdx == 0)
            passed++;
        else {
            failed++;
            qWarning() << "[tests:flex-point] failed: shallow curve storage guard";
        }
    }
    {
        guardCaseCount++;
        float startPosition[3] = {0, 2, 0};
        float targetPosition[3] = {20, 7, -20};
        int planarTileX = 0;
        int planarTileZ = 0;
        float planarPosition[3] = {0, 0, 0};
        float gradePromille = 0.0f;
        float sections[10] = {0};
        const bool prepared = Flex::ElevatedPlanarTarget(
                0, 0, startPosition, 0.0f,
                0, 0, targetPosition,
                planarTileX, planarTileZ, planarPosition, gradePromille);
        const bool solved = prepared && Flex::NewFlexToPoint(
                0, 0, startPosition, 0.0f,
                planarTileX, planarTileZ, planarPosition,
                sections, 5.0f);
        float startQ[4] = {0, 0, 0, 1};
        if(solved)
            Quat::rotateX(startQ, startQ,
                    std::asin(gradePromille / 1000.0f));
        int endTileX = 0;
        int endTileZ = 0;
        float endPosition[3] = {0, 0, 0};
        float endQ[4] = {0, 0, 0, 1};
        const bool endpointOk = solved && Flex::DyntrackEndpoint(
                0, 0, startPosition, startQ, sections,
                endTileX, endTileZ, endPosition, endQ);
        const bool caseOk = endpointOk
                && endTileX == 0
                && endTileZ == 0
                && nearlyEqual(endPosition[0], targetPosition[0], 0.01f)
                && nearlyEqual(endPosition[1], targetPosition[1], 0.01f)
                && nearlyEqual(endPosition[2], targetPosition[2], 0.01f);
        if(caseOk)
            passed++;
        else {
            failed++;
            qWarning() << "[tests:flex-point] failed: elevated curved endpoint"
                    << "prepared=" << prepared << "solved=" << solved
                    << "grade=" << gradePromille
                    << "end=" << endTileX << endTileZ
                    << endPosition[0] << endPosition[1] << endPosition[2];
        }
    }
    {
        guardCaseCount++;
        const int startTileX = 100;
        const int startTileZ = -200;
        const float yaw = 0.8f;
        const double lateral = 15.0;
        const double forward = 30.0;
        const double dx = lateral * std::cos((double)yaw)
                + forward * std::sin((double)yaw);
        const double dz = lateral * std::sin((double)yaw)
                - forward * std::cos((double)yaw);
        float startPosition[3] = {1020, 10, -1000};
        const double targetAbsoluteX = startTileX * 2048.0
                + (double)startPosition[0] + dx;
        const double targetAbsoluteZ = startTileZ * 2048.0
                + (double)startPosition[2] + dz;
        const int targetTileX = (int)std::floor(
                (targetAbsoluteX + 1024.0) / 2048.0);
        const int targetTileZ = (int)std::floor(
                (targetAbsoluteZ + 1024.0) / 2048.0);
        float targetPosition[3] = {
            (float)(targetAbsoluteX - targetTileX * 2048.0),
            6.0f,
            (float)(targetAbsoluteZ - targetTileZ * 2048.0)
        };
        int planarTileX = 0;
        int planarTileZ = 0;
        float planarPosition[3] = {0, 0, 0};
        float gradePromille = 0.0f;
        float sections[10] = {0};
        const bool prepared = Flex::ElevatedPlanarTarget(
                startTileX, startTileZ, startPosition, yaw,
                targetTileX, targetTileZ, targetPosition,
                planarTileX, planarTileZ, planarPosition, gradePromille);
        const bool solved = prepared && Flex::NewFlexToPoint(
                startTileX, startTileZ, startPosition, yaw,
                planarTileX, planarTileZ, planarPosition,
                sections, 5.0f);
        float startQ[4] = {0, 0, 0, 1};
        Quat::rotateY(startQ, startQ, -yaw);
        if(solved)
            Quat::rotateX(startQ, startQ,
                    std::asin(gradePromille / 1000.0f));
        int endTileX = 0;
        int endTileZ = 0;
        float endPosition[3] = {0, 0, 0};
        float endQ[4] = {0, 0, 0, 1};
        const bool endpointOk = solved && Flex::DyntrackEndpoint(
                startTileX, startTileZ, startPosition, startQ, sections,
                endTileX, endTileZ, endPosition, endQ);
        const bool caseOk = endpointOk
                && endTileX == targetTileX
                && endTileZ == targetTileZ
                && nearlyEqual(endPosition[0], targetPosition[0], 0.01f)
                && nearlyEqual(endPosition[1], targetPosition[1], 0.01f)
                && nearlyEqual(endPosition[2], targetPosition[2], 0.01f);
        if(caseOk)
            passed++;
        else {
            failed++;
            qWarning() << "[tests:flex-point] failed: rotated elevated tile endpoint"
                    << "prepared=" << prepared << "solved=" << solved
                    << "expected=" << targetTileX << targetTileZ
                    << targetPosition[0] << targetPosition[1] << targetPosition[2]
                    << "actual=" << endTileX << endTileZ
                    << endPosition[0] << endPosition[1] << endPosition[2];
        }
    }
    {
        guardCaseCount++;
        DynTrackObj dyntrack;
        dyntrack.load(0, 0);
        float yawQ[4] = {0, 0, 0, 1};
        Quat::rotateY(yawQ, yawQ, -0.7f);
        dyntrack.setQdirection(yawQ);
        dyntrack.setElevation(120.0f);
        dyntrack.setElevation(-45.0f);
        const float actualPromille = std::sin(dyntrack.getElevation()) * 1000.0f;
        if(nearlyEqual(actualPromille, -45.0f, 0.001f))
            passed++;
        else {
            failed++;
            qWarning() << "[tests:flex-point] failed: exact absolute elevation"
                    << "actual=" << actualPromille;
        }
    }
    {
        guardCaseCount++;
        float sections[10] = {0};
        sections[2] = (float)M_PI * 0.5f - 0.001f;
        sections[3] = 100.0f;
        const bool belowNinetyAccepted =
                Flex::CanUseRigidElevation(sections);
        sections[2] = (float)M_PI * 0.5f + 0.001f;
        const bool aboveNinetyRejected =
                !Flex::CanUseRigidElevation(sections);
        sections[2] = (float)M_PI * 0.4f;
        sections[6] = (float)M_PI * 0.2f;
        sections[7] = 100.0f;
        const bool cumulativeTurnRejected =
                !Flex::CanUseRigidElevation(sections);
        if(belowNinetyAccepted && aboveNinetyRejected
                && cumulativeTurnRejected)
            passed++;
        else {
            failed++;
            qWarning() << "[tests:flex-point] failed: rigid elevation turn limit";
        }
    }
    int tdbBoundaryCaseCount = 0;
    {
        tdbBoundaryCaseCount++;

        // Regression captured from route bbb, DynTrack UiD 101. The second
        // vector section follows a sizeable curve, so the former tangent-
        // relative append pitch produced a clearly excessive height.
        constexpr int shapeId = 900000;
        constexpr int firstSectionId = 900001;
        TSectionDAT sectionDat(false, false);
        TSection firstCurve(firstSectionId, 1, -0.634851f, 100.0f);
        TSection firstCurveReverse(
                firstSectionId + 1, 1, 0.634851f, 100.0f);
        TSection middleStraight(firstSectionId + 2, 0, 87.7f, 0.0f);
        TSection finalCurveReverse(
                firstSectionId + 3, 1, -0.0432589f, 100.0f);
        TSection finalCurve(
                firstSectionId + 4, 1, 0.0432589f, 100.0f);
        sectionDat.sekcja[firstSectionId] = &firstCurve;
        sectionDat.sekcja[firstSectionId + 1] = &firstCurveReverse;
        sectionDat.sekcja[firstSectionId + 2] = &middleStraight;
        sectionDat.sekcja[firstSectionId + 3] = &finalCurveReverse;
        sectionDat.sekcja[firstSectionId + 4] = &finalCurve;

        TrackShape::SectionIdx path = {};
        path.n = 3;
        path.sect[0] = firstSectionId;
        path.sect[1] = firstSectionId + 2;
        path.sect[2] = firstSectionId + 4;
        TrackShape shape(shapeId);
        shape.dyntrack = false;
        shape.numpaths = 1;
        shape.path = &path;
        sectionDat.shape[shapeId] = &shape;

        const int startTileX = -5306;
        const int startTileZ = -14961;
        float startPosition[3] = {-313.659821f, 0.983149f, -91.258560f};
        float startQ[4] = {0.0630665f, 0.0f, 0.0f, 0.998009f};

        TDB database(&sectionDat, false);
        bool caseOk = database.placeTrack(
                startTileX, startTileZ, startPosition, startQ,
                shapeId, 101);

        int vectorNodeId = -1;
        TRnode* vectorNode = NULL;
        if(caseOk) {
            for(const auto &entry : database.trackNodes) {
                if(entry.second != NULL && entry.second->typ == 1) {
                    vectorNodeId = entry.first;
                    vectorNode = entry.second;
                    break;
                }
            }
            caseOk = vectorNode != NULL && vectorNode->iTrv == 3;
        }

        auto storedPositionMatches = [](
                int actualTileX, int actualTdbTileZ,
                float actualX, float actualY, float actualTdbZ,
                int expectedTileX, int expectedTileZ,
                const float *expectedPosition) {
            const double dx = actualTileX * 2048.0 + (double)actualX
                    - (expectedTileX * 2048.0 + (double)expectedPosition[0]);
            const double dy = (double)actualY - (double)expectedPosition[1];
            const double dz = (-actualTdbTileZ) * 2048.0 - (double)actualTdbZ
                    - (expectedTileZ * 2048.0 + (double)expectedPosition[2]);
            return std::sqrt(dx * dx + dy * dy + dz * dz) < 0.002;
        };

        float prefixSections[10] = {0};
        const float sectionValues[3][2] = {
            {-0.634851f, 100.0f},
            {87.7f, 0.0f},
            {0.0432589f, 100.0f}
        };
        const int dyntrackSlots[3] = {1, 2, 3};
        float cumulativeLength = 0.0f;
        bool transportedRollFound = false;
        for(int prefix = 0; caseOk && prefix < 3; prefix++) {
            prefixSections[dyntrackSlots[prefix] * 2] = sectionValues[prefix][0];
            prefixSections[dyntrackSlots[prefix] * 2 + 1] = sectionValues[prefix][1];
            int expectedTileX = 0;
            int expectedTileZ = 0;
            float expectedPosition[3] = {0};
            float expectedQ[4] = {0, 0, 0, 1};
            caseOk = Flex::DyntrackEndpoint(
                    startTileX, startTileZ, startPosition, startQ,
                    prefixSections, expectedTileX, expectedTileZ,
                    expectedPosition, expectedQ);
            if(!caseOk)
                break;

            TSection* currentSection = sectionDat.sekcja[
                    path.sect[prefix]];
            cumulativeLength += currentSection->getDlugosc();

            float drawPosition[7] = {0};
            caseOk = database.getDrawPositionOnTrNode(
                    drawPosition, vectorNodeId, cumulativeLength);
            if(caseOk) {
                const double drawAbsoluteX = drawPosition[5] * 2048.0
                        + drawPosition[0];
                const double drawAbsoluteZ = -drawPosition[6] * 2048.0
                        - drawPosition[2];
                const double expectedAbsoluteX = expectedTileX * 2048.0
                        + expectedPosition[0];
                const double expectedAbsoluteZ = expectedTileZ * 2048.0
                        + expectedPosition[2];
                const double dx = drawAbsoluteX - expectedAbsoluteX;
                const double dy = drawPosition[1] - expectedPosition[1];
                const double dz = drawAbsoluteZ - expectedAbsoluteZ;
                caseOk = std::sqrt(dx * dx + dy * dy + dz * dz)
                        < 0.002;
            }

            const float* frame =
                    vectorNode->trVectorSection[prefix].param + 13;
            caseOk = caseOk
                    && std::isfinite(frame[0])
                    && std::isfinite(frame[1])
                    && std::isfinite(frame[2]);
            if(prefix > 0 && std::fabs(frame[2]) > 1e-5f)
                transportedRollFound = true;

            if(caseOk && prefix < 2) {
                const float* stored =
                        vectorNode->trVectorSection[prefix + 1].param;
                caseOk = storedPositionMatches(
                        (int)stored[8], (int)stored[9],
                        stored[10], stored[11], stored[12],
                        expectedTileX, expectedTileZ, expectedPosition);
            } else if(caseOk) {
                TRnode* endNode =
                        database.trackNodes[vectorNode->TrPinS[1]];
                caseOk = endNode != NULL && storedPositionMatches(
                        (int)endNode->UiD[4], (int)endNode->UiD[5],
                        endNode->UiD[6], endNode->UiD[7], endNode->UiD[8],
                        expectedTileX, expectedTileZ, expectedPosition);
            }
        }

        caseOk = caseOk && transportedRollFound;

        struct AbsoluteSample {
            double x;
            double y;
            double z;
        };
        auto sampleVector = [&database](int nodeId, float distance,
                AbsoluteSample &sample) {
            float drawPosition[7] = {0};
            if(!database.getDrawPositionOnTrNode(
                    drawPosition, nodeId, distance))
                return false;
            sample.x = drawPosition[5] * 2048.0 + drawPosition[0];
            sample.y = drawPosition[1];
            sample.z = -drawPosition[6] * 2048.0 - drawPosition[2];
            return std::isfinite(sample.x)
                    && std::isfinite(sample.y)
                    && std::isfinite(sample.z);
        };
        auto samplesMatch = [](const AbsoluteSample &a,
                const AbsoluteSample &b) {
            const double dx = a.x - b.x;
            const double dy = a.y - b.y;
            const double dz = a.z - b.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz) < 0.002;
        };

        const float firstLength = firstCurve.getDlugosc();
        const float middleLength = middleStraight.getDlugosc();
        const float finalLength = finalCurve.getDlugosc();
        const float totalLength = firstLength + middleLength + finalLength;
        const float sampleDistances[] = {
            0.0f,
            firstLength * 0.5f,
            firstLength,
            firstLength + middleLength * 0.5f,
            firstLength + middleLength,
            firstLength + middleLength + finalLength * 0.5f,
            totalLength
        };
        AbsoluteSample forwardSamples[7] = {};
        for(int i = 0; caseOk && i < 7; i++)
            caseOk = sampleVector(
                    vectorNodeId, sampleDistances[i], forwardSamples[i]);

        // The yellow/collision-line matrix must describe the same path as
        // the hot TDB sampler. The final curve has transported roll and its
        // second generated point is exactly four metres into the section.
        QVector<float> finalCurvePoints;
        if(caseOk) {
            database.getVectorSectionPoints(startTileX, -startTileZ,
                    vectorNodeId, 2, finalCurvePoints);
            caseOk = finalCurvePoints.size() >= 6;
        }
        if(caseOk) {
            AbsoluteSample sampledPoint = {};
            caseOk = sampleVector(vectorNodeId,
                    firstLength + middleLength + 4.0f, sampledPoint);
            const double expectedX = sampledPoint.x
                    - startTileX * 2048.0;
            const double expectedZ = sampledPoint.z
                    - startTileZ * 2048.0;
            const double dx = finalCurvePoints[3] - expectedX;
            const double dy = finalCurvePoints[4] - sampledPoint.y;
            const double dz = finalCurvePoints[5] - expectedZ;
            caseOk = std::sqrt(dx * dx + dy * dy + dz * dz) < 0.002;
        }

        caseOk = caseOk && database.rotate(vectorNodeId) == 0;
        if(caseOk) {
            vectorNode = database.trackNodes[vectorNodeId];
            caseOk = vectorNode != NULL
                    && (int)vectorNode->trVectorSection[0].param[0]
                            == firstSectionId + 3
                    && (int)vectorNode->trVectorSection[1].param[0]
                            == firstSectionId + 2
                    && (int)vectorNode->trVectorSection[2].param[0]
                            == firstSectionId + 1;
        }
        for(int i = 0; caseOk && i < 7; i++) {
            AbsoluteSample reversedSample = {};
            caseOk = sampleVector(vectorNodeId,
                    totalLength - sampleDistances[i], reversedSample)
                    && samplesMatch(forwardSamples[i], reversedSample);
        }

        caseOk = caseOk && database.rotate(vectorNodeId) == 0;
        for(int i = 0; caseOk && i < 7; i++) {
            AbsoluteSample restoredSample = {};
            caseOk = sampleVector(
                    vectorNodeId, sampleDistances[i], restoredSample)
                    && samplesMatch(forwardSamples[i], restoredSample);
        }

        if(caseOk)
            passed++;
        else {
            failed++;
            qWarning() << "[tests:flex-point] failed: complete TDB subsection frames";
        }
    }
    {
        tdbBoundaryCaseCount++;

        // Route bbb endpoints 308-309, vector 310. These three identical
        // one-degree curves were independently rotated before being joined;
        // their stored yaws are intentionally not tangent-continuous.
        constexpr int forwardSectionId = 36170;
        constexpr int reverseSectionId = 36171;
        constexpr int startNodeId = 309;
        constexpr int endNodeId = 308;
        constexpr int vectorNodeId = 310;
        TSectionDAT sectionDat(false, false);
        TSection forwardSection(
                forwardSectionId, 1, -(float)M_PI / 180.0f, 1000.0f);
        TSection reverseSection(
                reverseSectionId, 1, (float)M_PI / 180.0f, 1000.0f);
        sectionDat.sekcja[forwardSectionId] = &forwardSection;
        sectionDat.sekcja[reverseSectionId] = &reverseSection;

        TDB database(&sectionDat, false);
        TRnode *startNode = new TRnode();
        TRnode *endNode = new TRnode();
        TRnode *vectorNode = new TRnode();
        database.trackNodes[startNodeId] = startNode;
        database.trackNodes[endNodeId] = endNode;
        database.trackNodes[vectorNodeId] = vectorNode;
        database.iTRnodes = vectorNodeId;

        startNode->typ = 0;
        const float startUid[12] = {
            -5306, 14961, 133, 0, -5306, 14961,
            -755.71405f, 0.97610611f, -280.55771f,
            0.0f, (float)M_PI, 0.0f
        };
        std::copy(startUid, startUid + 12, startNode->UiD);
        startNode->TrPinS[0] = vectorNodeId;
        startNode->TrPinK[0] = 1;

        endNode->typ = 0;
        const float endUid[12] = {
            -5306, 14961, 135, 1, -5306, 14961,
            -746.98889f, 0.9761017f, -230.54849f,
            -0.00049897865f, -0.0023593903f, 1.1773519e-06f
        };
        std::copy(endUid, endUid + 12, endNode->UiD);
        endNode->TrPinS[0] = vectorNodeId;
        endNode->TrPinK[0] = 0;

        vectorNode->typ = 1;
        vectorNode->iTrv = 3;
        vectorNode->trVectorSection = new TRnode::TRSect[3];
        const float capturedSections[3][16] = {
            {36170, 33262, -5306, 14961, 133, 0, 1, 0,
             -5306, 14961, -755.71405f, 0.97610611f, -280.55771f,
             0.0f, 0.0f, 0.0f},
            {36170, 33262, -5306, 14961, 134, 0, 1, 0,
             -5306, 14961, -755.86633f, 0.97610611f, -263.10532f,
             0.0f, 0.5325464f, 0.0f},
            {36170, 33262, -5306, 14961, 135, 0, 1, 0,
             -5306, 14961, -747.13647f, 0.97610611f, -247.99245f,
             0.0f, 0.015093067f, 0.0f}
        };
        for(int i = 0; i < 3; ++i)
            std::copy(capturedSections[i], capturedSections[i] + 16,
                    vectorNode->trVectorSection[i].param);
        vectorNode->TrPinS[0] = startNodeId;
        vectorNode->TrPinS[1] = endNodeId;
        vectorNode->TrPinK[0] = 1;
        vectorNode->TrPinK[1] = 1;

        struct CapturedSample {
            double x;
            double y;
            double z;
        };
        auto sample = [&database](float distance, CapturedSample &result) {
            float drawPosition[7] = {0};
            if(!database.getDrawPositionOnTrNode(
                    drawPosition, vectorNodeId, distance))
                return false;
            result.x = drawPosition[5] * 2048.0 + drawPosition[0];
            result.y = drawPosition[1];
            result.z = -drawPosition[6] * 2048.0 - drawPosition[2];
            return true;
        };
        auto matches = [](const CapturedSample &a, const CapturedSample &b) {
            const double dx = a.x - b.x;
            const double dy = a.y - b.y;
            const double dz = a.z - b.z;
            // The captured final curve and endpoint 308 disagree by about
            // 3.75 cm. Preserve that stored boundary rather than hiding it
            // by regenerating a new endpoint during reversal.
            return std::sqrt(dx * dx + dy * dy + dz * dz) < 0.05;
        };

        const float sectionLength = forwardSection.getDlugosc();
        const float totalLength = sectionLength * 3.0f;
        const float distances[6] = {
            sectionLength * 0.25f, sectionLength * 0.75f,
            sectionLength * 1.25f, sectionLength * 1.75f,
            sectionLength * 2.25f, sectionLength * 2.75f
        };
        CapturedSample forwardSamples[6] = {};
        bool caseOk = true;
        for(int i = 0; caseOk && i < 6; ++i)
            caseOk = sample(distances[i], forwardSamples[i]);
        const float originalStartFrame[3] = {
            startNode->UiD[9], startNode->UiD[10], startNode->UiD[11]
        };
        const float originalEndFrame[3] = {
            endNode->UiD[9], endNode->UiD[10], endNode->UiD[11]
        };

        caseOk = caseOk && database.rotate(vectorNodeId) == 0;
        if(caseOk) {
            vectorNode = database.trackNodes[vectorNodeId];
            caseOk = vectorNode != NULL
                    && std::equal(endNode->UiD + 4, endNode->UiD + 9,
                            vectorNode->trVectorSection[0].param + 8)
                    && std::equal(capturedSections[2] + 8,
                            capturedSections[2] + 13,
                            vectorNode->trVectorSection[1].param + 8)
                    && std::equal(capturedSections[1] + 8,
                            capturedSections[1] + 13,
                            vectorNode->trVectorSection[2].param + 8);
        }
        for(int i = 0; caseOk && i < 6; ++i) {
            CapturedSample reversedSample = {};
            const bool sampled = sample(
                    totalLength - distances[i], reversedSample);
            caseOk = sampled && matches(forwardSamples[i], reversedSample);
        }
        const bool endpointFramesUnchanged =
                std::equal(originalStartFrame, originalStartFrame + 3,
                        startNode->UiD + 9)
                && std::equal(originalEndFrame, originalEndFrame + 3,
                        endNode->UiD + 9);
        caseOk = caseOk && endpointFramesUnchanged;

        caseOk = caseOk && database.rotate(vectorNodeId) == 0;
        for(int i = 0; caseOk && i < 6; ++i) {
            CapturedSample restoredSample = {};
            caseOk = sample(distances[i], restoredSample)
                    && matches(forwardSamples[i], restoredSample);
        }

        if(caseOk)
            passed++;
        else {
            failed++;
            qWarning() << "[tests:flex-point] failed: stored-boundary vector reversal";
        }
    }
    {
        guardCaseCount++;
        float p[3] = {0, 0, 0};
        float q[4] = {0, 0, 0, 1};
        int targetTileX = 0;
        int targetTileZ = 0;
        float targetPosition[3] = {0, 0, 0};
        float targetQ[4] = {0, 0, 0, 1};
        if(!Flex::OffsetWorldPose(
                0, 0, p, q,
                std::numeric_limits<float>::quiet_NaN(),
                targetTileX, targetTileZ, targetPosition, targetQ))
            passed++;
        else {
            failed++;
            qWarning() << "[tests:flex-point] failed: non-finite offset guard";
        }
    }
    const int caseCount = (int)(sizeof(cases) / sizeof(cases[0]))
            + (int)(sizeof(yawCases) / sizeof(yawCases[0]))
            + (int)(sizeof(offsetCases) / sizeof(offsetCases[0]))
            + endpointOrientationCaseCount
            + parallelCaseCount
            + exactRadiusCaseCount
            + tdbBoundaryCaseCount
            + guardCaseCount;
    qInfo() << "[tests:flex-point] cases=" << caseCount
            << "passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}

static int runProceduralPolicySuite(bool verbose) {
    int passed = 0;
    int failed = 0;
    const QStringList templates = {"DefaultTrack", "AsphaltRoad"};

    auto expect = [&](const char *name, ProceduralTracksMode mode, const QString &request,
            ProceduralTrackBackend backend, const QString &resolved,
            bool missingRequested = false) {
        const ProceduralTrackResolution actual =
                ProceduralTrackPolicy::resolve(mode, request, templates);
        const bool ok = actual.backend == backend
                && actual.templateName == resolved
                && actual.missingRequested == missingRequested;
        if(ok){
            passed++;
            if(verbose)
                qInfo() << "[tests:procedural-policy] PASS" << name;
        } else {
            failed++;
            qWarning() << "[tests:procedural-policy] FAIL" << name
                       << "backend=" << (int)actual.backend
                       << "template=" << actual.templateName
                       << "missingRequested=" << actual.missingRequested;
        }
    };

    expect("disabled-default", ProceduralTracksMode::Disabled, "DEFAULT",
           ProceduralTrackBackend::Fallback, "");
    expect("forced-empty", ProceduralTracksMode::Forced, "",
           ProceduralTrackBackend::Procedural, "DefaultTrack");
    expect("forced-disabled", ProceduralTracksMode::Forced, "DISABLED",
           ProceduralTrackBackend::Fallback, "");
    expect("forced-custom", ProceduralTracksMode::Forced, "asphaltroad",
           ProceduralTrackBackend::Procedural, "AsphaltRoad");
    expect("forced-missing", ProceduralTracksMode::Forced, "Missing",
           ProceduralTrackBackend::Procedural, "DefaultTrack", true);
    expect("enabled-empty", ProceduralTracksMode::Enabled, "",
           ProceduralTrackBackend::Fallback, "");
    expect("enabled-default", ProceduralTracksMode::Enabled, "DEFAULT",
           ProceduralTrackBackend::Procedural, "DefaultTrack");
    expect("enabled-disabled", ProceduralTracksMode::Enabled, "disabled",
           ProceduralTrackBackend::Fallback, "");
    expect("enabled-custom", ProceduralTracksMode::Enabled, "AsphaltRoad",
           ProceduralTrackBackend::Procedural, "AsphaltRoad");
    expect("enabled-missing", ProceduralTracksMode::Enabled, "Missing",
           ProceduralTrackBackend::Fallback, "", true);

    const ProceduralTrackResolution noDefault = ProceduralTrackPolicy::resolve(
            ProceduralTracksMode::Forced, "Missing", {"AsphaltRoad"});
    if(noDefault.backend == ProceduralTrackBackend::Fallback
            && noDefault.missingRequested && noDefault.missingDefault){
        passed++;
    } else {
        failed++;
        qWarning() << "[tests:procedural-policy] FAIL missing-default";
    }

    const bool modesOk =
            ProceduralTrackPolicy::modeFromSetting("true") == ProceduralTracksMode::Forced
            && ProceduralTrackPolicy::modeFromSetting("FORCED") == ProceduralTracksMode::Forced
            && ProceduralTrackPolicy::modeFromSetting("enabled") == ProceduralTracksMode::Enabled
            && ProceduralTrackPolicy::modeFromSetting("false") == ProceduralTracksMode::Disabled
            && ProceduralTrackPolicy::modeFromSetting("disabled") == ProceduralTracksMode::Disabled;
    if(modesOk)
        passed++;
    else {
        failed++;
        qWarning() << "[tests:procedural-policy] FAIL setting-mapping";
    }

    qInfo() << "[tests:procedural-policy] cases=" << (passed + failed)
            << "passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}

static int runDynTrackRoadSuite(bool verbose) {
    int passed = 0;
    int failed = 0;

    auto check = [&](bool condition, const char *name) {
        if(condition){
            passed++;
            if(verbose)
                qInfo() << "[tests:dyntrack-road] PASS" << name;
        } else {
            failed++;
            qWarning() << "[tests:dyntrack-road] FAIL" << name;
        }
    };

    check(!DynTrackObj::isRoadStaticFlags(
                  DynTrackObj::DefaultStaticFlags),
          "rail-static-flags");
    check(DynTrackObj::isRoadStaticFlags(
                  DynTrackObj::RoadStaticFlags),
          "road-static-flags");

    Ref generated;
    generated.ensureDynTrackItems();
    generated.ensureDynTrackItems();
    int generatedRail = 0;
    int generatedRoad = 0;
    for(auto it = generated.refItems.begin();
            it != generated.refItems.end(); ++it){
        for(const Ref::RefItem &item : it.value()){
            if(item.type != "dyntrack")
                continue;
            if(DynTrackObj::isRoadStaticFlags(item.staticFlags))
                generatedRoad++;
            else
                generatedRail++;
        }
    }
    check(generatedRail == 1 && generatedRoad == 1,
          "generate-missing-items-once");

    QString serialized;
    QTextStream generatedStream(&serialized);
    generated.saveToStream(&generatedStream);
    generatedStream.flush();
    check(serialized.trimmed().isEmpty(),
          "generated-items-are-transient");

    Ref legacy;
    Ref::RefItem legacyRail;
    legacyRail.type = "dyntrack";
    legacyRail.clas = "Dynamic track";
    legacyRail.filename.push_back("DYNTRACK");
    legacy.refItems[legacyRail.clas].push_back(legacyRail);
    legacy.ensureDynTrackItems();

    int legacyRailCount = 0;
    int legacyRoadCount = 0;
    for(const Ref::RefItem &item : legacy.refItems["Dynamic track"]){
        if(DynTrackObj::isRoadStaticFlags(item.staticFlags))
            legacyRoadCount++;
        else
            legacyRailCount++;
        check(item.value == -1, "dyntrack-ref-has-no-trackshape");
    }
    check(legacyRailCount == 1 && legacyRoadCount == 1,
          "legacy-rail-kept-road-added");

    qInfo() << "[tests:dyntrack-road] cases=" << (passed + failed)
            << "passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}

static int runOrtsProfileSuite(bool verbose) {
    const QString stf =
            "SIMISA@@@@@@@@@@JINX0p0t______\n"
            "TrProfile (\n"
            " Name ( \"Test profile\" )\n"
            " LODMethod ( ComponentAdditive )\n"
            " ChordSpan ( 1 )\n"
            " PitchControl ( ChordLength )\n"
            " PitchControlScalar ( 10m )\n"
            " TrackGauge ( 1.435m )\n"
            " SuperElevationMethod ( Outside )\n"
            " LOD (\n"
            "  CutoffRadius ( 1200m )\n"
            "  LODItem (\n"
            "   Name ( Surface ) TexName ( test.ace ) ShaderName ( TexDiff )\n"
            "   LightModelName ( OptSpecular0 ) AlphaTestMode ( 0 )\n"
            "   TexAddrModeName ( Wrap ) ESD_Alternative_Texture ( 0 )\n"
            "   MipMapLevelOfDetailBias ( 0 )\n"
            "   Polyline ( Name ( top ) DeltaTexCoord ( 0 0.2 )\n"
            "    Vertex ( Position ( -1 0.2 ) Normal ( 0 1 0 ) "
            "TexCoord ( 0 0 ) PositionControl ( All ) )\n"
            "    Vertex ( Position ( 1 0.2 ) Normal ( 0 1 0 ) "
            "TexCoord ( 1 0 ) PositionControl ( All ) )\n"
            "   )\n"
            "  )\n"
            " )\n"
            ")\n";
    const QString xml =
            "<TrProfile Name=\"Test profile\" LODMethod=\"ComponentAdditive\" "
            "ChordSpan=\"1\" PitchControl=\"ChordLength\" "
            "PitchControlScalar=\"10\" TrackGauge=\"1.435\" "
            "SuperElevationMethod=\"Outside\">"
            "<LOD CutoffRadius=\"1200\"><LODItem Name=\"Surface\" "
            "TexName=\"test.ace\" ShaderName=\"TexDiff\" "
            "LightModelName=\"OptSpecular0\" AlphaTestMode=\"0\" "
            "TexAddrModeName=\"Wrap\" ESD_Alternative_Texture=\"0\" "
            "MipMapLevelOfDetailBias=\"0\">"
            "<Polyline Name=\"top\" DeltaTexCoord=\"0 0.2\">"
            "<Vertex Position=\"-1 0.2 0\" Normal=\"0 1 0\" "
            "TexCoord=\"0 0\" PositionControl=\"All\"/>"
            "<Vertex Position=\"1 0.2 0\" Normal=\"0 1 0\" "
            "TexCoord=\"1 0\" PositionControl=\"All\"/>"
            "</Polyline></LODItem></LOD></TrProfile>";

    int passed = 0;
    int failed = 0;
    auto check = [&](bool condition, const char *name) {
        if(condition){
            passed++;
            if(verbose)
                qInfo() << "[tests:orts-profile] PASS" << name;
        } else {
            failed++;
            qWarning() << "[tests:orts-profile] FAIL" << name;
        }
    };

    QStringList stfDiagnostics;
    QStringList xmlDiagnostics;
    const QSharedPointer<OrtsTrackProfile> stfProfile =
            OrtsTrackProfileParser::parseStf(stf, "TrProfileStf", &stfDiagnostics);
    const QSharedPointer<OrtsTrackProfile> xmlProfile =
            OrtsTrackProfileParser::parseXml(xml, "TrProfileXml", &xmlDiagnostics);
    check(stfProfile != nullptr && stfProfile->valid, "parse-stf");
    check(xmlProfile != nullptr && xmlProfile->valid, "parse-xml");
    check(stfProfile != nullptr && xmlProfile != nullptr
          && stfProfile->name == xmlProfile->name
          && stfProfile->lods.size() == xmlProfile->lods.size()
          && stfProfile->lods[0].items[0].polylines[0].vertices.size()
             == xmlProfile->lods[0].items[0].polylines[0].vertices.size(),
          "stf-xml-equivalence");

    const QSharedPointer<OrtsTrackProfile> badSignature =
            OrtsTrackProfileParser::parseStf("TrProfile ( )", "Bad");
    const QSharedPointer<OrtsTrackProfile> missingLod =
            OrtsTrackProfileParser::parseXml("<TrProfile Name=\"Bad\"/>", "Bad");
    QString invalidVertexXml = xml;
    invalidVertexXml.replace("Normal=\"0 1 0\"", "Normal=\"0 0 0\"");
    const QSharedPointer<OrtsTrackProfile> invalidVertex =
            OrtsTrackProfileParser::parseXml(invalidVertexXml, "BadVertex");
    check(badSignature == nullptr, "reject-stf-signature");
    check(missingLod != nullptr && !missingLod->valid, "reject-missing-lod");
    check(invalidVertex != nullptr && !invalidVertex->valid,
          "reject-invalid-vertex");

    if(xmlProfile != nullptr){
        QVector<TSection> straight;
        straight.append(TSection(0, 0, 10.0f, 0));
        QVector<OrtsGeneratedProfileMesh> straightMeshes;
        QStringList renderDiagnostics;
        check(OrtsTrackProfileRenderer::buildMeshes(
                      *xmlProfile, straight, straightMeshes, &renderDiagnostics)
              && straightMeshes.size() == 1
              && straightMeshes[0].vertices.size() == 54
              && std::abs(straightMeshes[0].vertices[8] - 1.0f) < 0.001f
              && std::abs(straightMeshes[0].bounds[0] - 1.0f) < 0.001f
              && std::abs(straightMeshes[0].bounds[1] + 1.0f) < 0.001f
              && std::abs(straightMeshes[0].bounds[4] - 10.0f) < 0.001f,
              "straight-geometry");
        check(renderDiagnostics.join(' ').contains("PositionControl")
              && renderDiagnostics.join(' ').contains("LightModelName"),
              "compatibility-diagnostics");

        OrtsTrackProfile blendedProfile = *xmlProfile;
        blendedProfile.lods[0].items[0].shaderName = "BlendATexDiff";
        QVector<OrtsGeneratedProfileMesh> blendedMeshes;
        check(OrtsTrackProfileRenderer::buildMeshes(
                      blendedProfile, straight, blendedMeshes)
              && blendedMeshes.size() == 1
              && blendedMeshes[0].materialPass
                    == OrtsGeneratedProfileMesh::MaterialPass::Blended
              && std::abs(blendedMeshes[0].vertices[8] + 1.0f / 255.0f)
                    < 0.001f,
              "blendatex-transparent-cutoff");

        OrtsTrackProfile alphaTestProfile = blendedProfile;
        alphaTestProfile.lods[0].items[0].alphaTestMode = 1;
        QVector<OrtsGeneratedProfileMesh> alphaTestMeshes;
        check(OrtsTrackProfileRenderer::buildMeshes(
                      alphaTestProfile, straight, alphaTestMeshes)
              && alphaTestMeshes.size() == 1
              && alphaTestMeshes[0].materialPass
                    == OrtsGeneratedProfileMesh::MaterialPass::AlphaTest
              && std::abs(alphaTestMeshes[0].vertices[8] + 0.51f) < 0.001f,
              "alpha-test-cutoff");

        OrtsTrackProfile replacementProfile = *xmlProfile;
        replacementProfile.lodMethod =
                OrtsTrackProfile::LodMethod::CompleteReplacement;
        OrtsProfileLod secondLod = replacementProfile.lods.first();
        secondLod.cutoffRadius = 2400;
        replacementProfile.lods.append(secondLod);
        QVector<OrtsGeneratedProfileMesh> replacementMeshes;
        check(OrtsTrackProfileRenderer::buildMeshes(
                      replacementProfile, straight, replacementMeshes)
              && replacementMeshes.size() == 2
              && replacementMeshes[0].minimumDistance < 0
              && std::abs(replacementMeshes[0].maximumDistance - 1200) < 0.001f
              && std::abs(replacementMeshes[1].minimumDistance - 1200) < 0.001f
              && std::abs(replacementMeshes[1].maximumDistance - 2400) < 0.001f,
              "replacement-lod-ranges");

        QVector<TSection> curves;
        curves.append(TSection(0, 1, (float)M_PI / 2.0f, 100.0f));
        QVector<OrtsGeneratedProfileMesh> curveMeshes;
        bool finite = OrtsTrackProfileRenderer::buildMeshes(
                *xmlProfile, curves, curveMeshes) && !curveMeshes.isEmpty();
        if(finite){
            for(float value : curveMeshes[0].vertices)
                finite = finite && std::isfinite(value);
        }
        check(finite && curveMeshes[0].vertices.size() > 54,
              "curve-subdivision");
        bool endpointFound = false;
        if(finite){
            const QVector<float> &vertices = curveMeshes[0].vertices;
            for(int i = 0; i < vertices.size(); i += 9){
                if(std::abs(vertices[i] + 100.0f) < 0.01f
                        && std::abs(vertices[i + 2] - 100.0f) < 1.01f){
                    endpointFound = true;
                    break;
                }
            }
        }
        check(endpointFound, "curve-endpoint");

        // ComplexLine reports its TSection yaw using TSRE's object-transform
        // convention. A swept cross-section must keep each X-side at a
        // constant radius instead of twisting from the outside of the curve
        // to the inside.
        bool constantCurveSides = false;
        if(finite){
            const QVector<float> &vertices = curveMeshes[0].vertices;
            bool outerEndpointFound = false;
            bool innerEndpointFound = false;
            for(int i = 0; i < vertices.size(); i += 9){
                const float x = vertices[i];
                const float z = vertices[i + 2];
                const float textureU = vertices[i + 6];
                outerEndpointFound = outerEndpointFound
                        || (std::abs(x + 100.0f) < 0.01f
                            && std::abs(z - 101.0f) < 0.01f
                            && std::abs(textureU - 1.0f) < 0.01f);
                innerEndpointFound = innerEndpointFound
                        || (std::abs(x + 100.0f) < 0.01f
                            && std::abs(z - 99.0f) < 0.01f
                            && std::abs(textureU) < 0.01f);
            }
            constantCurveSides = outerEndpointFound && innerEndpointFound;
        }
        check(constantCurveSides, "curve-cross-section-handedness");

        QVector<OrtsGeneratedProfileMesh> apronMeshes;
        bool endApron = OrtsTrackProfileRenderer::buildMeshes(
                *xmlProfile, curves, apronMeshes, nullptr, 0.25f, 0.002f)
                && apronMeshes.size() == 1;
        bool exactApronEndpointFound = false;
        bool loweredApronEndpointFound = false;
        if(endApron){
            const QVector<float> &vertices = apronMeshes[0].vertices;
            for(int i = 0; i < vertices.size(); i += 9){
                const float x = vertices[i];
                const float y = vertices[i + 1];
                exactApronEndpointFound = exactApronEndpointFound
                        || (std::abs(x + 100.0f) < 0.001f
                            && std::abs(y - 0.2f) < 0.0001f);
                loweredApronEndpointFound = loweredApronEndpointFound
                        || (std::abs(x + 100.25f) < 0.001f
                            && std::abs(y - 0.198f) < 0.0001f);
            }
        }
        check(endApron && exactApronEndpointFound
              && loweredApronEndpointFound,
              "road-end-apron-render-only-extension");

        QVector<TSection> reverseCurves;
        reverseCurves.append(TSection(0, 1, -(float)M_PI / 2.0f, 100.0f));
        QVector<OrtsGeneratedProfileMesh> reverseCurveMeshes;
        bool reverseCurveSides = OrtsTrackProfileRenderer::buildMeshes(
                *xmlProfile, reverseCurves, reverseCurveMeshes)
                && !reverseCurveMeshes.isEmpty();
        bool reverseInnerEndpointFound = false;
        bool reverseOuterEndpointFound = false;
        if(reverseCurveSides){
            const QVector<float> &vertices = reverseCurveMeshes[0].vertices;
            for(int i = 0; i < vertices.size(); i += 9){
                const float x = vertices[i];
                const float z = vertices[i + 2];
                const float textureU = vertices[i + 6];
                reverseInnerEndpointFound = reverseInnerEndpointFound
                        || (std::abs(x - 100.0f) < 0.01f
                            && std::abs(z - 99.0f) < 0.01f
                            && std::abs(textureU - 1.0f) < 0.01f);
                reverseOuterEndpointFound = reverseOuterEndpointFound
                        || (std::abs(x - 100.0f) < 0.01f
                            && std::abs(z - 101.0f) < 0.01f
                            && std::abs(textureU) < 0.01f);
            }
        }
        check(reverseCurveSides && reverseInnerEndpointFound
              && reverseOuterEndpointFound,
              "reverse-curve-cross-section-handedness");
    }

    QTemporaryDir temporaryDirectory;
    bool precedenceOk = temporaryDirectory.isValid();
    bool routeOverrideOk = false;
    if(precedenceOk){
        QDir().mkpath(temporaryDirectory.path() + "/TrackProfiles");
        QFile stfFile(temporaryDirectory.path()
                      + "/TrackProfiles/TrProfileDual.stf");
        QFile xmlFile(temporaryDirectory.path()
                      + "/TrackProfiles/TrProfileDual.xml");
        QFile defaultFile(temporaryDirectory.path()
                          + "/TrackProfiles/default_road.stf");
        precedenceOk = stfFile.open(QIODevice::WriteOnly)
                && stfFile.write(stf.toUtf8()) > 0;
        stfFile.close();
        QString precedenceXml = xml;
        precedenceXml.replace("Test profile", "XML wins");
        precedenceOk = precedenceOk && xmlFile.open(QIODevice::WriteOnly)
                && xmlFile.write(precedenceXml.toUtf8()) > 0;
        xmlFile.close();
        QString defaultRoadStf = stf;
        defaultRoadStf.replace("Test profile", "Default Road");
        precedenceOk = precedenceOk && defaultFile.open(QIODevice::WriteOnly)
                && defaultFile.write(defaultRoadStf.toUtf8()) > 0;
        defaultFile.close();
        OrtsTrackProfileCatalog::load(temporaryDirectory.path(), true);
        const QSharedPointer<const OrtsTrackProfile> selected =
                OrtsTrackProfileCatalog::find("TrProfileDual");
        const QSharedPointer<const OrtsTrackProfile> alias =
                OrtsTrackProfileCatalog::find("XML wins");
        precedenceOk = precedenceOk && selected != nullptr
                && selected->name == "XML wins"
                && alias != nullptr && alias->id == "TrProfileDual";
        const QSharedPointer<const OrtsTrackProfile> defaultRoad =
                OrtsTrackProfileCatalog::find("default_road");
        check(defaultRoad != nullptr
              && defaultRoad->name == "Default Road",
              "default-profile-filename");

        QStringList availableNames = OrtsTrackProfileCatalog::selectionNames();
        const QStringList globalNames = {
            "TrProfileDual", "XML wins", "GlobalOnly"
        };
        for(const QString &globalName : globalNames){
            if(OrtsTrackProfileCatalog::find(globalName) == nullptr)
                availableNames.append(globalName);
        }
        const ProceduralTrackResolution idCollision =
                ProceduralTrackPolicy::resolve(
                    ProceduralTracksMode::Forced,
                    "TrProfileDual", availableNames);
        const ProceduralTrackResolution aliasCollision =
                ProceduralTrackPolicy::resolve(
                    ProceduralTracksMode::Forced,
                    "XML wins", availableNames);
        routeOverrideOk = availableNames.indexOf("TrProfileDual")
                        < availableNames.indexOf("GlobalOnly")
                && availableNames.count("TrProfileDual") == 1
                && availableNames.count("XML wins") == 1
                && OrtsTrackProfileCatalog::find(idCollision.templateName)
                        != nullptr
                && OrtsTrackProfileCatalog::find(aliasCollision.templateName)
                        != nullptr;
    }
    check(precedenceOk, "xml-precedence-and-alias");
    check(routeOverrideOk, "route-profile-overrides-global-template");

    qInfo() << "[tests:orts-profile] cases=" << (passed + failed)
            << "passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}

static int runTerrainGridSuite(bool verbose) {
    int passed = 0;
    int failed = 0;
    auto check = [&](bool condition, const char *name) {
        if (condition) {
            ++passed;
            if (verbose)
                qInfo() << "[tests:terrain-grid] PASS" << name;
        } else {
            ++failed;
            qWarning() << "[tests:terrain-grid] FAIL" << name;
        }
    };
    auto accepts = [](int samples, float spacing, int patches = 16,
                      float rotation = 0.0f) {
        TerrainGridLayout layout;
        QString error;
        return TerrainGridLayout::tryCreate(samples, spacing, patches, rotation,
                                             layout, error);
    };

    const Brush defaultBrush;
    check(Brush::TerrainAdjustmentUnitMetres == 1
          && defaultBrush.eSize * Brush::TerrainAdjustmentUnitMetres == 16
          && defaultBrush.eRadius * Brush::TerrainAdjustmentUnitMetres == 160
          && std::abs(Brush::terrainSlopeRatio(45) - 1.0f) < 0.000001f
          && std::abs(Brush::terrainSlopeRatio(defaultBrush.eCut) - 0.625f)
                < 0.001f,
          "terrain-adjustment-metre-and-degree-units-preserve-defaults");

    float sectionMatrix[16];
    Mat4::identity(sectionMatrix);
    QVector<float> sectionPoints;
    TSection firstStraight(0, 0, 10.0f, 0.0f);
    firstStraight.getPoints(sectionPoints, sectionMatrix);
    TSection secondStraight(0, 0, 2.0f, 0.0f);
    secondStraight.getPoints(sectionPoints, sectionMatrix);
    check(sectionPoints.size() == 18
          && std::abs(sectionPoints[sectionPoints.size() - 3]) < 0.000001f
          && std::abs(sectionPoints[sectionPoints.size() - 2]) < 0.000001f
          && std::abs(sectionPoints[sectionPoints.size() - 1] - 12.0f)
                < 0.000001f
          && std::abs(sectionMatrix[14] - 12.0f) < 0.000001f,
          "tsection-points-chain-straights-at-exact-endpoints");

    Mat4::identity(sectionMatrix);
    sectionPoints.clear();
    TSection curve(0, 1, 0.1f, 100.0f);
    curve.getPoints(sectionPoints, sectionMatrix);
    float curveEndpoint[3] = {0.0f, 0.0f, 0.0f};
    Vec3::transformMat4(curveEndpoint, curveEndpoint, sectionMatrix);
    check(sectionPoints.size() == 12
          && std::abs(sectionPoints[sectionPoints.size() - 3]
                      - curveEndpoint[0]) < 0.000001f
          && std::abs(sectionPoints[sectionPoints.size() - 2]
                      - curveEndpoint[1]) < 0.000001f
          && std::abs(sectionPoints[sectionPoints.size() - 1]
                      - curveEndpoint[2]) < 0.000001f,
          "tsection-points-curve-ends-at-output-matrix");

    TerrainActionRaster actionRaster(-9.0f, -9.0f, 9.0f, 9.0f, 2);
    check(actionRaster.isValid()
          && actionRaster.minimumX() == -10
          && actionRaster.minimumZ() == -10
          && actionRaster.maximumX() == 10
          && actionRaster.maximumZ() == 10,
          "terrain-action-raster-aligned-bounds");
    float actionFactor = -1.0f;
    actionRaster.stampLegacyPoint(0.0f, 0.0f, 8.0f, 2.0f);
    check(actionRaster.sampleNearest(0.0f, 0.0f, actionFactor)
          && std::abs(actionFactor) < 0.000001f,
          "terrain-action-raster-flat-bed");
    check(actionRaster.sampleNearest(4.0f, 0.0f, actionFactor)
          && std::abs(actionFactor - 2.0f) < 0.000001f,
          "terrain-action-raster-native-distance");
    actionRaster.stampLegacyPoint(4.0f, 0.0f, 8.0f, 2.0f);
    check(actionRaster.sampleNearest(4.0f, 0.0f, actionFactor)
          && std::abs(actionFactor) < 0.000001f,
          "terrain-action-raster-overlap-selects-strongest");
    check(!actionRaster.sampleNearest(10.0f, 10.0f, actionFactor),
          "terrain-action-raster-uncovered-sample");
    TerrainActionRaster segmentRaster(-12.0f, -12.0f, 12.0f, 12.0f, 2);
    segmentRaster.stampSegment(-8.0f, -8.0f, 8.0f, 8.0f, 8.0f, 0.0f);
    const float expectedDiagonalDistance = std::sqrt(2.0f);
    check(segmentRaster.sampleNearest(0.0f, 2.0f, actionFactor)
          && std::abs(actionFactor - expectedDiagonalDistance) < 0.000001f,
          "terrain-action-raster-continuous-segment-distance");
    TerrainActionRaster bedRaster(-12.0f, -8.0f, 12.0f, 8.0f, 2);
    bedRaster.stampSegment(-8.0f, 0.0f, 8.0f, 0.0f, 8.0f, 2.0f);
    check(bedRaster.sampleNearest(0.0f, 2.0f, actionFactor)
          && std::abs(actionFactor) < 0.000001f
          && bedRaster.sampleNearest(0.0f, 4.0f, actionFactor)
          && std::abs(actionFactor - 2.0f) < 0.000001f,
          "terrain-action-raster-segment-flat-bed-and-slope");

    const TerrainGridLayout standard = TerrainGridLayout::profile(
                TerrainHeightProfile::Standard256x8);
    const TerrainGridLayout low = TerrainGridLayout::profile(
                TerrainHeightProfile::Low128x16);
    const TerrainGridLayout high = TerrainGridLayout::profile(
                TerrainHeightProfile::High512x4);
    const TerrainGridLayout ultra = TerrainGridLayout::profile(
                TerrainHeightProfile::Ultra1024x2);
    const TerrainGridLayout standardFourPatches = TerrainGridLayout::profile(
                TerrainHeightProfile::Standard256x8, 4);
    const TerrainGridLayout standardEightPatches = TerrainGridLayout::profile(
                TerrainHeightProfile::Standard256x8, 8);
    const TerrainGridLayout standardThirtyTwoPatches = TerrainGridLayout::profile(
                TerrainHeightProfile::Standard256x8, 32);
    check(standard.sampleCount == 256 && standard.sampleSpacing == 8
          && standard.patchResolution == 16
          && standard.terrainWorldSize == TerrainGridLayout::WorldTileSize,
          "profile-256-8");
    check(TerrainGridLayout::heightProfileName(
                  TerrainHeightProfile::Standard256x8)
                  == "Standard - 256 x 256 samples at 8 m"
          && TerrainGridLayout::profileName(
                  TerrainHeightProfile::High512x4, 32)
                  == "High resolution - 512 x 512 samples at 4 m; "
                     "32 x 32 patches; R=16",
          "profile-meaningful-names");
    check(low.sampleCount == 128 && low.sampleSpacing == 16
          && low.patchResolution == 8
          && low.terrainWorldSize == TerrainGridLayout::WorldTileSize,
          "profile-128-16");
    check(high.sampleCount == 512 && high.sampleSpacing == 4
          && high.patchResolution == 32
          && high.terrainWorldSize == TerrainGridLayout::WorldTileSize,
          "profile-512-4");
    check(ultra.sampleCount == 1024 && ultra.sampleSpacing == 2
          && ultra.patchResolution == 64
          && ultra.terrainWorldSize == TerrainGridLayout::WorldTileSize,
          "profile-1024-2");
    check(standardFourPatches.patchesPerSide == 4
          && standardFourPatches.patchResolution == 64,
          "profile-256-8-four-patches");
    check(standardEightPatches.patchesPerSide == 8
          && standardEightPatches.patchResolution == 32,
          "profile-256-8-eight-patches");
    check(standardThirtyTwoPatches.patchesPerSide == 32
          && standardThirtyTwoPatches.patchResolution == 8
          && standardThirtyTwoPatches.patchRecordCount() == 1024
          && standardThirtyTwoPatches.supportsEditing(),
          "profile-256-8-thirty-two-patches");
    check(standard.supportsEditing(), "sixteen-patch-grid-is-editable");
    TerrainGridLayout fourPatchLayout;
    QString fourPatchError;
    check(TerrainGridLayout::tryCreate(64, 32.0f, 4, 0.0f,
                                       fourPatchLayout, fourPatchError)
          && fourPatchLayout.supportsEditing(),
          "four-patch-grid-is-editable");
    check(fourPatchLayout.patchRecordCount() == 16
          && fourPatchLayout.isPatchIndexValid(15)
          && !fourPatchLayout.isPatchIndexValid(16),
          "four-patch-active-record-range");
    check(fourPatchLayout.patchRow(11) == 2
          && fourPatchLayout.patchColumn(11) == 3
          && fourPatchLayout.patchIndex(2, 3) == 11,
          "four-patch-index-round-trip");
    TerrainGridLayout eightPatchLayout;
    QString eightPatchError;
    check(TerrainGridLayout::tryCreate(256, 8.0f, 8, 0.0f,
                                       eightPatchLayout, eightPatchError)
          && eightPatchLayout.supportsEditing(),
          "eight-patch-grid-is-editable");
    check(eightPatchLayout.defaultPatchTextureScale() == 0.03125f,
          "texture-scale-follows-samples-per-patch");
    TerrainGridLayout futureLargePatchLayout;
    futureLargePatchLayout.patchesPerSide = 33;
    check(!futureLargePatchLayout.supportsEditing(),
          "above-maximum-patch-grid-is-read-only");
    check(!accepts(16, 128.0f), "reject-patch-resolution-below-four");
    check(accepts(64, 32.0f, 16), "minimum-patch-resolution-four");
    check(accepts(64, 32.0f), "lower-resolution-whole-world-tile");
    check(accepts(2048, 1.0f), "maximum-resolution");
    check(!accepts(2048, 1.0f, 4), "reject-patch-resolution-above-128");
    check(!accepts(2049, 1.0f), "reject-above-maximum");
    check(!accepts(15, 256.0f), "reject-below-minimum");
    check(!accepts(64, 8.0f), "reject-sub-world-tile-footprint");
    check(accepts(64, 32.0f, 4), "accept-four-patch-grid");
    check(accepts(256, 8.0f, 32), "accept-thirty-two-patch-grid");
    check(!accepts(2048, 1.0f, 33), "reject-above-maximum-patch-count");
    check(!accepts(256, 8.0f, 10), "reject-non-divisible-patch-count");
    check(!accepts(512, 4.5f), "reject-fractional-spacing");
    check(!accepts(512, 4.0f, 16, 1.0f), "reject-rotated-grid");
    auto generatedLodIndexBytes = [](const TerrainGridLayout &layout) {
        std::size_t count = 0;
        const QVector<int> steps = TerrainLod::availableSourceSteps(layout);
        for (int level = 0; level < steps.size(); ++level) {
            const int lastMask = level + 1 < steps.size() ? 15 : 0;
            for (int mask = 0; mask <= lastMask; ++mask)
                count += TerrainMeshPaged::buildLodIndices(
                            layout.patchResolution, steps[level],
                            static_cast<quint8>(mask)).size();
        }
        return count * sizeof(quint16);
    };
    check(standard.pagedVerticesPerPatch() == 17 * 17
          && standard.pagedIndicesPerPatch() == 16 * 16 * 6
          && standard.pagedPageCount() == 1
          && standardThirtyTwoPatches.pagedPageCount() == 4
          && standard.pagedPatchVertexBytes
                == static_cast<std::size_t>(17 * 17 * sizeof(TerrainVertex8Derived)),
          "paged-layout-sizes-and-pages");
    check(standard.pagedIndexBytes == generatedLodIndexBytes(standard)
          && ultra.pagedIndexBytes == generatedLodIndexBytes(ultra),
          "paged-layout-lod-index-memory-accounting");

    const QVector<quint16> twoCellIndices =
            TerrainMeshPaged::buildRegularIndices(2);
    check(twoCellIndices.size() == 24
          && twoCellIndices[0] == 0 && twoCellIndices[1] == 3
          && twoCellIndices[2] == 4 && twoCellIndices[3] == 0
          && twoCellIndices[4] == 4 && twoCellIndices[5] == 1
          && twoCellIndices[6] == 4 && twoCellIndices[7] == 5
          && twoCellIndices[8] == 2 && twoCellIndices[9] == 1
          && twoCellIndices[10] == 4 && twoCellIndices[11] == 2,
          "paged-checkerboard-index-topology");

    bool lodTopologyOk = true;
    const int topologyResolutions[] = {4, 8, 16, 32, 64, 128};
    for (int resolution : topologyResolutions) {
        for (int sourceStep = 1; sourceStep <= resolution;
             sourceStep *= 2) {
            if (resolution % sourceStep != 0)
                break;
            const int cells = resolution / sourceStep;
            const int lastMask = cells >= 2 && (cells % 2) == 0 ? 15 : 0;
            for (int mask = 0; mask <= lastMask; ++mask) {
                const QVector<quint16> indices =
                        TerrainMeshPaged::buildLodIndices(
                            resolution, sourceStep,
                            static_cast<quint8>(mask));
                int edgeCount = 0;
                for (int bit = 0; bit < 4; ++bit)
                    edgeCount += (mask >> bit) & 1;
                const int expectedTriangles = 2 * cells * cells
                        - edgeCount * (cells / 2);
                lodTopologyOk = lodTopologyOk
                        && indices.size() == expectedTriangles * 3;
                const int side = resolution + 1;
                int doubledArea = 0;
                std::set<std::array<int, 3>> uniqueTriangles;
                for (int index = 0; index + 2 < indices.size(); index += 3) {
                    const int first = indices[index];
                    const int second = indices[index + 1];
                    const int third = indices[index + 2];
                    lodTopologyOk = lodTopologyOk
                            && first >= 0 && first < side * side
                            && second >= 0 && second < side * side
                            && third >= 0 && third < side * side
                            && first != second && first != third
                            && second != third;
                    const int x0 = first % side;
                    const int z0 = first / side;
                    const int x1 = second % side;
                    const int z1 = second / side;
                    const int x2 = third % side;
                    const int z2 = third / side;
                    const int signedArea = (x1 - x0) * (z2 - z0)
                            - (z1 - z0) * (x2 - x0);
                    lodTopologyOk = lodTopologyOk && signedArea < 0;
                    doubledArea -= signedArea;
                    std::array<int, 3> triangleKey = {first, second, third};
                    std::sort(triangleKey.begin(), triangleKey.end());
                    lodTopologyOk = lodTopologyOk
                            && uniqueTriangles.insert(triangleKey).second;
                    const int vertices[] = {first, second, third};
                    for (int vertex : vertices) {
                        const int x = vertex % side;
                        const int z = vertex / side;
                        const int levelX = x / sourceStep;
                        const int levelZ = z / sourceStep;
                        lodTopologyOk = lodTopologyOk
                                && x % sourceStep == 0
                                && z % sourceStep == 0
                                && !((mask & TerrainLod::LocalX0)
                                     && x == 0 && (levelZ & 1))
                                && !((mask & TerrainLod::LocalXMax)
                                     && x == resolution && (levelZ & 1))
                                && !((mask & TerrainLod::LocalZ0)
                                     && z == 0 && (levelX & 1))
                                && !((mask & TerrainLod::LocalZMax)
                                     && z == resolution && (levelX & 1));
                    }
                }
                lodTopologyOk = lodTopologyOk
                        && doubledArea == 2 * resolution * resolution;
            }
        }
    }
    check(lodTopologyOk, "paged-lod-all-transition-topologies");

    const QVector<TerrainLodLevel> lodProfile = {
        {4, 1000}, {8, 2000}, {16, 4000}
    };
    QString lodProfileError;
    check(TerrainLod::validateProfile(lodProfile, &lodProfileError)
          && TerrainLod::requestedSampleSpacing(lodProfile, 500.0f * 500.0f) == 4
          && TerrainLod::requestedSampleSpacing(lodProfile, 1500.0f * 1500.0f) == 8
          && TerrainLod::requestedSampleSpacing(lodProfile, 5000.0f * 5000.0f) == 16
          && !TerrainLod::validateProfile({{4, 1000}, {16, 2000}},
                                          &lodProfileError)
          && !TerrainLod::validateProfile({{4, 1000}, {8, 900}},
                                          &lodProfileError),
          "terrain-lod-profile-validation-and-last-range");

    const QVector<int> ultraSteps = TerrainLod::availableSourceSteps(ultra);
    check(ultraSteps == QVector<int>({1, 2, 4, 8, 16})
          && TerrainLod::sourceStepForRequest(ultra, 1) == 1
          && TerrainLod::sourceStepForRequest(ultra, 8) == 4
          && TerrainLod::sourceStepForRequest(ultra, 32) == 16
          && TerrainLod::sourceStepForRequest(standard, 4) == 1,
          "terrain-lod-available-level-selection");

    QVector<quint8> noGaps(ultra.patchRecordCount(), 0);
    QVector<TerrainPatchLodState> tileLod = TerrainLod::buildTileState(
                ultra, {{2, 200}, {4, 500}, {8, 1000}, {16, 2000}},
                1024.0f, 1024.0f, noGaps);
    bool tileLodOk = tileLod.size() == ultra.patchRecordCount();
    for (int patchId = 0; patchId < tileLod.size(); ++patchId) {
        const int row = ultra.patchRow(patchId);
        const int column = ultra.patchColumn(patchId);
        if (row == 0 || column == 0
                || row == ultra.patchesPerSide - 1
                || column == ultra.patchesPerSide - 1)
            tileLodOk = tileLodOk && tileLod[patchId].sourceStep == 1;
        const int neighbours[2][2] = {{row, column + 1}, {row + 1, column}};
        for (const auto &neighbour : neighbours) {
            if (neighbour[0] >= ultra.patchesPerSide
                    || neighbour[1] >= ultra.patchesPerSide)
                continue;
            const int neighbourId = ultra.patchIndex(neighbour[0], neighbour[1]);
            const int lower = std::min(tileLod[patchId].sourceStep,
                                       tileLod[neighbourId].sourceStep);
            const int higher = std::max(tileLod[patchId].sourceStep,
                                        tileLod[neighbourId].sourceStep);
            tileLodOk = tileLodOk && higher <= lower * 2;
        }
    }
    QVector<quint8> oneGap = noGaps;
    const int gapPatch = ultra.patchIndex(8, 8);
    oneGap[gapPatch] = 1;
    const QVector<TerrainPatchLodState> gapLod = TerrainLod::buildTileState(
                ultra, {{2, 1}, {4, 2}, {8, 3}, {16, 4}, {32, 5}},
                0.0f, 0.0f, oneGap);
    tileLodOk = tileLodOk
            && gapLod[gapPatch].sourceStep == 1
            && gapLod[ultra.patchIndex(7, 8)].sourceStep == 1
            && gapLod[ultra.patchIndex(9, 8)].sourceStep == 1
            && gapLod[ultra.patchIndex(8, 7)].sourceStep == 1
            && gapLod[ultra.patchIndex(8, 9)].sourceStep == 1;
    bool profileViolation = false;
    TerrainLod::buildTileState(
                ultra, {{2, 1}, {4, 2}, {8, 3}, {16, 4}, {32, 5}},
                8.5f * ultra.patchWorldSize,
                8.5f * ultra.patchWorldSize,
                noGaps, &profileViolation);
    tileLodOk = tileLodOk && profileViolation;
    check(tileLodOk, "terrain-lod-tile-constraints-and-gap-pinning");

    Trk serializedTrk;
    serializedTrk.terrainLodLevels = lodProfile;
    QString serializedTrkText;
    QTextStream serializedTrkStream(&serializedTrkText);
    serializedTrk.saveToStream(serializedTrkStream);
    check(serializedTrkText.contains("TsreTerrainLod (\n")
          && serializedTrkText.contains("Level ( 4 1000 )")
          && serializedTrkText.contains("Level ( 16 4000 )")
          && serializedTrk.terrainLodSummary().contains("and beyond"),
          "terrain-lod-trk-serialization");

    auto parseTrkText = [](const QString &text) {
        const int byteCount = text.size() * int(sizeof(char16_t));
        unsigned char *bytes = new unsigned char[byteCount];
        std::memcpy(bytes, text.utf16(), static_cast<std::size_t>(byteCount));
        FileBuffer data(bytes, byteCount);
        Trk parsed;
        parsed.loadUtf16Data(&data);
        return parsed.terrainLodLevels;
    };
    const QVector<TerrainLodLevel> parsedLod = parseTrkText(
                "Tr_RouteFile ( TsreTerrainLod ( Level ( 4 1000 ) "
                "Level ( 8 2000 ) Level ( 16 4000 ) ) )");
    const QVector<TerrainLodLevel> malformedLod = parseTrkText(
                "Tr_RouteFile ( TsreTerrainLod ( Level ( 4 1000 ) "
                "Level ( 16 2000 ) ) )");
    check(parsedLod == lodProfile && malformedLod.isEmpty(),
          "terrain-lod-trk-parse-and-malformed-fallback");
    check(TerrainMeshPaged::packNormal(0.0f, 1.0f, 0.0f)
                == (511u << 10)
          && TerrainMeshPaged::packNormal(0.0f, 1.0f, 0.0f, true)
                == ((511u << 10) | (1u << 30))
          && sizeof(TerrainVertex8Derived) == 8
          && sizeof(TerrainPatchGpuParams) == 32,
          "paged-packed-normal-and-record-layout");

    int tileX = 10;
    int tileZ = 20;
    float localX = 1024.0f;
    float localZ = -1024.0f;
    check(TerrainGridLayout::normalizeWorldPosition(
              tileX, tileZ, localX, localZ)
          && tileX == 11 && tileZ == 20
          && localX == -1024.0f && localZ == -1024.0f,
          "positive-edge-selects-outside-world-cell");
    tileX = 10;
    tileZ = 20;
    localX = 3072.0f;
    localZ = -3072.0f;
    check(TerrainGridLayout::normalizeWorldPosition(
              tileX, tileZ, localX, localZ)
          && tileX == 12 && tileZ == 19
          && localX == -1024.0f && localZ == -1024.0f,
          "multi-world-tile-normalization");
    localX = std::numeric_limits<float>::infinity();
    check(!TerrainGridLayout::normalizeWorldPosition(
              tileX, tileZ, localX, localZ),
          "reject-non-finite-world-position");

    auto selectionWindowRoundTrips = [](const TerrainPatchSelectionWindow &window) {
        int mapped = 0;
        for (int patchId = 0;
             patchId < window.patchesPerSide * window.patchesPerSide;
             ++patchId) {
            const int selectionIndex = window.selectionIdForPatch(patchId);
            if (selectionIndex < 0)
                continue;
            ++mapped;
            if (window.patchIdForSelection(selectionIndex) != patchId)
                return false;
        }
        return mapped == 256;
    };
    const TerrainPatchSelectionWindow topLeft =
            TerrainPatchSelectionWindow::forCameraPatch(32, 0, 0);
    const TerrainPatchSelectionWindow topRight =
            TerrainPatchSelectionWindow::forCameraPatch(32, 0, 31);
    const TerrainPatchSelectionWindow bottomLeft =
            TerrainPatchSelectionWindow::forCameraPatch(32, 31, 0);
    const TerrainPatchSelectionWindow bottomRight =
            TerrainPatchSelectionWindow::forCameraPatch(32, 31, 31);
    const TerrainPatchSelectionWindow centered =
            TerrainPatchSelectionWindow::forCameraPatch(32, 16, 16);
    const TerrainPatchSelectionWindow fourPatches =
            TerrainPatchSelectionWindow::forCameraPatch(4, 3, 3);
    check(topLeft.row == 0 && topLeft.column == 0
          && topRight.row == 0 && topRight.column == 16
          && bottomLeft.row == 16 && bottomLeft.column == 0
          && bottomRight.row == 16 && bottomRight.column == 16,
          "selection-window-corners-clamp");
    check(TerrainPatchSelectionWindow::forCameraPatch(32, 0, 15).row == 0
          && TerrainPatchSelectionWindow::forCameraPatch(32, 31, 15).row == 16
          && TerrainPatchSelectionWindow::forCameraPatch(32, 15, 0).column == 0
          && TerrainPatchSelectionWindow::forCameraPatch(32, 15, 31).column == 16,
          "selection-window-edges-clamp");
    check(centered.row == 8 && centered.column == 8
          && centered.selectionIdForPatch(16 * 32 + 16) == 8 * 16 + 8
          && centered.selectionIdForPatch(0) == -1
          && centered.patchIdForSelection(0) == 8 * 32 + 8
          && fourPatches.selectionIdForPatch(15) == 3 * 16 + 3
          && fourPatches.patchIdForSelection(3 * 16 + 3) == 15,
          "selection-window-centers-and-excludes-outside-patches");
    check(selectionWindowRoundTrips(topLeft)
          && selectionWindowRoundTrips(topRight)
          && selectionWindowRoundTrips(bottomLeft)
          && selectionWindowRoundTrips(bottomRight)
          && selectionWindowRoundTrips(centered),
          "selection-window-forward-reverse-round-trips");

    QTemporaryDir overwriteDirectory;
    const QString originalRoot = Game::root;
    const QString originalRoute = Game::route;
    const bool originalWriteEnabled = Game::writeEnabled;
    bool overwriteDescriptorsOk = overwriteDirectory.isValid();
    if (overwriteDescriptorsOk) {
        Game::root = overwriteDirectory.path();
        Game::route = "terrain-overwrite-test";
        const QString tilesPath = overwriteDirectory.path()
                + "/routes/" + Game::route + "/tiles";
        overwriteDescriptorsOk = QDir().mkpath(tilesPath);
        Game::writeEnabled = false;
        overwriteDescriptorsOk = overwriteDescriptorsOk
                && !Terrain::SaveEmpty("writeDisabled", 256, 8, 32)
                && !QFile::exists(tilesPath + "/writeDisabled.t")
                && !QFile::exists(tilesPath + "/writeDisabled_y.raw");
        Game::writeEnabled = true;
        struct OverwriteCase {
            const char *name;
            int samples;
            int spacing;
        };
        const OverwriteCase overwriteCases[] = {
            {"overwrite256", 256, 8},
            {"overwrite512", 512, 4}
        };
        for (const OverwriteCase &overwriteCase : overwriteCases) {
            const QString name = overwriteCase.name;
            QFile staleE(tilesPath + "/" + name + "_e.raw");
            QFile staleN(tilesPath + "/" + name + "_n.raw");
            overwriteDescriptorsOk = overwriteDescriptorsOk
                    && staleE.open(QIODevice::WriteOnly)
                    && staleE.write("stale") == 5;
            staleE.close();
            overwriteDescriptorsOk = overwriteDescriptorsOk
                    && staleN.open(QIODevice::WriteOnly)
                    && staleN.write("stale") == 5;
            staleN.close();
            overwriteDescriptorsOk = overwriteDescriptorsOk
                    && Terrain::SaveEmpty(name, overwriteCase.samples,
                                          overwriteCase.spacing, 16,
                                          false, true);
            TFile descriptor;
            overwriteDescriptorsOk = overwriteDescriptorsOk
                    && descriptor.readT(tilesPath + "/" + name + ".t")
                    && descriptor.sampleEbuffer != NULL
                    && *descriptor.sampleEbuffer == name + "_e.raw"
                    && descriptor.sampleNbuffer != NULL
                    && *descriptor.sampleNbuffer == name + "_n.raw"
                    && !QFile::exists(tilesPath + "/" + name + "_e.raw")
                    && !QFile::exists(tilesPath + "/" + name + "_n.raw");
        }

        const QString roundTripName = "patches32";
        overwriteDescriptorsOk = overwriteDescriptorsOk
                && Terrain::SaveEmpty(roundTripName, 512, 4, 32);
        TFile createdDescriptor;
        overwriteDescriptorsOk = overwriteDescriptorsOk
                && createdDescriptor.readT(
                        tilesPath + "/" + roundTripName + ".t")
                && createdDescriptor.patchsetNpatches == 32
                && createdDescriptor.flags != NULL
                && createdDescriptor.errorBias != NULL
                && createdDescriptor.tdata != NULL
                && std::abs(createdDescriptor.patchValue(
                        0, TFile::PatchField::FactorY) - 49.74062729f)
                        < 0.00001f
                && std::abs(createdDescriptor.patchValue(
                        1023, TFile::PatchField::FactorY)
                    - 49.74062729f) < 0.00001f
                && std::abs(createdDescriptor.patchValue(
                        1023, TFile::PatchField::TextureW) - 0.0625f)
                        < 0.000001f;
        const std::array<TFile::PatchField, TFile::PatchFieldCount> fields = {
            TFile::PatchField::CenterX,
            TFile::PatchField::AverageY,
            TFile::PatchField::CenterZ,
            TFile::PatchField::FactorY,
            TFile::PatchField::RangeY,
            TFile::PatchField::RadiusM,
            TFile::PatchField::ShaderIndex,
            TFile::PatchField::TextureX,
            TFile::PatchField::TextureY,
            TFile::PatchField::TextureW,
            TFile::PatchField::TextureB,
            TFile::PatchField::TextureC,
            TFile::PatchField::TextureH
        };
        for (int i = 0; i < static_cast<int>(fields.size()); ++i) {
            const float value = fields[i] == TFile::PatchField::ShaderIndex
                    ? 0.0f : 10.25f + static_cast<float>(i);
            createdDescriptor.setPatchValue(17, fields[i], value);
        }
        createdDescriptor.flags[17] = 0x010000c3;
        createdDescriptor.errorBias[17] = 4.75f;
        const QString fieldRoundTripPath = tilesPath + "/fields-round-trip.t";
        createdDescriptor.save(fieldRoundTripPath);
        TFile fieldRoundTripDescriptor;
        overwriteDescriptorsOk = overwriteDescriptorsOk
                && fieldRoundTripDescriptor.readT(fieldRoundTripPath)
                && fieldRoundTripDescriptor.flags[17] == 0x010000c3
                && std::abs(fieldRoundTripDescriptor.errorBias[17] - 4.75f)
                    < 0.000001f;
        for (int i = 0; i < static_cast<int>(fields.size()); ++i) {
            const float expected = fields[i] == TFile::PatchField::ShaderIndex
                    ? 0.0f : 10.25f + static_cast<float>(i);
            overwriteDescriptorsOk = overwriteDescriptorsOk
                    && std::abs(fieldRoundTripDescriptor.patchValue(17, fields[i])
                                - expected) < 0.000001f;
        }
        TerrainInfo terrainInfo;
        terrainInfo.cx = 0;
        terrainInfo.cy = 0;
        terrainInfo.name = roundTripName;
        Terrain terrain(&terrainInfo);
        overwriteDescriptorsOk = overwriteDescriptorsOk
                && terrain.loaded && terrain.isEditable()
                && terrain.getGridLayout().patchRecordCount() == 1024;
        if (terrain.loaded && terrain.isEditable()) {
            const int heightSide = terrain.getSampleCount() + 1;
            std::vector<float> heights(
                        static_cast<std::size_t>(heightSide) * heightSide,
                        10.0f);
            heights[0] = 30.0f;
            terrain.fillHeightMap(heights.data());
            terrain.refreshModified();
            terrain.setErrorBias(0, 0, 1023.0f, 1023.0f, 3.5f);
            terrain.setPatchFlags(0, 0, 1023.0f, 1023.0f, 7);
            terrain.save();
        }
        TFile savedDescriptor;
        overwriteDescriptorsOk = overwriteDescriptorsOk
                && savedDescriptor.readT(
                        tilesPath + "/" + roundTripName + ".t")
                && savedDescriptor.patchsetNpatches == 32
                && savedDescriptor.errorBias != NULL
                && std::abs(savedDescriptor.errorBias[1023] - 3.5f) < 0.000001f
                && savedDescriptor.flags != NULL
                && savedDescriptor.flags[1023] == 7
                && std::abs(savedDescriptor.patchValue(
                        0, TFile::PatchField::CenterX) - 32.0f) < 0.000001f
                && std::abs(savedDescriptor.patchValue(
                        0, TFile::PatchField::CenterZ) + 32.0f) < 0.000001f
                && std::abs(savedDescriptor.patchValue(
                        0, TFile::PatchField::AverageY) - 20.0f) < 0.000001f
                && std::abs(savedDescriptor.patchValue(
                        0, TFile::PatchField::RangeY) - 10.0f) < 0.000001f
                && std::abs(savedDescriptor.patchValue(
                        0, TFile::PatchField::RadiusM) - 32.0f) < 0.000001f
                && std::abs(savedDescriptor.patchValue(
                        0, TFile::PatchField::FactorY)
                    - std::sqrt(49.74062729f * 49.74062729f + 100.0f))
                        < 0.00001f;
    }
    Game::root = originalRoot;
    Game::route = originalRoute;
    Game::writeEnabled = originalWriteEnabled;
    check(overwriteDescriptorsOk,
          "write-guard-overwrite-and-thirty-two-patch-round-trip");

    qInfo() << "[tests:terrain-grid] cases=" << (passed + failed)
            << "passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}

struct TerrainCorpusRouteResult {
    int files = 0;
    int descriptorsAccepted = 0;
    int loaded = 0;
    int editable = 0;
    int readOnly = 0;
    int variablePatchEditChecks = 0;
    int variablePatchEditFailures = 0;
    int descriptorRejected = 0;
    int payloadFailed = 0;
};

static int runTerrainFilesSuite(const TsreTests::TestRunOptions &opts) {
    const QString scanRoot = QDir::cleanPath(opts.casesFile);
    if (scanRoot.isEmpty() || !QDir(scanRoot).exists()) {
        qWarning() << "[tests:terrain-files] --test-cases must name an existing directory:"
                   << opts.casesFile;
        return 2;
    }

    QStringList descriptors;
    QDirIterator iterator(scanRoot, QStringList() << "*.t", QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = QDir::cleanPath(iterator.next());
        const QString directoryName = QFileInfo(path).dir().dirName();
        if (directoryName.compare("tiles", Qt::CaseInsensitive) == 0
                || directoryName.compare("lo_tiles", Qt::CaseInsensitive) == 0)
            descriptors.append(path);
    }
    descriptors.sort(Qt::CaseInsensitive);

    if (descriptors.isEmpty()) {
        qWarning() << "[tests:terrain-files] no terrain descriptors found below"
                   << scanRoot;
        return 2;
    }

    const QString originalRoot = Game::root;
    const QString originalRoute = Game::route;
    QMap<QString, TerrainCorpusRouteResult> routeResults;
    QStringList rejectedDescriptors;

    for (int index = 0; index < descriptors.size(); ++index) {
        const QString descriptorPath = descriptors[index];
        const QFileInfo descriptorInfo(descriptorPath);
        const QDir tileDirectory = descriptorInfo.dir();
        QDir routeDirectory(tileDirectory);
        routeDirectory.cdUp();
        QDir routesDirectory(routeDirectory);
        routesDirectory.cdUp();
        QDir simulatorRoot(routesDirectory);
        simulatorRoot.cdUp();

        Game::root = simulatorRoot.absolutePath();
        Game::route = routeDirectory.dirName();

        TerrainInfo terrainInfo;
        terrainInfo.name = descriptorInfo.completeBaseName();
        terrainInfo.low = tileDirectory.dirName().compare(
                    "lo_tiles", Qt::CaseInsensitive) == 0;

        if (opts.verbose)
            qInfo() << "[tests:terrain-files] LOAD"
                    << (index + 1) << "/" << descriptors.size()
                    << descriptorPath;

        Terrain terrain(&terrainInfo);
        TerrainCorpusRouteResult &result = routeResults[Game::route];
        ++result.files;
        if (terrain.getGridLayout().sampleCount > 0) {
            ++result.descriptorsAccepted;
            if (terrain.isEditable())
                ++result.editable;
            else
                ++result.readOnly;
            if (terrain.loaded) {
                ++result.loaded;
                const TerrainGridLayout &layout = terrain.getGridLayout();
                if (opts.verbose) {
                    qInfo() << "[tests:terrain-files] GRID"
                            << descriptorPath
                            << "samples" << layout.sampleCount
                            << "spacing" << layout.sampleSpacing
                            << "patches" << layout.patchesPerSide
                            << "patch_resolution" << layout.patchResolution
                            << "first_transform"
                            << terrain.getPatchTexTransformString(0)
                            << "last_transform"
                            << terrain.getPatchTexTransformString(
                                   layout.patchRecordCount() - 1);
                }
                if (layout.patchesPerSide
                        != TerrainGridLayout::DefaultPatchesPerSide) {
                    ++result.variablePatchEditChecks;
                    const int lastPatch = layout.patchRecordCount() - 1;
                    const QString originalTransform =
                            terrain.getPatchTexTransformString(lastPatch);
                    const QString probeTransform =
                            "0.125 0.25 0.03125 0 0 0.03125";
                    terrain.setPatchTexTransform(probeTransform, lastPatch);
                    const bool editOk = !originalTransform.isEmpty()
                            && terrain.getPatchTexTransformString(lastPatch)
                            == probeTransform;
                    terrain.setPatchTexTransform(originalTransform, lastPatch);
                    if (!editOk) {
                        ++result.variablePatchEditFailures;
                        qWarning() << "[tests:terrain-files] VARIABLE_PATCH_EDIT_FAILURE"
                                   << descriptorPath
                                   << "patches" << layout.patchesPerSide;
                    }
                }
            } else {
                ++result.payloadFailed;
                qWarning() << "[tests:terrain-files] PAYLOAD_FAILURE"
                           << descriptorPath;
            }
        } else {
            ++result.descriptorRejected;
            rejectedDescriptors.append(descriptorPath);
            qWarning() << "[tests:terrain-files] REJECT" << descriptorPath;
        }
    }

    Game::root = originalRoot;
    Game::route = originalRoute;

    int loaded = 0;
    int descriptorsAccepted = 0;
    int descriptorRejected = 0;
    int payloadFailed = 0;
    int editable = 0;
    int readOnly = 0;
    int variablePatchEditChecks = 0;
    int variablePatchEditFailures = 0;
    for (auto it = routeResults.cbegin(); it != routeResults.cend(); ++it) {
        descriptorsAccepted += it->descriptorsAccepted;
        loaded += it->loaded;
        editable += it->editable;
        readOnly += it->readOnly;
        variablePatchEditChecks += it->variablePatchEditChecks;
        variablePatchEditFailures += it->variablePatchEditFailures;
        descriptorRejected += it->descriptorRejected;
        payloadFailed += it->payloadFailed;
        qInfo() << "[tests:terrain-files] ROUTE" << it.key()
                << "files=" << it->files
                << "descriptors_accepted=" << it->descriptorsAccepted
                << "loaded=" << it->loaded
                << "editable=" << it->editable
                << "read_only=" << it->readOnly
                << "variable_patch_edit_checks=" << it->variablePatchEditChecks
                << "variable_patch_edit_failures=" << it->variablePatchEditFailures
                << "descriptor_rejected=" << it->descriptorRejected
                << "payload_failed=" << it->payloadFailed;
    }
    qInfo() << "[tests:terrain-files] root=" << scanRoot
            << "files=" << descriptors.size()
            << "descriptors_accepted=" << descriptorsAccepted
            << "loaded=" << loaded
            << "editable=" << editable
            << "read_only=" << readOnly
            << "variable_patch_edit_checks=" << variablePatchEditChecks
            << "variable_patch_edit_failures=" << variablePatchEditFailures
            << "descriptor_rejected=" << descriptorRejected
            << "payload_failed=" << payloadFailed;
    return rejectedDescriptors.isEmpty() && variablePatchEditFailures == 0
            ? 0 : 1;
}

} // namespace

QStringList TsreTests::listSuites() {
    return {
        "dyntrack-road",
        "flex",
        "flex-point",
        "orts-profile",
        "procedural-policy",
        "route-load",
        "settings",
        "tdb-load",
        "terrain-files",
        "terrain-grid",
        "terrain-raw-benchmark"
    };
}

int TsreTests::run(const TestRunOptions &opts) {
    const QString suite = suiteNameNormalized(opts.suite);

    Game::gui = false;

    if (suite.isEmpty() || suite == "flex-point")
        return runFlexPointSuite(opts.verbose);

    if (suite == "flex")
        return runFlexSuite(opts.casesFile, opts.verbose);

    if (suite == "route-load")
        return runRouteLoadSuite(opts);

    if (suite == "tdb-load")
        return runTdbLoadSuite(opts.verbose);

    if (suite == "procedural-policy")
        return runProceduralPolicySuite(opts.verbose);

    if (suite == "dyntrack-road")
        return runDynTrackRoadSuite(opts.verbose);

    if (suite == "orts-profile")
        return runOrtsProfileSuite(opts.verbose);

    if (suite == "settings")
        return runSettingsSuite(opts.verbose);

    if (suite == "terrain-grid")
        return runTerrainGridSuite(opts.verbose);

    if (suite == "terrain-files")
        return runTerrainFilesSuite(opts);

    if (suite == "terrain-raw-benchmark")
        return runTerrainRawBenchmark(opts);

    if (suite == "all") {
        int rc = 0;
        rc = std::max(rc, runFlexSuite(opts.casesFile, opts.verbose));
        rc = std::max(rc, runFlexPointSuite(opts.verbose));
        rc = std::max(rc, runProceduralPolicySuite(opts.verbose));
        rc = std::max(rc, runDynTrackRoadSuite(opts.verbose));
        rc = std::max(rc, runOrtsProfileSuite(opts.verbose));
        rc = std::max(rc, runRouteLoadSuite(opts));
        rc = std::max(rc, runSettingsSuite(opts.verbose));
        rc = std::max(rc, runTdbLoadSuite(opts.verbose));
        rc = std::max(rc, runTerrainGridSuite(opts.verbose));
        return rc;
    }

    qWarning() << "[tests] unknown suite:" << opts.suite;
    qWarning() << "[tests] available suites:" << TsreTests::listSuites();
    return 2;
}
