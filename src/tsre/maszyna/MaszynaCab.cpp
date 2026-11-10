/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/maszyna/MaszynaCab.h>
#include <tsre/maszyna/MaszynaText.h>
#include <algorithm>
#include <cstring>

namespace {

// The .mmd's words (MaszynaText's: white space and ;), with one put back.
class Words {
public:
    explicit Words(MaszynaText &text) : text(text) {}
    bool next(QByteArray &word) {
        if (!pending.isEmpty()) {
            word = pending.takeFirst();
            return true;
        }
        QByteArrayView view;
        if (!text.next(view))
            return false;
        word = view.toByteArray();
        return true;
    }
    // Read again by the next call.
    void back(const QByteArray &word) { pending.prepend(word); }

private:
    MaszynaText &text;
    QList<QByteArray> pending;
};

bool isLabel(const QByteArray &word) {
    return word.size() > 1 && word.endsWith(':');
}

// cab<N>definition:, cab<N>model:, driver<N>angle: ... : N, -1 for another
// word.
int numbered(const QByteArray &lower, const char *before, const char *after) {
    const qsizetype head = qsizetype(strlen(before)), tail = qsizetype(strlen(after));
    if (lower.size() != head + 1 + tail || !lower.startsWith(before) || !lower.endsWith(after))
        return -1;
    const char digit = lower[head];
    return digit >= '0' && digit <= '9' ? digit - '0' : -1;
}

// Labels MaSzyna loads with their scales times 0.1 (initialize_gauge).
bool isPressure(const QString &label) {
    static const char *const labels[] = {"brakes", "brakepress", "brakepressb", "pipepress", "pipepressb",
                                         "scndpress", "limpipepress", "cntrlpress", "springbrakepress",
                                         "epctrlvalue", "compressor", "compressorb", "pantpress"};
    for (const char *name : labels)
        if (label == QLatin1String(name))
            return true;
    return false;
}

// Whether a block key takes a sound: a lamp's soundinc: and sounddec:; a
// gauge's also soundon:, soundoff: and sound<value>: (sound-1:, sound12:).
bool takesSound(const QByteArray &key, bool lamp) {
    if (key == "soundinc:" || key == "sounddec:")
        return true;
    if (lamp)
        return false;
    if (key == "soundon:" || key == "soundoff:")
        return true;
    return key.startsWith("sound")
           && std::any_of(key.begin() + 5, key.end(), [](char c) { return c == '-' || (c >= '0' && c <= '9'); });
}

// The labels MaSzyna's cab knows, without their colon, in order
// (TTrain::initialize_gauge and initialize_button at 66a940d4; i-edenabled
// has no colon there and never matches). A lamp's starts with i-, no
// gauge's does. Any other label is passed over, and so are the words after
// it, as MaSzyna passes them over.
const char *const KnownLabels[] = {
    "alarmchain", "antislip_bt", "autosandallow_sw", "battery_sw", "batteryoff_sw", "batteryon_sw",
    "brakectrl", "brakeopmode_sw", "brakepress", "brakepressb", "brakeprofile_sw", "brakeprofileg_sw",
    "brakeprofiler_sw", "brakes", "cabactivation_sw", "cablight_sw", "cablightdim_sw", "clock",
    "clock_seconds", "cntrlpress", "compartmentlights_sw", "compartmentlightsoff_sw", "compartmentlightson_sw",
    "compressor", "compressor_sw", "compressorb", "compressorlist_sw", "compressorlocal_sw", "converter_sw",
    "converterfuse_bt", "converterlocal_sw", "converteroff_sw", "coolingfans_sw", "couplingdisconnect_sw",
    "couplingdisconnectback_sw", "dashboardlight_sw", "departure_signal_bt", "dimheadlights_sw",
    "dirbackward_bt", "dirforward_bt", "dirkey", "dirneutral_bt", "distancecounter_sw", "distcounter",
    "door_left_sw", "door_right_sw", "door_signalling_sw", "dooralloff_sw", "doorallon_sw", "doorleftoff_sw",
    "doorlefton_sw", "doorleftpermit_sw", "doormode_sw", "doorpermitpreset_sw", "doorrightoff_sw",
    "doorrighton_sw", "doorrightpermit_sw", "doorstep_sw", "eimscreen", "engageratio", "enrot1m", "enrot2m",
    "enrot3m", "epbrake_bt", "epctrlvalue", "evoltage", "fuelpump_sw", "fuse_bt", "heatingcurrent",
    "heatingvoltage", "helper_bt", "horn_bt", "hornhigh_bt", "hornlow_bt", "hvbcurrent", "hvbcurrent1",
    "hvbcurrent2", "hvbcurrent3", "hvcurrent", "hvcurrent1", "hvcurrent1b", "hvcurrent2", "hvcurrent2b",
    "hvcurrent3", "hvcurrent3b", "hvcurrentb", "hvoltage", "i-aklvents", "i-backward", "i-battery",
    "i-brake_delay_r", "i-brakeprofileg", "i-brakeprofilep", "i-brakeprofiler", "i-braking", "i-braking-ezt",
    "i-braking-ezt2", "i-brakingoff", "i-cabactived", "i-cablight", "i-comp_ovld", "i-compressor",
    "i-compressorany", "i-compressorb", "i-compressorboff", "i-compressoroff", "i-contactors", "i-contactorsb",
    "i-conv_ovld", "i-conv_ovldb", "i-converter", "i-converterb", "i-converterboff", "i-converteroff",
    "i-coolingfans", "i-dashboardlight", "i-departure_signal", "i-diff_relay", "i-diff_relay2",
    "i-distancecounter", "i-door_blocked", "i-door_blockedoff", "i-door_left", "i-door_right",
    "i-doorpermit_any", "i-doorpermit_left", "i-doorpermit_right", "i-doors", "i-doorstep", "i-dynamicbrake",
    "i-engine_overheat", "i-forward", "i-fuelpumpoff", "i-gear1", "i-gear2", "i-highcurrent", "i-hvoltageb",
    "i-hydrolockup", "i-instrumentlight", "i-instrumentlight_a", "i-instrumentlight_c", "i-instrumentlight_l",
    "i-instrumentlight_m", "i-leftend", "i-leftlight", "i-mainbreaker", "i-mainbreakerb",
    "i-mainbreakerblinking", "i-mainbreakerboff", "i-mainbreakeroff", "i-mainbreakerready", "i-mainpipelock",
    "i-malfunction", "i-malfunctionb", "i-manual_brake", "i-maxft", "i-maxftt", "i-motor_ovld",
    "i-motorblowers", "i-neutral", "i-no_resistors", "i-no_resistors_b", "i-oil_overheat", "i-radio",
    "i-radiomessage", "i-radiostop", "i-rearleftend", "i-rearleftlight", "i-rearrightend", "i-rearrightlight",
    "i-rearupperlight", "i-reserve", "i-resistors", "i-resistorsb", "i-rightend", "i-rightlight", "i-scnd",
    "i-scnd1", "i-scnd2", "i-scnd3", "i-scnd4", "i-security_aware", "i-security_aware_cabsignal",
    "i-security_cabsignal", "i-slippery", "i-springbrakeactive", "i-springbrakeinactive", "i-tempomat",
    "i-timetablelight", "i-train_controll", "i-trainheating", "i-universal0", "i-universal1", "i-universal2",
    "i-universal3", "i-universal4", "i-universal5", "i-universal6", "i-universal7", "i-universal8",
    "i-universal9", "i-upperlight", "i-vent_ovld", "i-vent_trim", "i-voltbrake", "i-water_overheat",
    "i-wateraux_overheat", "ignitionkey", "instrumentlight_sw", "inverterdisable10_bt", "inverterdisable11_bt",
    "inverterdisable12_bt", "inverterdisable1_bt", "inverterdisable2_bt", "inverterdisable3_bt",
    "inverterdisable4_bt", "inverterdisable5_bt", "inverterdisable6_bt", "inverterdisable7_bt",
    "inverterdisable8_bt", "inverterdisable9_bt", "inverterenable10_bt", "inverterenable11_bt",
    "inverterenable12_bt", "inverterenable1_bt", "inverterenable2_bt", "inverterenable3_bt",
    "inverterenable4_bt", "inverterenable5_bt", "inverterenable6_bt", "inverterenable7_bt",
    "inverterenable8_bt", "inverterenable9_bt", "invertertoggle10_bt", "invertertoggle11_bt",
    "invertertoggle12_bt", "invertertoggle1_bt", "invertertoggle2_bt", "invertertoggle3_bt",
    "invertertoggle4_bt", "invertertoggle5_bt", "invertertoggle6_bt", "invertertoggle7_bt",
    "invertertoggle8_bt", "invertertoggle9_bt", "jointctrl", "leftend_sw", "leftlight_sw", "lights_sw",
    "limpipepress", "localbrake", "lvoltage", "main_off_bt", "main_on_bt", "main_sw", "mainctrl",
    "mainctrlact", "maingearstatus", "manualbrake", "maxcurrent_sw", "mirrors_sw", "moderndimmer_sw",
    "motorblowersalloff_sw", "motorblowersfront_sw", "motorblowersrear_sw", "nextcurrent_sw", "oilpress",
    "oilpressb", "oilpump_sw", "oiltemp", "pantalloff_sw", "pantcompressor_sw", "pantcompressorvalve_sw",
    "pantfront_sw", "pantfrontoff_sw", "pantpress", "pantrear_sw", "pantrearoff_sw", "pantselect_sw",
    "pantselected_sw", "pantselectedoff_sw", "pantvalves_sw", "pantvalvesoff_bt", "pantvalvesupdate_bt",
    "pipepress", "pipepressb", "radio_sw", "radiocall1_sw", "radiocall3_sw", "radiochannel_sw",
    "radiochannelnext_sw", "radiochannelprev_sw", "radiostop_sw", "radiotest_sw", "radiovolume_sw",
    "radiovolumenext_sw", "radiovolumeprev_sw", "rearleftend_sw", "rearleftlight_sw", "rearrightend_sw",
    "rearrightlight_sw", "rearupperlight_sw", "relayreset1_bt", "relayreset2_bt", "relayreset3_bt",
    "releaser_bt", "rightend_sw", "rightlight_sw", "sand_bt", "scndctrl", "scndpress", "security_reset_bt",
    "shp_reset_bt", "shuntmodepower", "signalling_sw", "speedbutton0", "speedbutton1", "speedbutton2",
    "speedbutton3", "speedbutton4", "speedbutton5", "speedbutton6", "speedbutton7", "speedbutton8",
    "speedbutton9", "speedctrlpowerdec_bt", "speedctrlpowerinc_bt", "speeddec_bt", "speedinc_bt",
    "springbrakeoff_bt", "springbrakeon_bt", "springbrakepress", "springbraketoggle_bt", "stlinoff_bt",
    "tachometer", "tachometerb", "tachometerd", "tachometern", "tempomat_sw", "tempomatoff_sw",
    "timetablelight_sw", "trainheating_sw", "universal0", "universal1", "universal10", "universal11",
    "universal12", "universal13", "universal14", "universal15", "universal16", "universal17", "universal18",
    "universal19", "universal2", "universal20", "universal21", "universal22", "universal23", "universal24",
    "universal25", "universal26", "universal27", "universal28", "universal29", "universal3", "universal4",
    "universal5", "universal6", "universal7", "universal8", "universal9", "universalbrake1_bt",
    "universalbrake2_bt", "universalbrake3_bt", "upperlight_sw", "water1temp", "water1tempb", "water2temp",
    "watercircuitslink_sw", "waterheater_sw", "waterheaterbreaker_sw", "waterpump_sw", "waterpumpbreaker_sw",
    "whistle_bt", "wipers_sw"
};

bool isKnown(const QByteArray &label) {
    const auto end = std::end(KnownLabels);
    const auto found = std::lower_bound(std::begin(KnownLabels), end, label.constData(),
                                        [](const char *a, const char *b) { return std::strcmp(a, b) < 0; });
    return found != end && label == *found;
}

// A word as MaSzyna's parser reads a number: its leading number ("0.1soundinc:"
// is 0.1), 0 where it starts with none.
float numberOf(const QByteArray &word, bool *ok) {
    const char *start = word.constData();
    char *end = nullptr;
    const float value = std::strtof(start, &end);
    *ok = end != start;
    return *ok ? value : 0.0f;
}

// A known entry's words taken as MaSzyna takes them: as many as it reads,
// whatever they are.
class Reader {
public:
    Reader(Words &words, QStringList &problems) : words(words), problems(problems) {}

