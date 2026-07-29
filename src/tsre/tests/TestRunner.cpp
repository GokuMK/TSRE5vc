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
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>

#include <tsre/Game.h>
#include <tsre/math3d/Flex.h>
#include <tsre/tests/RouteLoadTestSuite.h>

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
    if (casesFile.trimmed().isEmpty())
        casesFile = "features/tests/cases/flex.jsonl";

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
        const bool ok = Flex::NewFlex(c.x1, c.z1, c.p1, c.q1, c.x2, c.z2, c.p2, c.q2, got, c.preferredMinCurveRadius);
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
            const bool hasCurve = (std::fabs(got[2]) > 0.01f && got[3] > 0.1f)
                    || (std::fabs(got[6]) > 0.01f && got[7] > 0.1f);
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
        float objectYaw;
        float rightOffset;
        int expectedTileX;
        int expectedTileZ;
        float expectedPosition[3];
    };
    const OffsetPoseCase offsetCases[] = {
        {"offset-identity", 5, -2, {10, 3, -20}, 0, 4,
            5, -2, {14, 3, -20}},
        {"offset-rotated", 0, 0, {0, 7, 0}, (float)M_PI / 2.0f, 4,
            0, 0, {0, 7, -4}},
        {"offset-positive-tile-crossing", 0, 0, {1023, 0, 0}, 0, 4,
            1, 0, {-1021, 0, 0}},
        {"offset-negative-tile-crossing", 0, 0, {-1023, 0, 0}, 0, -4,
            -1, 0, {1021, 0, 0}},
    };
    for(const OffsetPoseCase &c : offsetCases) {
        float sourceQ[4] = {
            0.0f,
            std::sin(c.objectYaw * 0.5f),
            0.0f,
            std::cos(c.objectYaw * 0.5f)
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
            + guardCaseCount;
    qInfo() << "[tests:flex-point] cases=" << caseCount
            << "passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}

} // namespace

QStringList TsreTests::listSuites() {
    return {"flex", "flex-point", "route-load"};
}

int TsreTests::run(const TestRunOptions &opts) {
    const QString suite = suiteNameNormalized(opts.suite);

    Game::gui = false;

    if (suite.isEmpty() || suite == "flex")
        return runFlexSuite(opts.casesFile, opts.verbose);

    if (suite == "flex-point")
        return runFlexPointSuite(opts.verbose);

    if (suite == "route-load")
        return runRouteLoadSuite(opts);

    if (suite == "all") {
        int rc = 0;
        rc = std::max(rc, runFlexSuite(opts.casesFile, opts.verbose));
        rc = std::max(rc, runFlexPointSuite(opts.verbose));
        rc = std::max(rc, runRouteLoadSuite(opts));
        return rc;
    }

    qWarning() << "[tests] unknown suite:" << opts.suite;
    qWarning() << "[tests] available suites:" << TsreTests::listSuites();
    return 2;
}
