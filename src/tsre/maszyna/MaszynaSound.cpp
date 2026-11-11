/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/maszyna/MaszynaSound.h>
#include <tsre/maszyna/MaszynaFiz.h>
#include <tsre/maszyna/MaszynaText.h>

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace MaszynaSound;

namespace {

// MaSzyna's ranges (audio/sound.h).
constexpr float CabControlsRange = 7.5f;
constexpr float GlobalRange = -1.0f;
constexpr float BrakingRange = 100.0f;
constexpr float RunningNoiseRange = 200.0f;

// Its soundproofing steps (sound.h): square roots of 1, 0.65, 0.2, 0.01.
constexpr float None = 1.0f;
const float Some = std::sqrt(0.65f);
const float Strong = std::sqrt(0.20f);
constexpr float VeryStrong = 0.1f;

// The words MaSzyna's cParser gives: white space and ; split them (as
// MaszynaText's), and per call more separators: , always, [ ] where a key
// or a vector is read. A word is taken up to the next separator, the rest
// of it kept for the next call, as cParser reads its buffer.
class Words {
public:
    explicit Words(MaszynaText &text) : text(&text) {}
    explicit Words(const QByteArray &string) : string(string) {}

    QByteArray take(const char *separators, bool lower = true) {
        for (;;) {
            if (pending.isEmpty() && !raw(pending))
                return QByteArray();
            qsizetype from = 0;
            while (from < pending.size() && strchr(separators, pending[from]) != nullptr)
                ++from;
            if (from == pending.size()) {
                pending.clear();
                continue;
            }
            qsizetype to = from;
            while (to < pending.size() && strchr(separators, pending[to]) == nullptr)
                ++to;
            QByteArray word = pending.mid(from, to - from);
            pending = pending.mid(to);
            return lower ? word.toLower() : word;
        }
    }

private:
    bool raw(QByteArray &word) {
        if (text != nullptr) {
            QByteArrayView view;
            if (!text->next(view))
                return false;
            word = view.toByteArray();
            return true;
        }
        while (at < string.size() && (QChar::isSpace(uchar(string[at])) || string[at] == ';'))
            ++at;
        if (at >= string.size())
            return false;
        const qsizetype from = at;
        while (at < string.size() && !QChar::isSpace(uchar(string[at])) && string[at] != ';')
            ++at;
        word = string.mid(from, at - from);
        return true;
    }