    // The next word; false at the file's end.
    bool word(const QString &label, QByteArray &word) {
        if (words.next(word))
            return true;
        problems << label + ": cut short by the end of the file";
        return false;
    }
    float number(const QString &label) {
        QByteArray w;
        if (!word(label, w))
            return 0.0f;
        bool ok = false;
        const float value = numberOf(w, &ok);
        if (!ok)
            problems << QString("%1: \"%2\" is no number").arg(label, MaszynaText::decode(w));
        return value;
    }

    // A sound's value: a word, or a block's words.
    QString soundValue() {
        QByteArray w;
        if (!words.next(w))
            return QString();
        if (w != "{")
            return MaszynaText::decode(w);
        QStringList inside;
        int depth = 1;
        while (depth > 0 && words.next(w)) {
            if (w == "{")
                ++depth;
            else if (w == "}")
                --depth;
            if (depth > 0)
                inside << MaszynaText::decode(w);
        }
        return "{ " + inside.join(' ') + " }";
    }

    // A control's block keys to its }, as TGauge::Load_mapping and
    // TButton::Load_mapping take them: type: and sound keys with a value,
    // other keys without one.
    void keys(MaszynaCab::Control &control) {
        QByteArray w;
        while (words.next(w) && w != "}") {
            const QByteArray key = w.toLower();
            const QString name = QString::fromLatin1(key.endsWith(':') ? key.chopped(1) : key);
            QString text;
            if (!control.lamp && key == "type:") {
                if (words.next(w)) {
                    text = MaszynaText::decode(w);
                    const QByteArray type = w.toLower();
                    control.type = type == "push" || type == "impulse" || type == "return" ? MaszynaCab::Type::Push
                                   : type == "delayed"                                     ? MaszynaCab::Type::Delayed
                                   : type == "pushtoggle"                                  ? MaszynaCab::Type::PushToggle
                                                                                           : MaszynaCab::Type::Toggle;
                }
            } else if (!control.lamp && key == "soundproofing:") {
                // Six numbers; a set in brackets comes as its first.
                QStringList numbers;
                for (int i = 0; i < 6 && words.next(w); ++i) {
                    bool ok = false;
                    numberOf(w, &ok);
                    if (!ok) {
                        words.back(w);
                        break;
                    }
                    numbers << MaszynaText::decode(w);
                }
                text = numbers.join(' ');
            } else if (takesSound(key, control.lamp)) {
                text = soundValue();
            }
            control.keys.push_back({name, text});
        }
    }

