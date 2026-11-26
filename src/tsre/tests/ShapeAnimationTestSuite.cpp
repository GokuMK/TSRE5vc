#include "ShapeAnimationTestSuite.h"
#include "TokenTestSupport.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cmath>
#include <tsre/Game.h>
#include <tsre/shape/SFileComplex.h>
#include <tsre/shape/SFileLegacy.h>
#include <tsre/shape/ShapeAnimation.h>
#include <tsre/shape/ShapeClock.h>
#include <tsre/ui/UiShapeDisplays.h>
#include <QMatrix4x4>

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

// Five keys a quarter turn apart about z, from 12 o'clock round to 12.
#define CLOCK_TURN "slerp_rot ( 0 0 0 0 1 ) slerp_rot ( 1 0 0 0.7071068 0.7071068 ) slerp_rot ( 2 0 0 1 0 )" \
    " slerp_rot ( 3 0 0 0.7071068 -0.7071068 ) slerp_rot ( 4 0 0 0 -1 )"

// An analog clock (task shapes 22): an hour hand, a minute hand named in
// lower case with a suffix and position keys too, a second hand with four
// keys, a sweeping second hand with one key, a part that is no hand.
const char ClockShape[] = "SIMISA@@@@@@@@@@JINX0s1t______\r\n"
"shape (\n"
" shape_header ( 00000000 00000000 )\n"
" shader_names ( 1 named_shader ( TexDiff ) )\n"
" texture_filter_names ( 1 named_filter_mode ( MipLinear ) )\n"
" points ( 4 point ( 0 0 0 ) point ( 1 0 0 ) point ( 1 1 0 ) point ( 0 1 0 ) )\n"
" uv_points ( 4 uv_point ( 0 1 ) uv_point ( 1 1 ) uv_point ( 1 0 ) uv_point ( 0 0 ) )\n"
" normals ( 1 vector ( 0 0 1 ) )\n"
" sort_vectors ( 0 ) colours ( 0 )\n"
" matrices ( 6 matrix MAIN ( 1 0 0 0 1 0 0 0 1 0 0 0 )\n"
"  matrix ORTS_HHand_Clock01 ( 1 0 0 0 1 0 0 0 1 0 4 0 )\n"
"  matrix orts_mhand_clock01_face ( 1 0 0 0 1 0 0 0 1 0 4 0 )\n"
"  matrix ORTS_SHand_Clock01 ( 1 0 0 0 1 0 0 0 1 0 4 0 )\n"
"  matrix ORTS_CHand_Clock01 ( 1 0 0 0 1 0 0 0 1 0 4 0 )\n"
"  matrix FLAG ( 1 0 0 0 1 0 0 0 1 0 2 0 ) )\n"
" images ( 1 image ( body.ace ) )\n"
" textures ( 1 texture ( 0 0 0 ff000000 ) )\n"
" light_materials ( 0 )\n"
" light_model_cfgs ( 1 light_model_cfg ( 00000000 uv_ops ( 1 uv_op_copy ( 1 0 ) ) ) )\n"
" vtx_states ( 1 vtx_state ( 00000000 0 -5 0 00000002 ) )\n"
" prim_states ( 1 prim_state Body ( 00000000 0 tex_idxs ( 1 0 ) 0 0 0 0 1 ) )\n"
" lod_controls ( 1 lod_control ( distance_levels_header ( 0 ) distance_levels ( 1\n"
" distance_level ( distance_level_header ( dlevel_selection ( 1000 ) hierarchy ( 6 -1 0 0 0 0 0 ) )\n"
" sub_objects ( 1 sub_object ( sub_object_header ( 00000400 -1 -1 000001d2 000001c4\n"
"  geometry_info ( 2 1 0 6 0 0 1 0 0 0 geometry_nodes ( 1 geometry_node ( 1 0 0 0 0 cullable_prims ( 1 2 6 ) ) )\n"
"   geometry_node_map ( 1 0 ) )\n"
"  subobject_shaders ( 1 0 ) subobject_light_cfgs ( 1 0 ) 0 )\n"
" vertices ( 4 vertex ( 00000000 0 0 ffffffff ff000000 vertex_uvs ( 1 0 ) )\n"
"  vertex ( 00000000 1 0 ffffffff ff000000 vertex_uvs ( 1 1 ) )\n"
"  vertex ( 00000000 2 0 ffffffff ff000000 vertex_uvs ( 1 2 ) )\n"
"  vertex ( 00000000 3 0 ffffffff ff000000 vertex_uvs ( 1 3 ) ) )\n"
" vertex_sets ( 1 vertex_set ( 0 0 4 ) )\n"
" primitives ( 2\n"
"  prim_state_idx ( 0 ) indexed_trilist ( vertex_idxs ( 6 0 1 2 0 2 3 ) normal_idxs ( 2 0 3 0 3 ) flags ( 2 00000000 00000000 ) ) )\n"
" ) ) ) ) ) )\n"
" animations ( 1 animation ( 4 30 anim_nodes ( 6\n"
"  anim_node MAIN ( controllers ( 0 ) )\n"
"  anim_node ORTS_HHand_Clock01 ( controllers ( 1 tcb_rot ( 5 " CLOCK_TURN " ) ) )\n"
"  anim_node orts_mhand_clock01_face ( controllers ( 2 tcb_rot ( 5 " CLOCK_TURN " )\n"
"   linear_pos ( 5 linear_key ( 0 0 4 0 ) linear_key ( 1 0 4 0 ) linear_key ( 2 0 4 0 ) linear_key ( 3 0 4 0 ) linear_key ( 4 0 4 0 ) ) ) )\n"
"  anim_node ORTS_SHand_Clock01 ( controllers ( 1 tcb_rot ( 4 slerp_rot ( 0 0 0 0 1 ) slerp_rot ( 1 0 0 0.7071068 0.7071068 )"
" slerp_rot ( 2 0 0 1 0 ) slerp_rot ( 3 0 0 0.7071068 -0.7071068 ) ) ) )\n"
"  anim_node ORTS_CHand_Clock01 ( controllers ( 1 tcb_rot ( 1 slerp_rot ( 0 0 0 0 1 ) ) ) )\n"
"  anim_node FLAG ( controllers ( 1 tcb_rot ( 2 slerp_rot ( 0 0 0 0 1 ) slerp_rot ( 4 0 0 0.7071068 0.7071068 ) ) ) )\n"
" ) ) )\n"
")";

bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// A text shape as MSTS writes one: UTF-16LE with a byte order mark.
QByteArray utf16(const QByteArray &text) {
    const QString s = QString::fromLatin1(text);
    QByteArray out("\xff\xfe", 2);
    out.append(reinterpret_cast<const char *>(s.utf16()), s.size() * 2);
    return out;
}

// The hands found: part and hand, in part order.
QString handList(const std::vector<ShapeClock::HandPart> &hands) {
    QStringList list;
    for (const ShapeClock::HandPart &h : hands)
        list << QString("%1:%2").arg(h.part).arg(int(h.hand));
    return list.join(' ');
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

    // Open Rails' analog clocks (task shapes 22).
    {
        using ShapeClock::Hand;
        Hand h = Hand::Minute;
        const bool hour = ShapeClock::handOf("ORTS_HHand_Clock01_sosnowiec", h) && h == Hand::Hour;
        const bool sweep = ShapeClock::handOf("orts_chand_clock", h) && h == Hand::SweepSecond;
        test.check(hour && sweep && !ShapeClock::handOf("xORTS_HHand_Clock", h) && !ShapeClock::handOf("ORTS_Hand_Clock", h)
                       && !ShapeClock::handOf("ORTS_HHand_Clok", h),
                   "clock hands by Open Rails' names: any case, any suffix, nothing before");
        // 10:30:50.25: the hour hand half way from 9 to 12, the minute hand
        // at 6, the seconds in the last quarter, a hundredth a second on.
        const double at = 10 * 3600 + 30 * 60 + 50.25;
        int q[4];
        float a[4];
        ShapeClock::quarter(Hand::Hour, at, q[0], a[0]);
        ShapeClock::quarter(Hand::Minute, at, q[1], a[1]);
        ShapeClock::quarter(Hand::Second, at, q[2], a[2]);
        ShapeClock::quarter(Hand::SweepSecond, at, q[3], a[3]);
        test.check(q[0] == 3 && near(a[0], 0.5f) && q[1] == 2 && near(a[1], 0.0f) && q[2] == 3 && near(a[2], 1.0f / 3)
                       && q[3] == 3 && near(a[3], 1.0f / 3 + 25.0f / 1500),
                   "a hand's quarter and how far into it, as Open Rails reckons them");
        int qn, qd;
        float an, ad;
        ShapeClock::quarter(Hand::Hour, -60.0, qn, an);
        ShapeClock::quarter(Hand::Minute, 86400.0 + 7 * 60 + 59, qd, ad);
        test.check(qn == 3 && near(an, 2.0f / 3 + 59.0f / 180) && qd == 0 && near(ad, 7.0f / 15),
                   "the time of any day: before midnight, the next day");

        QTemporaryDir dir;
        const QString path = dir.path() + "/clock.s";
        const QString still = dir.path() + "/still.s";
        QByteArray stillText(ClockShape);
        stillText.truncate(stillText.indexOf(" animations ("));
        stillText += ")";
        if (!dir.isValid() || !writeFile(path, utf16(ClockShape)) || !writeFile(still, utf16(stillText))) {
            test.check(false, "clock shapes written");
        } else {
            SFileLegacy legacy(path, "clock.s", dir.path());
            SFileComplex complex(path, "clock.s", dir.path());
            const bool read = legacy.loadData() && complex.loadData();
            const std::vector<ShapeClock::HandPart> hl = ShapeClock::hands(legacy), hc = ShapeClock::hands(complex);
            test.check(read && handList(hl) == "1:0 2:1 3:2 4:3" && handList(hc) == handList(hl),
                       QString("a clock's hands in both backends, the other parts not (%1; %2)").arg(handList(hl), handList(hc)));
            SFileLegacy stillShape(still, "still.s", dir.path());
            test.check(stillShape.loadData() && stillShape.partCount() == 6 && ShapeClock::hands(stillShape).empty(),
                       "hands without an animation: no clock (Open Rails leaves it at rest)");
            // The frames: by key index; four keys only: the last quarter shows
            // the first (Open Rails); one key: none.
            const float fh = ShapeClock::frame(legacy, 1, Hand::Hour, at), fm = ShapeClock::frame(legacy, 2, Hand::Minute, at),
                        fs = ShapeClock::frame(legacy, 3, Hand::Second, at), fc = ShapeClock::frame(legacy, 4, Hand::SweepSecond, at);
            test.check(near(fh, 3.5f) && near(fm, 2.0f) && near(fs, 1.0f / 3) && fc < 0.0f
                           && near(ShapeClock::frame(complex, 1, Hand::Hour, at), fh),
                       QString("each hand's frame (%1 %2 %3 %4)").arg(fh).arg(fm).arg(fs).arg(fc));
            // Posed: 03:00 a quarter turn, 10:30 the hour hand at 315 degrees,
            // alike in both backends; the minute hand where its keys hold it.
            // The y row (the root's x mirror leaves it): the turn's cosine in
            // [5], its sine in [1].
            const unsigned sl = legacy.newState(), sc = complex.newState();
            float l[16], c[16];
            legacy.setPartFrame(sl, 1, ShapeClock::frame(legacy, 1, Hand::Hour, 3 * 3600.0));
            complex.setPartFrame(sc, 1, ShapeClock::frame(complex, 1, Hand::Hour, 3 * 3600.0));
            const bool three = legacy.partMatrix(sl, 1, l) && complex.partMatrix(sc, 1, c) && same(l, c)
                    && near(l[5], 0.0f, 1e-3f) && near(std::fabs(l[1]), 1.0f, 1e-3f);
            legacy.setPartFrame(sl, 1, fh);
            complex.setPartFrame(sc, 1, ShapeClock::frame(complex, 1, Hand::Hour, at));
            legacy.setPartFrame(sl, 2, fm);
            const bool half = legacy.partMatrix(sl, 1, l) && complex.partMatrix(sc, 1, c) && same(l, c)
                    && near(l[5], std::sqrt(0.5f), 1e-3f) && near(std::fabs(l[1]), std::sqrt(0.5f), 1e-3f);
            float m[16];
            const bool minute = legacy.partMatrix(sl, 2, m) && near(m[5], -1.0f, 1e-3f) && near(m[13], 4.0f, 1e-3f);
            test.check(three && half && minute, "the hands posed: 03:00 a quarter turn, 10:30 the hour hand at 315 degrees, both backends alike");
        }
    }

    const QStringList shapes = stockShapes();
    if (shapes.isEmpty()) {
        qInfo() << "[tests:shape-animation] SKIP stock shapes: no TRAINS/TRAINSET (TSRE_ANIMATION_SHAPES)";
        return test.finish();
    }
    int keyed = 0, restAtZero = 0, animated = 0, agreed = 0, compared = 0, loops = 0, loopsAgreed = 0;
    bool squareChecked = false;
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
            // A display's square hung on a keyed part (task scripting 02),
            // found by the part's name: where the state poses the part, its
            // place in the part's space with it; the first part that moves.
            if (!squareChecked) {
                ::ShapeDisplays::Display d;
                d.square = true;
                d.hierarchy = legacy.partName(part);
                d.position[0] = 0.1f;
                d.position[1] = 0.2f;
                d.position[2] = 0.3f;
                d.size[0] = d.size[1] = 1.0f;
                const int found = Ui::ShapeDisplays::squarePart(legacy, d);
                QVector3D centre[2], expected[2];
                for (int k = 0; k < 2; ++k) {
                    legacy.setPartFrame(a, part, k == 0 ? 0.0f : last);
                    centre[k] = Ui::ShapeDisplays::squareMatrix(legacy, a, found, d).map(QVector3D());
                    float m[16];
                    legacy.partMatrix(a, part, m);
                    QMatrix4x4 posed;
                    std::copy(m, m + 16, posed.data());
                    expected[k] = posed.map(QVector3D(0.1f, 0.2f, 0.3f));
                }
                legacy.clearPose(a);
                if (found == part && (expected[1] - expected[0]).length() > 0.05f) {
                    squareChecked = true;
                    test.check((centre[0] - expected[0]).length() < 1e-3f && (centre[1] - expected[1]).length() < 1e-3f,
                               QString("a display's square moves with the part it hangs on (%1, %2, %3 m)")
                                   .arg(QFileInfo(path).fileName(), d.hierarchy)
                                   .arg(double((centre[1] - centre[0]).length()), 0, 'f', 2));
                }
            }
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
