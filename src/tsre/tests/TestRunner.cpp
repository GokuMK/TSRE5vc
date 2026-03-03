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

#include <tsre/Game.h>
#include <tsre/math3d/Flex.h>

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
    float preferredMinCurveRadius = 200.0f;
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

    out.preferredMinCurveRadius = (float)obj.value("preferredMinCurveRadius").toDouble(200.0);
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

} // namespace

QStringList TsreTests::listSuites() {
    return {"flex"};
}

int TsreTests::run(const TestRunOptions &opts) {
    const QString suite = suiteNameNormalized(opts.suite);

    Game::gui = false;

    if (suite.isEmpty() || suite == "flex")
        return runFlexSuite(opts.casesFile, opts.verbose);

    if (suite == "all") {
        int rc = 0;
        rc = std::max(rc, runFlexSuite(opts.casesFile, opts.verbose));
        return rc;
    }

    qWarning() << "[tests] unknown suite:" << opts.suite;
    qWarning() << "[tests] available suites:" << TsreTests::listSuites();
    return 2;
}