    // TGauge::Load: old `submodel anim scale offset friction [endvalue
    // endscale]`, or the same in a block with keys after it.
    void gauge(MaszynaCab::Control &control) {
        QByteArray w;
        if (!word(control.label, w))
            return;
        const bool block = w == "{";
        if (block && !word(control.label, w))
            return;
        control.submodel = MaszynaText::decode(w);
        if (!word(control.label, w))
            return;
        const QByteArray animation = w.toLower();
        control.animationName = QString::fromLatin1(animation);
        control.animation = animation == "rot" || animation == "rotvar" ? MaszynaCab::Animation::Rotate
                            : animation == "mov" || animation == "movvar" ? MaszynaCab::Animation::Move
                            : animation == "wip"                         ? MaszynaCab::Animation::Wiper
                            : animation == "dgt"                         ? MaszynaCab::Animation::Digital
                                                                         : MaszynaCab::Animation::None;
        control.interpolated = animation == "rotvar" || animation == "movvar";
        control.scale = number(control.label);
        control.offset = number(control.label);
        control.friction = number(control.label);
        if (control.interpolated) {
            control.endValue = number(control.label);
            control.endScale = number(control.label);
        }
        if (block)
            keys(control);
        if (isPressure(control.label)) {
            control.scale *= 0.1f;
            if (control.interpolated)
                control.endScale *= 0.1f;
        }
    }