    MaszynaText *text = nullptr;
    QByteArray string;
    qsizetype at = 0;
    QByteArray pending;
};

// A number as stringstream >> float reads one: its leading number, 0 when
// there is none.
float number(const QByteArray &word) {
    const QByteArray text = word.trimmed();
    return text.isEmpty() ? 0.0f : float(std::strtod(text.constData(), nullptr));
}

enum Form { Single, Multipart };
enum Parameters { NoNumbers = 0, Range = 1, Amplitude = 2, Frequency = 4 };

// How a key reads and where its sound sits (DynObj.h's and Train.cpp's
// members, LoadMMediaFile's calls).
struct Key {
    const char *name;
    Form form;
    int numbers;
    Placement placement;
    float range;
};

// sounds: (DynObj). engine:, tractionmotor: and tractionacmotor: only on a
// powered vehicle; radiatorfan2 without its colon, as MaSzyna compares it
// (so radiatorfan2: never matches).
const Key VehicleKeys[] = {
    {"engine:", Single, Range | Amplitude | Frequency, Placement::Engine, 50.0f},
    {"fakeengine:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"dieselinc:", Single, Range, Placement::Engine, 50.0f},
    {"oilpump:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"fuelpump:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"waterpump:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"waterheater:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"tractionmotor:", Single, Range | Amplitude | Frequency, Placement::External, 50.0f},
    {"tractionacmotor:", Single, Range | Amplitude | Frequency, Placement::External, 50.0f},
    {"motorblower:", Single, Range | Amplitude | Frequency, Placement::Engine, 50.0f},
    {"inverter:", Single, Range | Amplitude | Frequency, Placement::Engine, 50.0f},
    {"ventilator:", Single, Range | Amplitude | Frequency, Placement::Engine, 50.0f},
    {"radiatorfan1:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"radiatorfan2", Single, NoNumbers, Placement::Engine, 50.0f},
    {"transmission:", Single, Range, Placement::Engine, 50.0f},
    {"brakesound:", Single, Amplitude | Frequency, Placement::External, BrakingRange},
    {"brake:", Single, Range | Amplitude, Placement::External, BrakingRange},
    {"brakecylinderinc:", Single, NoNumbers, Placement::External, 50.0f},
    {"brakecylinderdec:", Single, NoNumbers, Placement::External, 50.0f},
    {"epbrakeinc:", Single, NoNumbers, Placement::External, 50.0f},
    {"epbrakedec:", Single, NoNumbers, Placement::External, 50.0f},
    {"emergencybrake:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"brakeacc:", Single, NoNumbers, Placement::External, 50.0f},
    {"unbrake:", Single, Range, Placement::External, 50.0f},
    {"springbrake:", Single, NoNumbers, Placement::External, 50.0f},
    {"springbrakeoff:", Single, NoNumbers, Placement::External, 50.0f},
    {"derail:", Single, Range, Placement::External, 2 * RunningNoiseRange},
    {"curve:", Single, Range, Placement::External, RunningNoiseRange},
    {"horn1:", Multipart, Range, Placement::External, 5 * RunningNoiseRange},
    {"horn2:", Multipart, Range, Placement::External, 5 * RunningNoiseRange},
    {"horn3:", Multipart, Range, Placement::External, 5 * RunningNoiseRange},
    {"pantographup:", Single, NoNumbers, Placement::External, 50.0f},
    {"pantographdown:", Single, NoNumbers, Placement::External, 50.0f},
    {"compressor:", Multipart, Range, Placement::Engine, 50.0f},
    {"compressoridle:", Multipart, Range, Placement::Engine, 50.0f},
    {"converter:", Multipart, Range, Placement::Engine, 50.0f},
    {"brakingresistorventilator:", Multipart, Range, Placement::Engine, 50.0f},
    {"heater:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"battery:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"turbo:", Multipart, Range, Placement::Engine, 50.0f},
    {"small-compressor:", Multipart, Range, Placement::Engine, 50.0f},
    {"departuresignal:", Multipart, Range, Placement::General, 25.0f},
    {"dooropen:", Single, NoNumbers, Placement::General, 50.0f},
    {"doorclose:", Single, NoNumbers, Placement::General, 50.0f},
    {"doorlock:", Single, NoNumbers, Placement::General, 50.0f},
    {"doorunlock:", Single, NoNumbers, Placement::General, 50.0f},
    {"doorstepopen:", Single, NoNumbers, Placement::General, 50.0f},
    {"doorstepclose:", Single, NoNumbers, Placement::General, 50.0f},
    {"doorpermit:", Single, NoNumbers, Placement::General, 50.0f},
    {"unloading:", Single, NoNumbers, Placement::General, 50.0f},
    {"loading:", Single, NoNumbers, Placement::General, 50.0f},
    {"sand:", Multipart, Range, Placement::External, 50.0f},
    {"releaser:", Multipart, Range, Placement::External, 50.0f},
    {"outernoise:", Single, Amplitude | Frequency, Placement::External, RunningNoiseRange},
    {"wheelflat:", Single, Frequency, Placement::External, RunningNoiseRange},
};

// internaldata: (DynObj).
const Key VehicleInternalKeys[] = {
    {"ignition:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"shutdown:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"engageslippery:", Single, Amplitude | Frequency, Placement::Engine, 50.0f},
    {"wiperfrompark:", Single, NoNumbers, Placement::Internal, 50.0f},
    {"wipertopark:", Single, NoNumbers, Placement::Internal, 50.0f},
    {"retarder:", Single, Amplitude | Frequency, Placement::Engine, 50.0f},
    {"linebreakerclose:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"linebreakeropen:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"relay:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"shuntfield:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"wejscie_na_bezoporow:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"wejscie_na_drugi_uklad:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"directionrelay_d:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"directionrelay_r:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"directionrelay_n:", Single, NoNumbers, Placement::Engine, 50.0f},
    {"pneumaticrelay:", Single, NoNumbers, Placement::External, 50.0f},
    {"slipperysound:", Single, Amplitude, Placement::External, BrakingRange},
    {"couplerattach:", Single, NoNumbers, Placement::External, 50.0f},
    {"brakehoseattach:", Single, NoNumbers, Placement::External, 50.0f},
    {"mainhoseattach:", Single, NoNumbers, Placement::External, 50.0f},
    {"controlattach:", Single, NoNumbers, Placement::External, 50.0f},
    {"gangwayattach:", Single, NoNumbers, Placement::External, 50.0f},
    {"heatingattach:", Single, NoNumbers, Placement::External, 50.0f},
    {"couplerdetach:", Single, NoNumbers, Placement::External, 50.0f},
    {"brakehosedetach:", Single, NoNumbers, Placement::External, 50.0f},
    {"mainhosedetach:", Single, NoNumbers, Placement::External, 50.0f},
    {"controldetach:", Single, NoNumbers, Placement::External, 50.0f},
    {"gangwaydetach:", Single, NoNumbers, Placement::External, 50.0f},
    {"heatingdetach:", Single, NoNumbers, Placement::External, 50.0f},
    {"couplerstretch:", Single, NoNumbers, Placement::External, 50.0f},
    {"couplerstretch_loud:", Single, NoNumbers, Placement::External, 50.0f},
    {"bufferclamp:", Single, NoNumbers, Placement::External, 50.0f},
    {"bufferclamp_loud:", Single, NoNumbers, Placement::External, 50.0f},
    {"coupleradapterattach:", Single, NoNumbers, Placement::External, 50.0f},
    {"coupleradapterremove:", Single, NoNumbers, Placement::External, 50.0f},
    {"startjolt:", Single, NoNumbers, Placement::General, 50.0f},
};

// internaldata: (Train, the cab): all internal; the noises' tables end at
// Vmax.
const Key CabKeys[] = {
    {"ctrl:", Single, NoNumbers, Placement::Internal, CabControlsRange},
    {"ctrlscnd:", Single, NoNumbers, Placement::Internal, CabControlsRange},
    {"reverserkey:", Single, NoNumbers, Placement::Internal, CabControlsRange},
    {"buzzer:", Single, NoNumbers, Placement::Internal, CabControlsRange},
    {"buzzershp:", Single, NoNumbers, Placement::Internal, CabControlsRange},
    {"radiostop:", Single, NoNumbers, Placement::Internal, CabControlsRange},
    {"slipalarm:", Single, NoNumbers, Placement::Internal, CabControlsRange},
    {"distancecounter:", Single, NoNumbers, Placement::Internal, CabControlsRange},
    {"tachoclock:", Single, NoNumbers, Placement::Internal, CabControlsRange},
    {"switch:", Single, NoNumbers, Placement::Internal, CabControlsRange},
    {"pneumaticswitch:", Single, NoNumbers, Placement::Internal, CabControlsRange},
    {"airsound:", Single, Amplitude, Placement::Internal, CabControlsRange},
    {"airsound2:", Single, Amplitude, Placement::Internal, CabControlsRange},
    {"airsound3:", Single, Amplitude, Placement::Internal, CabControlsRange},
    {"airsound4:", Single, Amplitude, Placement::Internal, CabControlsRange},
    {"airsound5:", Single, Amplitude, Placement::Internal, CabControlsRange},
    {"localbrakesound:", Single, Amplitude, Placement::Internal, CabControlsRange},
    {"localbrakesound2:", Single, Amplitude, Placement::Internal, CabControlsRange},
    {"brakesound:", Single, Amplitude | Frequency, Placement::Internal, GlobalRange},
    {"fadesound:", Single, NoNumbers, Placement::Internal, CabControlsRange},
    {"runningnoise:", Single, Amplitude | Frequency, Placement::Internal, GlobalRange},
    {"resonancenoise:", Single, Amplitude | Frequency, Placement::Internal, GlobalRange},
    {"windsound:", Single, Amplitude | Frequency, Placement::Internal, GlobalRange},
    {"huntingnoise:", Single, Amplitude | Frequency, Placement::Internal, GlobalRange},
    {"rainsound:", Single, NoNumbers, Placement::Internal, GlobalRange},
};

template <size_t N>
const Key *keyOf(const Key (&keys)[N], const QByteArray &word) {
    for (const Key &key : keys)
        if (word == key.name)
            return &key;
    return nullptr;
}

QString nameOf(const QByteArray &key) {
    return QString::fromLatin1(key.endsWith(':') ? key.chopped(1) : key);
}

// Keys the pack misspells, told once where MaSzyna meets them: a key of
// internaldata:, a block's key.
constexpr const char *KeyTypo[2] = {"gengwaydetach:", "gangwaydetach:"};
constexpr const char *BlockTypo[2] = {"soudmain:", "soundmain:"};

QString typo(const char *const (&pair)[2]) {
    return QString("%1 is a typo of %2: not read").arg(pair[0], pair[1]);
}

class Reader {
public:
    Reader(Sounds &out, const QString &vehicleFolder, const QString &soundFolder, std::mt19937 *random)
        : out(out), vehicleFolder(vehicleFolder), soundFolder(soundFolder), random(random) {}

    // sound_source::deserialize: a { } block, or the key's one-line form.
    Definition sound(Words &words, const QByteArray &key, Form form, int numbers, Placement placement, float range,
                     int chunkRange = 100) {
        Definition d;
        d.key = nameOf(key);
        d.placement = placement;
        d.range = range;
        presets(d);
        const QByteArray first = words.take("\n\r\t ,;");
        bool variationGiven = false;
        if (first == "{") {
            d.block = true;
            while (mapping(words, d, variationGiven)) {
            }
            arrange(d, chunkRange);
        } else {
            if (form == Single) {
                d.main = sample(set(words, &first));
            } else {
                d.begin = sample(set(words, &first));
                d.main = sample(set(words));
                d.end = sample(set(words));
            }
            if (numbers & Range)
                d.range = number(words.take(",", false));
            if (numbers & Amplitude) {
                d.amplitudeFactor = number(words.take(",", false));
                d.amplitudeOffset = number(words.take(",", false));
            }
            if (numbers & Frequency) {
                d.frequencyFactor = number(words.take(",", false));
                d.frequencyOffset = number(words.take(",", false));
            }
        }
        if (!variationGiven)
            d.pitchVariation = random != nullptr ? std::uniform_real_distribution<float>(0.975f, 1.025f)(*random) : 1.0f;
        // The same file as main is no bookend.
        if (!d.begin.empty() && d.begin.path == d.main.path)
            d.begin = Sample();
        if (!d.end.empty() && d.end.path == d.main.path)
            d.end = Sample();
        return d;
    }

    void problem(const QString &text) {
        if (!out.problems.contains(text))
            out.problems << text;
    }

    // A word, or one of a [ set ]'s words.
    QByteArray set(Words &words, const QByteArray *first = nullptr) {
        QByteArray word = first != nullptr ? *first : words.take("\n\r\t ,;");
        word.replace('\\', '/');
        if (word != "[")
            return word;
        QList<QByteArray> items;
        for (;;) {
            const QByteArray item = set(words);
            if (item.isEmpty() || item == "]")
                break;
            items << item;
        }
        if (items.isEmpty())
            return QByteArray();
        return items[random != nullptr ? std::uniform_int_distribution<int>(0, int(items.size()) - 1)(*random) : 0];
    }

private:
    // Values set before reading (the transmission's and the release
    // hiss's).
    static void presets(Definition &d) {
        if (d.key == "transmission") {
            d.amplitudeFactor = 0.029f;
            d.amplitudeOffset = 0.1f;
            d.frequencyFactor = 0.005f;
            d.frequencyOffset = 1.0f;
        } else if (d.key == "unbrake") {
            d.amplitudeFactor = 20.0f;
        }
    }

    Sample sample(const QByteArray &word) {
        Sample s;
        if (word.isEmpty())
            return s;
        s.name = MaszynaText::decode(word);
        s.path = find(s.name, vehicleFolder, soundFolder);
        if (s.path.isEmpty())
            problem(s.name + ": found nowhere");
        return s;
    }

    // sound_source::deserialize_mapping: one key and its value; false at
    // the block's end. An unknown key's value is read as the next key.
    bool mapping(Words &words, Definition &d, bool &variationGiven) {
        const QByteArray key = words.take("\n\r\t ,;[]");
        if (key.isEmpty() || key == "}")
            return false;
        if (key == "soundmain:") {
            d.main = sample(set(words));
        } else if (key == "soundset:") {
            // One begin|main|end triple of the set.
            const QList<QByteArray> parts = set(words).split('|');
            d.begin = sample(parts.value(0));
            d.main = sample(parts.value(1));
            d.end = sample(parts.value(2));
        } else if (key == "soundbegin:") {
            d.begin = sample(set(words));
        } else if (key == "soundend:") {
            d.end = sample(set(words));
        } else if (key == "soundproofing:") {
            std::array<float, 6> factors;
            for (float &f : factors) {
                f = number(words.take("\n\r\t ,;[]", false));
                if (f != -1.0f)
                    f = std::sqrt(std::clamp(f, 0.0f, 1.0f));
            }
            d.soundproofing = factors;
        } else if (key.startsWith("sound")) {
            int threshold = 0;
            if (index(key, threshold)) {
                Chunk chunk;
                chunk.threshold = threshold;
                chunk.sample = sample(set(words));
                d.chunks.push_back(chunk);
            }
        } else if (key == "pitchvariation:") {
            const float v = std::clamp(number(words.take("\n\r\t ,;", false)), 0.0f, 1.0f) * 50.0f;
            d.pitchVariation = v == 0.0f || random == nullptr
                                       ? 1.0f
                                       : 0.01f * std::uniform_real_distribution<float>(100.0f - v, 100.0f + v)(*random);
            variationGiven = true;
        } else if (key == "startoffset:") {
            d.startOffset = std::clamp(number(words.take("\n\r\t ,;", false)), 0.0f, 1.0f);
        } else if (key.startsWith("pitch")) {
            int threshold = 0;
            if (index(key, threshold)) {
                const float pitch = number(words.take("\n\r\t ,;", false));
                // Only an entry read before it.
                for (Chunk &chunk : d.chunks)
                    if (chunk.threshold == threshold) {
                        chunk.pitch = pitch > 0.0f ? pitch : 1.0f;
                        break;
                    }
            }
        } else if (key == "crossfade:") {
            d.crossfade = std::clamp(int(std::strtol(words.take("\n\r\t ,;", false).constData(), nullptr, 10)), 0, 100);
        } else if (key == "placement:") {
            const QByteArray value = words.take("\n\r\t ,;");
            if (value == "internal")
                d.placement = Placement::Internal;
            else if (value == "engine")
                d.placement = Placement::Engine;
            else if (value == "external")
                d.placement = Placement::External;
            else if (value == "custom")
                d.placement = Placement::Custom;
            else if (value == "general")
                d.placement = Placement::General;
            else
                problem("placement: " + MaszynaText::decode(value) + " unknown");
        } else if (key == "offset:") {
            for (float &v : d.offset)
                v = number(words.take("\n\r\t ,;[]", false));
        } else if (key == "frequencyfactor:") {
            d.frequencyFactor = number(words.take("\n\r\t ,;", false));
        } else if (key == "frequencyoffset:") {
            d.frequencyOffset = number(words.take("\n\r\t ,;", false));
        } else if (key == "amplitudefactor:") {
            d.amplitudeFactor = number(words.take("\n\r\t ,;", false));
        } else if (key == "amplitudeoffset:") {
            d.amplitudeOffset = number(words.take("\n\r\t ,;", false));
        } else if (key == "range:") {
            d.range = number(words.take("\n\r\t ,;", false));
        } else if (key == BlockTypo[0]) {
            problem(typo(BlockTypo));
        }
        return true;
    }

    // The number in sound<N>: or pitch<N>: from its first digit or -.
    static bool index(const QByteArray &key, int &value) {
        const qsizetype from = std::find_if(key.begin(), key.end(), [](char c) { return c == '-' || (c >= '0' && c <= '9'); })
                               - key.begin();
        if (from >= key.size())
            return false;
        qsizetype to = from + 1;
        while (to < key.size() && (key[to] == '-' || (key[to] >= '0' && key[to] <= '9')))
            ++to;
        bool ok = false;
        value = key.mid(from, to - from).toInt(&ok);
        return ok;
    }

    // The table sorted, each entry's range with the crossfades.
    static void arrange(Definition &d, int chunkRange) {
        if (d.chunks.empty())
            return;
        std::stable_sort(d.chunks.begin(), d.chunks.end(),
                         [](const Chunk &a, const Chunk &b) { return a.threshold < b.threshold; });
        for (size_t i = 0; i + 1 < d.chunks.size(); ++i)
            d.chunks[i].fadeOut = float(d.chunks[i + 1].threshold);
        d.chunks.front().fadeIn = float(std::max(0, d.chunks.front().threshold));
        for (size_t i = 1; i < d.chunks.size(); ++i)
            d.chunks[i].fadeIn = float(d.chunks[i].threshold)
                                 - 0.01f * float(d.crossfade) * float(d.chunks[i].threshold - d.chunks[i - 1].threshold);
        d.chunks.back().fadeOut = float(std::max(chunkRange, d.chunks.back().threshold));
    }

    Sounds &out;
    QString vehicleFolder, soundFolder;
    std::mt19937 *random;
};

void keep(std::vector<Definition> &list, Definition d) {
    for (Definition &old : list)
        if (old.key == d.key) {
            old = std::move(d);
            return;
        }
    list.push_back(std::move(d));
}

// A list of numbers (and sides) to its end.
void locations(Words &words, Locations &out) {
    for (;;) {
        const QByteArray word = words.take("\n\r\t ,;");
        if (word.isEmpty() || word == "endlocations")
            return;
        const bool doors = word == "doors:", motors = word == "tractionmotors:", bogies = word == "bogies:";
        if (!doors && !motors && !bogies)
            continue;
        for (;;) {
            const QByteArray at = words.take("\n\r\t ,;");
            if (at.isEmpty() || at == "end")
                break;
            const float z = -number(at);
            if (doors) {
                const QByteArray sides = words.take("\n\r\t ,;");
                out.doors.push_back({z, sides == "both" || sides == "left", sides == "both" || sides == "right"});
            } else {
                (motors ? out.motors : out.bogies).push_back(z);
            }
        }
    }
}

// DynObj's rescaling after reading a key (LoadMMediaFile).
void rescale(Definition &d, const Physics &physics, std::mt19937 *random) {
    const float motorDivisor = float(physics.nmax * 60.0 + physics.power * 3.0);
    if (d.key == "engine" && !physics.diesel && motorDivisor != 0.0f) {
        d.amplitudeFactor /= motorDivisor;
    } else if (d.key == "tractionmotor" && motorDivisor != 0.0f) {
        d.amplitudeFactor /= motorDivisor;
    } else if (d.key == "motorblower") {
        const bool bookends = !d.begin.empty() && !d.end.empty();
        const float divisor = float(physics.blowerSpeed > 0 ? physics.blowerSpeed * physics.nmax * 60.0 + physics.power * 3.0
                                    : bookends              ? 1.0
                                                            : -physics.blowerSpeed);
        if (divisor != 0.0f) {
            d.amplitudeFactor /= divisor;
            d.frequencyFactor /= divisor;
        }
    } else if (d.key == "ventilator" && physics.rventnmax != 0.0) {
        d.amplitudeFactor /= float(physics.rventnmax);
        d.frequencyFactor /= float(physics.rventnmax);
    } else if (d.key == "brakesound") {
        d.frequencyFactor /= float(1.0 + physics.vmax);
    } else if (d.key == "brake") {
        // An old file's activation threshold in place of the factor.
        if (d.amplitudeFactor > 10.0f) {
            d.amplitudeFactor = 1.0f;
            d.amplitudeOffset = 0.0f;
        }
        d.amplitudeOffset *= random != nullptr ? (105.0f - std::uniform_real_distribution<float>(0.0f, 10.0f)(*random)) / 100.0f
                                               : 1.0f;
    } else if (d.key == "outernoise") {
        d.amplitudeFactor /= float(1.0 + physics.vmax);
        d.frequencyFactor /= float(1.0 + physics.vmax);
    } else if (d.key == "engageslippery") {
        d.frequencyFactor /= float(1.0 + physics.nmax);
    } else if (d.key == "slipperysound") {
        d.amplitudeFactor /= float(1.0 + physics.vmax);
    }
}

// The .mmd's top level (DynObj): models: to endmodels passed over,
// sounds: to endsounds, locations: to endlocations, internaldata: to the
// end.
void readVehicle(Words &words, Reader &reader, Sounds &out, const Physics &physics, std::mt19937 *random) {
    const bool powered = physics.power > 0.0;
    for (;;) {
        const QByteArray word = words.take("\n\r\t ,;");
        if (word.isEmpty())
            return;
        if (word == "models:") {
            QByteArray w;
            do
                w = words.take("\n\r\t ,;");
            while (!w.isEmpty() && w != "endmodels");
        } else if (word == "locations:") {
            locations(words, out.locations);
        } else if (word == "sounds:") {
            for (;;) {
                const QByteArray key = words.take("\n\r\t ,;");
                if (key.isEmpty() || key == "endsounds")
                    break;
                if (key == "wheel_clatter:") {
                    out.clatterRange = number(words.take("\n\r\t ,;", false));
                    out.clatter.clear();
                    for (;;) {
                        const QByteArray at = words.take("\n\r\t ,;");
                        if (at.isEmpty() || at == "end")
                            break;
                        out.clatter.push_back({-number(at), reader.sound(words, "clatter:", Single, NoNumbers,
                                                                         Placement::External, out.clatterRange)});
                    }
                    std::stable_sort(out.clatter.begin(), out.clatter.end(),
                                     [](const auto &a, const auto &b) { return a.first > b.first; });
                    continue;
                }
                if (key == "announcements:") {
                    // Its own block, not read (passenger announcements are
                    // not in this task): to its }.
                    QByteArray w = words.take("\n\r\t ,;");
                    if (w == "{")
                        for (int depth = 1; depth > 0 && !(w = words.take("\n\r\t ,;")).isEmpty();)
                            depth += w == "{" ? 1 : w == "}" ? -1 : 0;
                    continue;
                }
                const Key *k = keyOf(VehicleKeys, key);
                if (k == nullptr)
                    continue;
                if (!powered && (key == "engine:" || key == "tractionmotor:" || key == "tractionacmotor:"))
                    continue;
                Definition d = reader.sound(words, key, k->form, k->numbers, k->placement, k->range,
                                            key == "outernoise:" ? int(physics.vmax) : 100);
                rescale(d, physics, random);
                keep(out.vehicle, std::move(d));
            }
        } else if (word == "internaldata:") {
            for (;;) {
                const QByteArray key = words.take("\n\r\t ,;");
                if (key.isEmpty())
                    return;
                if (key == "soundproofing:") {
                    for (auto &row : out.soundproofing)
                        for (float &f : row) {
                            const float v = number(words.take("\n\r\t ,;", false));
                            if (v != -1.0f)
                                f = std::sqrt(std::clamp(v, 0.0f, 1.0f));
                        }
                    continue;
                }
                if (key == "huntingspring:") {
                    for (float &f : out.huntingSpring)
                        f = number(words.take("\n\r\t ,;", false));
                    continue;
                }
                const Key *k = keyOf(VehicleInternalKeys, key);
                if (k == nullptr) {
                    if (key == KeyTypo[0])
                        reader.problem(typo(KeyTypo));
                    continue;
                }
                Definition d = reader.sound(words, key, k->form, k->numbers, k->placement, k->range);
                rescale(d, physics, random);
                keep(out.vehicle, std::move(d));
            }
        }
    }
}

// Train's pass: to internaldata:, then its keys to the end.
void readCab(Words &words, Reader &reader, Sounds &out, const Physics &physics) {
    QByteArray word;
    do
        word = words.take("\n\r\t ,;");
    while (!word.isEmpty() && word != "internaldata:");
    if (word.isEmpty())
        return;
    for (;;) {
        const QByteArray key = words.take("\n\r\t ,;");
        if (key.isEmpty())
            break;
        const Key *k = keyOf(CabKeys, key);
        if (k == nullptr)
            continue;
        const bool noise = key == "runningnoise:" || key == "resonancenoise:" || key == "windsound:" || key == "huntingnoise:";
        Definition d = reader.sound(words, key, k->form, k->numbers, k->placement, k->range,
                                    noise ? int(physics.vmax) : 100);
        if (noise || key == "brakesound:")
            d.frequencyFactor /= float(1.0 + physics.vmax);
        keep(out.cab, std::move(d));
    }
    // MaSzyna's defaults for keys not given: the rain's sample, the local
    // brake hisses as the brake valve's.
    if (out.findCab(u"rainsound") == nullptr) {
        Words fallback(QByteArray("rainsound_default"));
        keep(out.cab, reader.sound(fallback, "rainsound:", Single, NoNumbers, Placement::Internal, GlobalRange));
    }
    for (const auto &pair : {std::pair{u"localbrakesound", u"airsound"}, std::pair{u"localbrakesound2", u"airsound2"}})
        if (out.findCab(pair.first) == nullptr)
            if (const Definition *given = out.findCab(pair.second)) {
                Definition copy = *given;
                copy.key = QString::fromUtf16(pair.first);
                keep(out.cab, std::move(copy));
            }
}

}

bool Definition::hasSample() const {
    if (!begin.empty() || !main.empty() || !end.empty())
        return true;
    return std::any_of(chunks.begin(), chunks.end(), [](const Chunk &c) { return !c.sample.empty(); });
}

const Definition *Sounds::find(QStringView key) const {
    for (const Definition &d : vehicle)
        if (d.key == key)
            return &d;
    return nullptr;
}

const Definition *Sounds::findCab(QStringView key) const {
    for (const Definition &d : cab)
        if (d.key == key)
            return &d;
    return nullptr;
}

Physics Physics::of(const MaszynaFiz &fiz) {
    Physics p;
    // MaszynaFiz gives these two in SI.
    p.vmax = fiz.number(u"Param", u"Vmax") * 3.6;
    p.power = fiz.number(u"Param", u"PWR") / 1000.0;
    const QString engine = fiz.text(u"Engine", u"EngineType").toLower();
    // MaSzyna reads DumbDE as DieselElectric.
    p.diesel = engine == QLatin1String("dieselengine") || engine == QLatin1String("dieselelectric")
               || engine == QLatin1String("dumbde");
    if (engine == QLatin1String("electricseriesmotor") || engine == QLatin1String("dieselengine")
        || engine == QLatin1String("wheelsdriven") || engine == QLatin1String("dumb"))
        p.nmax = fiz.number(u"Engine", u"nmax") / 60.0;
    const QString vent = fiz.text(u"RList", u"RVent").toLower();
    if ((vent == QLatin1String("yes") || vent == QLatin1String("automatic")) && fiz.number(u"RList", u"RVentnmax") != 0.0)
        p.rventnmax = fiz.number(u"RList", u"RVentnmax") / 60.0;
    p.blowerSpeed = fiz.number(u"Engine", u"MotorBlowersSpeed");
    return p;
}

std::array<std::array<float, 6>, 5> MaszynaSound::defaultSoundproofing() {
    return {{
            {None, Strong, None, Some, Strong, Strong},                         // internal
            {Strong, None, Strong, Some, Some, Some},                           // engine
            {Strong, Strong, Strong, Some, Some, None},                         // external
            {VeryStrong, VeryStrong, VeryStrong, Strong, Strong, None},         // external ambient
            {None, None, None, None, None, None},                               // custom
    }};
}

QString MaszynaSound::find(const QString &name, const QString &vehicleFolder, const QString &soundFolder) {
    QString stem = name.toLower();
    stem.replace(QLatin1Char('\\'), QLatin1Char('/'));
    // The extension as written goes, unless its dot is part of "..".
    const qsizetype dot = stem.lastIndexOf(QLatin1Char('.'));
    if (dot > 0 && stem[dot - 1] != QLatin1Char('.') && !stem.mid(dot).contains(QLatin1Char('/')))
        stem.truncate(dot);
    QStringList places;
    if (!vehicleFolder.isEmpty())
        places << vehicleFolder + QLatin1Char('/') + stem;
    if (stem.contains(QLatin1Char('/')) && soundFolder.endsWith(QLatin1String("/sounds")))
        places << QDir::cleanPath(soundFolder + QLatin1String("/..")) + QLatin1Char('/') + stem;
    places << soundFolder + QLatin1Char('/') + stem;
    for (const QString &place : places)
        for (const char *extension : {".ogg", ".flac", ".wav"}) {
            const QString path = place + QLatin1String(extension);
            if (QFileInfo(path).isFile())
                return path;
        }
    return QString();
}

Sounds MaszynaSound::read(const QString &folder, const QString &type, const QString &soundFolder,
                          const Physics &physics, std::mt19937 *random) {
    Sounds out;
    out.soundproofing = defaultSoundproofing();
    out.soundFolder = soundFolder;
    Reader reader(out, folder, soundFolder, random);
    // DynObj opens the .mmd through an include of it with the car's name,
    // its type and its skin; TSRE keeps (p1) and (p3) as written, as
    // MaszynaVehicle does.
    {
        MaszynaText text(folder, type + ".mmd", {QStringLiteral("(p1)"), type, QStringLiteral("(p3)")});
        text.keepBrackets = true;
        if (random != nullptr)
            text.chooser = [random](int n) { return std::uniform_int_distribution<int>(0, n - 1)(*random); };
        Words words(text);
        readVehicle(words, reader, out, physics, random);
        out.files = text.files();
        for (const QString &missing : text.problems())
            reader.problem(missing + ": include not found");
    }
    // Train reads the file again for the cab's keys; its sounds are not
    // looked for in the vehicle's folder.
    {
        MaszynaText text(folder, type + ".mmd");
        text.keepBrackets = true;
        if (random != nullptr)
            text.chooser = [random](int n) { return std::uniform_int_distribution<int>(0, n - 1)(*random); };
        Words words(text);
        Reader cabReader(out, QString(), soundFolder, random);
        readCab(words, cabReader, out, physics);
    }
    return out;
}

Definition MaszynaSound::readValue(const QString &key, const QString &text, const QString &soundFolder,
                                   std::mt19937 *random) {
    Sounds out;
    Reader reader(out, QString(), soundFolder, random);
    Words words(text.toLatin1());
    Definition d = reader.sound(words, key.toLatin1() + ':', Single, NoNumbers, Placement::Internal, CabControlsRange);
    return d;
}
