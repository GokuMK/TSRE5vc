#include "ShapeAnimationTestSuite.h"
#include "TokenTestSupport.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/shape/SFileComplex.h>
#include <tsre/shape/SFileLegacy.h>
#include <tsre/shape/ShapeAnimation.h>

namespace {

bool near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) < tolerance; }

bool same(const float *a, const float *b, float tolerance = 1e-3f) {
    for (int i = 0; i < 16; ++i)
        if (!near(a[i], b[i], tolerance))
            return false;
    return true;
}

void identity(float *m) {
    for (int i = 0; i < 16; ++i)
        m[i] = i % 5 == 0 ? 1.0f : 0.0f;
}

// A turn about x by `angle`, as the evaluator writes it.
void aboutX(float angle, float *q) {
    q[0] = std::sin(angle / 2);
    q[1] = q[2] = 0.0f;
    q[3] = std::cos(angle / 2);
}

QStringList stockShapes() {
    QString root = qEnvironmentVariable("TSRE_ANIMATION_SHAPES");
    if (root.isEmpty())
        root = Game::root + "/TRAINS/TRAINSET";
    QStringList shapes;
    if (!QDir(root).exists())
        return shapes;
    QDirIterator it(root, {"*.s", "*.S"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        shapes << it.next();
    shapes.sort();
    return shapes;
}

}

int TsreTests::runShapeAnimationSuite(bool verbose) {
    TokenTest::Suite test{"[tests:shape-animation]", verbose};

    // The evaluator: Open Rails' rules.
    ShapeAnimation::Channel channel;
    ShapeAnimation::PositionKey p0, p1;
    p0.frame = 0;
    p1.frame = 10;
    p1.position[0] = 2.0f;
    channel.positions = {p0, p1};
    float rest[16];
    identity(rest);
    rest[13] = 5.0f;
    float out[16];
    ShapeAnimation::pose(channel, 5.0f, rest, out);
    test.check(near(out[12], 1.0f) && near(out[13], 0.0f), "a position between two keys replaces the translation");
    ShapeAnimation::pose(channel, -3.0f, rest, out);
    const bool before = near(out[12], 0.0f);
    ShapeAnimation::pose(channel, 25.0f, rest, out);
    test.check(before && near(out[12], 2.0f), "before the first key the first, after the last the last");
    test.check(near(channel.lastFrame(), 10.0f), "the last key frame");

    ShapeAnimation::Channel turn;
    ShapeAnimation::RotationKey r0, r1;
    r0.frame = 0;
    r1.frame = 4;
    aboutX(float(M_PI) / 2, r1.rotation);
    turn.rotations = {r0, r1};
    float rested[16];
    identity(rested);
    // At rest turned about y and moved: both replaced or kept as the keys say.
    rested[0] = rested[10] = 0.0f;
    rested[2] = -1.0f;
    rested[8] = 1.0f;
    rested[12] = 3.0f;
    ShapeAnimation::pose(turn, 2.0f, rested, out);
    test.check(near(out[0], 1.0f) && near(out[5], std::cos(float(M_PI) / 4)) && near(out[6], std::sin(float(M_PI) / 4))
                   && near(out[12], 3.0f),
               "a rotation slerped halfway replaces the rest rotation, the translation kept");
    ShapeAnimation::Channel single;
    single.rotations = {r1};
    ShapeAnimation::pose(single, 0.0f, rested, out);
    test.check(near(out[0], 1.0f) && near(out[5], 0.0f) && near(out[6], 1.0f), "a single key holds at every frame");
    ShapeAnimation::pose(ShapeAnimation::Channel(), 3.0f, rested, out);
    test.check(same(out, rested), "no keys: the rest matrix");
    const float msts[4] = {0.1f, 0.2f, 0.3f, 0.9f};
    float tsre[4];
    ShapeAnimation::fromMsts(msts, tsre);
    test.check(near(tsre[0], -0.1f) && near(tsre[1], -0.2f) && near(tsre[2], -0.3f) && near(tsre[3], 0.9f),
               "an MSTS key in TSRE's space: its conjugate");

    ShapeAnimation::Pose pose;
    pose.setFrame(3, 2.0f);
    const bool framed = pose.posed && pose.parts.size() == 4;
    const float transform[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 7, 8, 9, 1};
    pose.setTransform(1, transform);
    pose.setTransform(3, nullptr);
    pose.local(1, &turn, false, 0.0f, rested, out);
    const bool own = near(out[12], 7.0f);
    pose.local(2, &turn, true, 4.0f, rested, out);
    const bool looping = near(out[6], 1.0f);
    pose.local(2, &turn, false, 4.0f, rested, out);
    const bool resting = same(out, rested);
    pose.setTransform(1, nullptr);
    test.check(framed && own && looping && resting && !pose.posed,
               "a pose: its own transform, a looping part, a resting part; no longer posed when all rest");

    const QStringList shapes = stockShapes();
    if (shapes.isEmpty()) {
        qInfo() << "[tests:shape-animation] SKIP stock shapes: no TRAINS/TRAINSET (TSRE_ANIMATION_SHAPES)";
        return test.finish();
    }
    int keyed = 0, restAtZero = 0, animated = 0, agreed = 0, compared = 0, loops = 0, loopsAgreed = 0;
    QStringList disagreeing;
    for (const QString &path : shapes) {
        const QString dir = QFileInfo(path).path();
        SFileLegacy legacy(path, QFileInfo(path).fileName(), dir);
        SFileComplex complex(path, QFileInfo(path).fileName(), dir);
        if (!legacy.loadData() || !complex.loadData() || legacy.partCount() != complex.partCount())
            continue;
        if (legacy.animationFrames() <= 0)
            continue;
        ++animated;
        const unsigned a = legacy.newState(), b = complex.newState();
        bool agree = true;
        for (int part = 1; part < legacy.partCount(); ++part) {
            const float last = legacy.partLastFrame(part);
            if (last < 0)
                continue;
            // Frame 0 against the rest pose (frame 0 keys are the rest pose
            // in content written by the exporters).
            float atRest[16], atZero[16];
            legacy.clearPose(a);
            legacy.partMatrix(a, part, atRest);
            legacy.setPartFrame(a, part, 0.0f);
            legacy.partMatrix(a, part, atZero);
            ++keyed;
            if (same(atRest, atZero, 2e-3f))
                ++restAtZero;
            // Both backends at a few frames.
            for (float f : {0.0f, last * 0.37f, last, last + 5.0f}) {
                legacy.setPartFrame(a, part, f);
                complex.setPartFrame(b, part, f);
                float l[16], c[16];
                const bool both = legacy.partMatrix(a, part, l) && complex.partMatrix(b, part, c);
                ++compared;
                if (both && same(l, c, 2e-3f))
                    ++agreed;
                else
                    agree = false;
            }
            legacy.clearPose(a);
            complex.clearPose(b);
        }
        if (!agree)
            disagreeing << QFileInfo(path).fileName();
        // A looping state as every part at the same frame.
        const unsigned looping = legacy.newState(), framed = legacy.newState();
        legacy.setAnimated(looping, true);
        const float dt = 0.37f * legacy.animationFrames() / std::max(legacy.animationRate(), 1.0f);
        legacy.updateSim(dt, looping);
        const float frame = std::fmod(dt * legacy.animationRate(), legacy.animationFrames());
        bool loopAgrees = true;
        for (int part = 0; part < legacy.partCount(); ++part)
            legacy.setPartFrame(framed, part, frame);
        for (int part = 1; part < legacy.partCount(); ++part) {
            float l[16], f[16];
            legacy.partMatrix(looping, part, l);
            legacy.partMatrix(framed, part, f);
            loopAgrees = loopAgrees && same(l, f, 2e-3f);
        }
        ++loops;
        loopsAgreed += loopAgrees ? 1 : 0;
        legacy.releaseState(looping);
        legacy.releaseState(framed);
        test.check(legacy.newState() == looping, QString("%1: a released state is taken again").arg(path));
    }
    qInfo().noquote() << "[tests:shape-animation]" << shapes.size() << "shapes," << animated << "animated;"
                      << keyed << "keyed parts," << restAtZero << "at rest at frame 0;" << agreed << "/" << compared
                      << "poses alike in both backends";
    if (!disagreeing.isEmpty())
        qInfo().noquote() << "[tests:shape-animation] backends differ:" << disagreeing.join(", ");
    test.check(animated > 0 && keyed > 0, "stock shapes with keyed parts");
    test.check(restAtZero * 100 >= keyed * 95, "keyed parts at frame 0 are at rest (95 % or more)");
    test.check(agreed == compared, "both MSTS backends pose every keyed part alike");
    test.check(loopsAgreed == loops, "a looping state poses every part at its frame");
    return test.finish();
}