    // TButton::Load: `submodel`, or `{ submodel keys }`.
    void lamp(MaszynaCab::Control &control) {
        control.lamp = true;
        QByteArray w;
        if (!word(control.label, w))
            return;
        if (w != "{") {
            control.submodel = MaszynaText::decode(w);
            return;
        }
        if (word(control.label, w))
            control.submodel = MaszynaText::decode(w);
        keys(control);
    }

    // screen_entry::deserialize: `{ script target: t updatetime: n
    // parameters: p }`; any other word is the target and the next the
    // script, which ends it (the old `pyscreen: target script`).
    MaszynaCab::Screen screen() {
        MaszynaCab::Screen screen;
        QByteArray w;
        while (words.next(w) && w != "}") {
            const QByteArray key = w.toLower();
            QByteArray v;
            if (key == "{") {
                if (words.next(v))
                    screen.script = MaszynaText::decode(v);
            } else if (key == "target:") {
                if (words.next(v))
                    screen.target = MaszynaText::decode(v);
            } else if (key == "updatetime:" || key == "parameters:") {
                if (words.next(v))
                    screen.keys.push_back({QString::fromLatin1(key.chopped(1)), MaszynaText::decode(v)});
            } else {
                screen.target = MaszynaText::decode(w);
                if (words.next(v))
                    screen.script = MaszynaText::decode(v);
                break;
            }
        }
        return screen;
    }

