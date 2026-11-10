/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MASZYNACAB_H
#define MASZYNACAB_H

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

// A vehicle's cabs in its .mmd as MaSzyna reads them (task maszyna 04;
// TTrain::InitializeCab, TGauge::Load, TButton::Load at 66a940d4). Cab 1 is
// at the vehicle's +Z end, cab 2 at its -Z end, cab 0 the engine room. A cab
// reads from its first cab<N>definition: to the end of the file or the next
// cab0definition: (cab 0 to the end or its next definition), so controls
// written once after cab 2's section serve cab 1 too; its cab<N>model:
// clears the controls read before it. Words split at white space and ;. A
// label MaSzyna knows (386 of them) takes as many words as MaSzyna reads
// for it, whatever they are; numbers are read by their leading digits
// ("0.1soundinc:" is 0.1). Every other word is passed over, so are labels
// MaSzyna does not know (destination signs, block keys written without
// their block) and other cabs' entries.
struct MaszynaCab {
    // TGaugeAnimation: rot and rotvar turn the submodel about its own Y,
    // mov and movvar move it along its own Z, wip turns it with its child
    // and grandchild, dgt turns its children named by digits.
    enum class Animation { None, Rotate, Move, Wiper, Digital };
    // TGaugeType: push (also impulse, return), delayed, pushtoggle, toggle
    // (the default).
    enum class Type { Toggle, Push, Delayed, PushToggle };

    struct Control {
        // In lower case, without its colon: "mainctrl", "i-mainbreaker".
        QString label;
        // As written; MaSzyna finds it in any case, else with _off after
        // it, and shows <submodel>_on while the control's state is on.
        QString submodel;
        // An i-... lamp: only <submodel>_on and <submodel>_off.
        bool lamp = false;
        Animation animation = Animation::None;
        // As written, in lower case ("rot", "rotvar", ...); empty for a lamp.
        QString animationName;
        // Its value v shown at v · scale + offset; rotvar and movvar
        // (interpolated) take the scale toward endScale as v goes to
        // endValue. A pressure gauge's scales times 0.1, as MaSzyna loads
        // them (brakepress, pipepress, compressor ...). friction: the time
        // constant (s) the shown value follows its target with.
        float scale = 0.0f;
        float offset = 0.0f;
        float friction = 0.0f;
        float endValue = -1.0f;
        float endScale = -1.0f;
        bool interpolated = false;
        Type type = Type::Toggle;
        // The numbers some labels take before their control: eimscreen: i j,
        // brakes: i j, i-doors: n; -1 none.
        int index[2] = {-1, -1};
        // A word some labels take instead of a control (clock: analog).
        QString word;
        // Its block's other keys in order (soundinc:, sound12:, type: ...),
        // without their colon, in lower case, with their values as written
        // (a sound's block as its words).
        QVector<QPair<QString, QString>> keys;
    };

    // A Python screen (pyscreen:): the submodel whose texture it draws in,
    // its script, its block's other keys.
    struct Screen {
        QString target, script;
        QVector<QPair<QString, QString>> keys;
    };

    struct End {
        bool defined = false;
        // cab<N>definition:'s box, x1 y1 z1 x2 y2 z2 (metres, the vehicle's
        // model space: Y up, Z toward cab 1, X left).
        float box[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        // driver<N>angle: yaw then pitch, degrees; driver<N>pos: the head;
        // driver<N>sitpos: the head seated (the head's place when not
        // given). Only right after the box and in this order, as MaSzyna
        // reads them.
        bool hasAngle = false;
        float angle[2] = {0.0f, 0.0f};
        bool hasPosition = false;
        float position[3] = {0.0f, 0.0f, 0.0f};
        bool hasSitting = false;
        float sitting[3] = {0.0f, 0.0f, 0.0f};
        // cab<N>model: in lower case with / and no leading one, as written
        // (".t3d"); "none": the controls are on the vehicle's own model;
        // empty when not given.
        QString model;
        // In the order read.
        QVector<Control> controls;
        QVector<Screen> screens;
    };

    End ends[3];
    // Words that should be numbers and entries cut short by the file's end,
    // for the messages; includes not found.
    QStringList problems;

    bool hasCab() const { return ends[0].defined || ends[1].defined || ends[2].defined; }

    // The .mmd `name` in the vehicle's `folder` (where its includes are
    // looked for); MaSzyna reads a cab without parameters, so a (pN) in it
    // stays as written.
    static MaszynaCab read(const QString &folder, const QString &name, const QStringList &parameters = QStringList());
};

#endif // MASZYNACAB_H