    // The box and what follows it, in MaSzyna's order: cablight: (nine
    // numbers, not used), the box, driver<N>angle:, driver<N>pos:,
    // driver<N>sitpos:.
    void header(MaszynaCab::End &end, int n) {
        end.defined = true;
        const QString label = QString("cab%1definition").arg(n);
        QByteArray w;
        if (!words.next(w))
            return;
        if (w.toLower() == "cablight:") {
            for (int i = 0; i < 9; ++i)
                number(label);
        } else {
            words.back(w);
        }
        for (float &v : end.box)
            v = number(label);
        if (!words.next(w))
            return;
        if (numbered(w.toLower(), "driver", "angle:") == n) {
            end.hasAngle = true;
            end.angle[0] = number(label);
            end.angle[1] = number(label);
            if (!words.next(w))
                return;
        }
        if (numbered(w.toLower(), "driver", "pos:") == n) {
            end.hasPosition = true;
            for (int i = 0; i < 3; ++i)
                end.sitting[i] = end.position[i] = number(label);
            if (!words.next(w))
                return;
        }
        if (numbered(w.toLower(), "driver", "sitpos:") == n) {
            end.hasSitting = true;
            for (float &v : end.sitting)
                v = number(label);
            if (!words.next(w))
                return;
        }
        words.back(w);
    }

private:
    Words &words;
    QStringList &problems;
};

QString modelName(const QByteArray &word) {
    QString name = MaszynaText::decode(word).toLower();
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (name.startsWith(QLatin1Char('/')))
        name.remove(0, 1);
    return name;
}

}

MaszynaCab MaszynaCab::read(const QString &folder, const QString &name, const QStringList &parameters) {
    MaszynaCab cab;
    MaszynaText text(folder, name, parameters);
    if (!text.opened())
        return cab;
    Words words(text);
    Reader reader(words, cab.problems);
    enum State { NotStarted, Reading, Done };
    State state[3] = {NotStarted, NotStarted, NotStarted};
    QByteArray word;
    while (words.next(word)) {
        const QByteArray lower = word.toLower();
        const int defined = numbered(lower, "cab", "definition:");
        if (defined == 0) {
            state[1] = state[1] == Reading ? Done : state[1];
            state[2] = state[2] == Reading ? Done : state[2];
            if (state[0] == Reading) {
                state[0] = Done;
            } else if (state[0] == NotStarted) {
                state[0] = Reading;
                reader.header(cab.ends[0], 0);
            }
            continue;
        }
        if (defined == 1 || defined == 2) {
            if (state[defined] == NotStarted) {
                state[defined] = Reading;
                reader.header(cab.ends[defined], defined);
            }
            continue;
        }
        bool reading = false;
        for (State s : state)
            reading = reading || s == Reading;
        if (!reading)
            continue;
        const int model = numbered(lower, "cab", "model:");
        if (model >= 0) {
            // Another cab's model is passed over with its name.
            if (model <= 2 && state[model] == Reading && words.next(word)) {
                cab.ends[model].model = modelName(word);
                cab.ends[model].controls.clear();
            }
            continue;
        }
        if (!isLabel(word))
            continue;
        const QByteArray bare = lower.chopped(1);
        const QString label = QString::fromLatin1(bare);
        Control control;
        control.label = label;
        bool isScreen = false;
        Screen screen;
        if (bare == "pyscreen") {
            isScreen = true;
            screen = reader.screen();
        } else if (bare == "pyscreenupdatetime") {
            QByteArray skipped;
            reader.word(label, skipped);
            continue;
        } else if (!isKnown(bare)) {
            continue;
        } else if (bare == "clock") {
            QByteArray kind;
            if (reader.word(label, kind))
                control.word = MaszynaText::decode(kind).toLower();
        } else if (bare == "eimscreen" || bare == "brakes") {
            control.index[0] = int(reader.number(label));
            control.index[1] = int(reader.number(label));
            reader.gauge(control);
        } else if (bare == "i-doors") {
            control.index[0] = int(reader.number(label));
            reader.lamp(control);
        } else if (label.startsWith(QLatin1String("i-"))) {
            reader.lamp(control);
        } else {
            reader.gauge(control);
        }
        for (int n = 0; n < 3; ++n) {
            if (state[n] != Reading)
                continue;
            if (isScreen)
                cab.ends[n].screens.push_back(screen);
            else
                cab.ends[n].controls.push_back(control);
        }
    }
    cab.problems += text.problems();
    return cab;
}
