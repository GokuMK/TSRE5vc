/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tests/MaszynaSoundTestSuite.h>
#include <tsre/tests/TokenTestSupport.h>
#include <tsre/maszyna/MaszynaCab.h>
#include <tsre/maszyna/MaszynaCabControls.h>
#include <tsre/maszyna/MaszynaSound.h>
#include <tsre/maszyna/MaszynaSounds.h>
#include <tsre/maszyna/MaszynaText.h>
#include <tsre/sound/SoundEngine.h>
#include <tsre/sound/SoundSource.h>
#include <tsre/sound/SoundSystem.h>
#include <tsre/maszyna/MaszynaVehicle.h>
#include <tsre/Game.h>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QTemporaryDir>
#include <cmath>

using namespace MaszynaSound;

namespace {

void writeFile(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).path());
    QFile out(path);
    if (out.open(QIODevice::WriteOnly))
        out.write(bytes);
}

bool near(float a, float b) {
    return std::fabs(a - b) <= 1e-5f * std::max(1.0f, std::fabs(b));
}

QString fileName(const Sample &s) {
    return QFileInfo(s.path).fileName();
}

QString keyValue(const MaszynaCab::Control &c, const char *key) {
    for (const auto &k : c.keys)
        if (k.first == QLatin1String(key))
            return k.second;
    return QString();
}

const MaszynaCab::Control *control(const MaszynaCab::End &end, const char *label) {
    for (const MaszynaCab::Control &c : end.controls)
        if (c.label == QLatin1String(label))
            return &c;
    return nullptr;
}

// A mono 16-bit wave of `seconds` at 8 kHz.
void writeWave(const QString &path, double seconds) {
    const int frames = int(seconds * 8000);
    QByteArray data;
    for (int i = 0; i < frames; ++i) {
        const int16_t v = int16_t(4000.0 * std::sin(2.0 * M_PI * 440.0 * i / 8000.0));
        data.append(char(v & 0xFF));
        data.append(char((v >> 8) & 0xFF));
    }
    const auto put32 = [](QByteArray &b, uint32_t v) {
        for (int i = 0; i < 4; ++i)
            b.append(char((v >> (8 * i)) & 0xFF));
    };
    QByteArray b("RIFF", 4);
    put32(b, uint32_t(36 + data.size()));
    b.append("WAVEfmt ", 8);
    put32(b, 16);
    b.append(QByteArray::fromHex("01000100"));
    put32(b, 8000);
    put32(b, 16000);
    b.append(QByteArray::fromHex("02001000"));
    b.append("data", 4);
    put32(b, uint32_t(data.size()));
    b.append(data);
    writeFile(path, b);
}

// Frames of the emitter's sound (60 a second), its voices updated.
void run(MaszynaEmitter &e, double seconds) {
    std::vector<float> out(800 * 2);
    for (double t = 0.0; t < seconds; t += 1.0 / 60.0) {
        e.update();
        SoundEngine::get().render(out.data(), 800);
    }
}

MaszynaEmitter::Hearing at(float distance, float soundproofing = 1.0f) {
    MaszynaEmitter::Hearing h;
    h.position[0] = distance;
    h.distance = distance;
    h.soundproofing = soundproofing;
    return h;
}

}

int TsreTests::runMmdSoundSuite(bool verbose) {
    TokenTest::Suite test{"[tests:mmd-sound]", verbose};
    QTemporaryDir dir;
    const QString sounds = dir.filePath("sounds");
    const QString vehicle = dir.filePath("dynamic/test/veh");
    for (const char *name : {"vent", "a", "b", "c", "motor0", "motor1", "motor2", "silence", "h1s", "h1m", "h1e", "h2s",
                             "h2m", "h2e", "x", "r0", "r1", "g", "ca1", "ca2", "slip", "cabbrake", "rn20", "bz1", "bz2",
                             "c0", "c1", "inc", "dec", "s1", "s2", "rainsound_default", "tm", "one", "two", "three",
                             "uno", "dos", "tres", "[1007]brack"})
        writeFile(sounds + "/" + name + ".ogg", QByteArray());
    // The vehicle's own: found first for its sounds, never for the cab's.
    writeFile(vehicle + "/vent.ogg", QByteArray());
    writeFile(vehicle + "/hiss.ogg", QByteArray());
    writeFile(vehicle + "/veh.mmd", QByteArray(R"(// a test vehicle
models: test.e3d brake: notread.wav endmodels
locations:
 doors: 4.5 both -4.5 left end
 tractionmotors: 2 -2 end
 bogies: 5 -5 end
endlocations
sounds:
 ventilator: vent.wav 150.0 2.2 0.2 0.7 0.4
 sand: a.wav b.wav c.wav 15
 tractionmotor: { amplitudeoffset: 0.45 amplitudefactor: 0.33 frequencyfactor: 0.04 range: 120 crossfade: 50
   pitch10: 9 sound10: motor1.wav sound0: silence.wav pitch0: 1 sound-2: motor0.wav sound65: motor2.wav pitch65: 60
   pitch10: 10 }
 horn1: { soundset: [ h1s.wav|h1m.wav|h1e.wav, h2s.wav|h2m.wav|h2e.wav ] range: 900 }
 compressor: { soundbegin: a.wav soundmain: b.wav soundend: c.wav placement: custom offset: [0.40, 2.90, 4.05]
   soundproofing: [ 0.25 1 -1 0.04 0 2 ] pitchvariation: 0 }
 curve: [ a.wav b.wav ] 222
 derail: d.wav 300
 releaser: [1007]brack.wav b.wav b.wav 20
endsounds
internaldata:
 relay: { soundproofing: [ 0.2 1.0 0.2 0.05 0.0025 0.0025 ] sound0: r0.wav sound1: r1.wav }
 gengwaydetach: g.wav
 couplerattach: [ ca1.wav ca2.wav ]
 slipperysound: slip.wav 2.0 0.2
 soundproofing:
 -1 -1 -1 -1 -1 -1
 -1 -1 -1 -1 -1 -1
 -1 -1 -1 -1 -1 -1
 -1 -1 -1 -1 -1 -1
 0.36 1.0 0.36 0.64 0.64 0.64
 brakesound: cabbrake.wav 0.2 0.0 1.5 0.2
 runningnoise: { range: -1 amplitudefactor: 0.4 crossfade: 95 sound0: silence.wav sound20: rn20.wav pitch20: 40
   sound190: rn20.wav }
 buzzer: [ bz1.wav bz2.wav ]
 airsound: hiss.wav 0.7 -0.01
cab1definition:
-1.2 1.5 7.35 1.2 1.5 8.5
driver1pos:
0.92 3.35 7.3
mainctrl: { nastawnik rot -0.0197 0.0 0.1 sound0: c0.wav sound1: c1.wav }
dirkey: { nawrotnik rot 0.08 0.01 0.1 soundinc: inc.wav sounddec: dec.wav }
sand_bt: { piasek mov -0.007 0 0.2 soundinc: [ s1.wav s2.wav ] sounddec: { soundmain: dec.wav range: 5 } }
)"));
    Physics physics;
    physics.vmax = 120;
    physics.power = 1000;
    physics.nmax = 10;
    physics.rventnmax = 2;
    const Sounds s = read(vehicle, "veh", sounds, physics);

    const Definition *vent = s.find(u"ventilator");
    test.check(vent && !vent->block && vent->main.path == vehicle + "/vent.ogg" && near(vent->range, 150)
                       && near(vent->amplitudeFactor, 1.1f) && near(vent->amplitudeOffset, 0.2f)
                       && near(vent->frequencyFactor, 0.35f) && near(vent->frequencyOffset, 0.4f)
                       && vent->placement == Placement::Engine,
               "a one-line entry takes its key's numbers; the ventilator's factors over RVentnmax; its own folder first");
    const Definition *sand = s.find(u"sand");
    test.check(sand && fileName(sand->begin) == "a.ogg" && fileName(sand->main) == "b.ogg" && fileName(sand->end) == "c.ogg"
                       && near(sand->range, 15) && sand->placement == Placement::External,
               "a three-part entry: begin, main, end and its range");
    const Definition *motor = s.find(u"tractionmotor");
    const bool motorTable = motor && motor->combined() && motor->chunks.size() == 4;
    test.check(motorTable && motor->chunks[0].threshold == -2 && motor->chunks[1].threshold == 0
                       && motor->chunks[2].threshold == 10 && motor->chunks[3].threshold == 65,
               "a table's entries sorted by their values");
    test.check(motorTable && near(motor->chunks[2].pitch, 10) && near(motor->chunks[1].pitch, 1) && near(motor->chunks[3].pitch, 60)
                       && near(motor->chunks[0].pitch, 1),
               "pitch<N> counts after its sound<N> only (pitch10: 9 before it dropped)");
    test.check(motorTable && near(motor->chunks[0].fadeIn, 0) && near(motor->chunks[1].fadeIn, -1)
                       && near(motor->chunks[2].fadeIn, 5) && near(motor->chunks[3].fadeIn, 37.5f)
                       && near(motor->chunks[0].fadeOut, 0) && near(motor->chunks[2].fadeOut, 65)
                       && near(motor->chunks[3].fadeOut, 100),
               "crossfades: each entry from its value back half its span (crossfade 50), the last to 100");
    test.check(motor && near(motor->amplitudeFactor, 0.33f / 3600) && near(motor->amplitudeOffset, 0.45f)
                       && near(motor->frequencyFactor, 0.04f) && near(motor->range, 120) && motor->block,
               "the motor's amplitude factor over nmax · 60 + Power · 3");
    const Definition *horn = s.find(u"horn1");
    test.check(horn && fileName(horn->begin) == "h1s.ogg" && fileName(horn->main) == "h1m.ogg"
                       && fileName(horn->end) == "h1e.ogg" && near(horn->range, 900),
               "a soundset's triple, its first without a car's random");
    const Definition *compressor = s.find(u"compressor");
    test.check(compressor && compressor->placement == Placement::Custom && near(compressor->offset[0], 0.4f)
                       && near(compressor->offset[1], 2.9f) && near(compressor->offset[2], 4.05f)
                       && compressor->soundproofing && near((*compressor->soundproofing)[0], 0.5f)
                       && near((*compressor->soundproofing)[1], 1) && near((*compressor->soundproofing)[2], -1)
                       && near((*compressor->soundproofing)[3], 0.2f) && near((*compressor->soundproofing)[4], 0)
                       && near((*compressor->soundproofing)[5], 1) && compressor->pitchVariation == 1.0f,
               "placement, offset [ x, y, z ], soundproofing square-rooted (-1 kept, clamped), pitchvariation 0");
    const Definition *curve = s.find(u"curve");
    test.check(curve && fileName(curve->main) == "a.ogg" && near(curve->range, 222), "a [ set ] in a one-line entry");
    const Definition *derail = s.find(u"derail");
    test.check(derail && derail->main.name == "d.wav" && derail->main.path.isEmpty() && near(derail->range, 300)
                       && s.problems.contains("d.wav: found nowhere"),
               "a name found nowhere is kept, without a file, and told");
    const Definition *releaser = s.find(u"releaser");
    test.check(releaser && fileName(releaser->begin) == "[1007]brack.ogg" && releaser->end.empty()
                       && fileName(releaser->main) == "b.ogg",
               "a name with brackets is a name; an end the same as main is none");
    test.check(s.find(u"brake") == nullptr, "models: is passed over to endmodels");
    test.check(s.locations.doors.size() == 2 && near(s.locations.doors[0].z, -4.5f) && s.locations.doors[0].left
                       && s.locations.doors[0].right && near(s.locations.doors[1].z, 4.5f) && s.locations.doors[1].left
                       && !s.locations.doors[1].right && s.locations.motors == std::vector<float>({-2, 2})
                       && s.locations.bogies == std::vector<float>({-5, 5}),
               "locations: doors with their sides, motors and bogies, +z front");

    const Definition *relay = s.find(u"relay");
    test.check(relay && relay->chunks.size() == 2 && relay->soundproofing && near((*relay->soundproofing)[4], 0.05f),
               "internaldata: the vehicle's keys (DynObj)");
    test.check(s.problems.contains("gengwaydetach: is a typo of gangwaydetach:: not read") && s.find(u"gangwaydetach") == nullptr,
               "a known typo is told and not read");
    const Definition *coupler = s.find(u"couplerattach");
    test.check(coupler && fileName(coupler->main) == "ca1.ogg", "a set's first without a car's random");
    const Definition *slip = s.find(u"slipperysound");
    test.check(slip && near(slip->amplitudeFactor, 2.0f / 121) && near(slip->amplitudeOffset, 0.2f)
                       && near(slip->range, 100),
               "the slip sound's factor over 1 + Vmax");
    const auto defaults = defaultSoundproofing();
    test.check(near(s.soundproofing[4][0], 0.6f) && near(s.soundproofing[4][3], 0.8f) && near(s.soundproofing[0][1], defaults[0][1])
                       && near(s.soundproofing[2][5], 1.0f),
               "the vehicle's soundproofing: -1 keeps MaSzyna's, others square-rooted");

    const Definition *cabBrake = s.findCab(u"brakesound");
    test.check(cabBrake && near(cabBrake->amplitudeFactor, 0.2f) && near(cabBrake->frequencyFactor, 1.5f / 121)
                       && near(cabBrake->frequencyOffset, 0.2f) && near(cabBrake->range, -1)
                       && cabBrake->placement == Placement::Internal && s.find(u"brakesound") == nullptr,
               "the cab's brakesound: Train's key in internaldata:, its frequency over 1 + Vmax");
    const Definition *noise = s.findCab(u"runningnoise");
    test.check(noise && noise->chunks.size() == 3 && near(noise->chunks[2].fadeOut, 190) && near(noise->chunks[1].pitch, 40)
                       && near(noise->frequencyFactor, 1.0f / 121) && near(noise->range, -1),
               "the cab's running noise: its table to its last value (above Vmax)");
    const Definition *buzzer = s.findCab(u"buzzer");
    test.check(buzzer && fileName(buzzer->main) == "bz1.ogg", "the cab's buzzer from its set");
    const Definition *hiss = s.findCab(u"airsound");
    test.check(hiss && hiss->main.path.isEmpty() && near(hiss->amplitudeFactor, 0.7f) && near(hiss->amplitudeOffset, -0.01f),
               "a cab sound is not looked for in the vehicle's folder");
    const Definition *local = s.findCab(u"localbrakesound");
    test.check(local && local->main.name == "hiss.wav" && s.findCab(u"localbrakesound2") == nullptr,
               "the local brake hiss falls back to the brake valve's");
    const Definition *rain = s.findCab(u"rainsound");
    test.check(rain && fileName(rain->main) == "rainsound_default.ogg", "the rain's default sample");

    const MaszynaCab cab = MaszynaCab::read(vehicle, "veh.mmd");
    const MaszynaCab::Control *mainctrl = control(cab.ends[1], "mainctrl");
    const MaszynaCab::Control *sandBt = control(cab.ends[1], "sand_bt");
    test.check(mainctrl && keyValue(*mainctrl, "sound1") == "c1.wav" && sandBt
                       && keyValue(*sandBt, "soundinc") == "[ s1.wav s2.wav ]"
                       && keyValue(*sandBt, "sounddec") == "{ soundmain: dec.wav range: 5 }",
               "the cab keeps a control's sound values whole: a name, a set, a block");
    if (sandBt) {
        const Definition inc = readValue("soundinc", keyValue(*sandBt, "soundinc"), sounds);
        const Definition dec = readValue("sounddec", keyValue(*sandBt, "sounddec"), sounds);
        test.check(fileName(inc.main) == "s1.ogg" && inc.placement == Placement::Internal && near(inc.range, 7.5f)
                           && fileName(dec.main) == "dec.ogg" && near(dec.range, 5) && dec.block,
                   "a control's value read as a cab sound: a set, a block with its range");
    }

    // A car's random: the same picks for the same seed, both triples
    // among a few cars.
    std::mt19937 one(7), again(7);
    const Sounds first = read(vehicle, "veh", sounds, physics, &one);
    const Sounds second = read(vehicle, "veh", sounds, physics, &again);
    test.check(first.find(u"horn1") && second.find(u"horn1") && first.find(u"horn1")->main.path == second.find(u"horn1")->main.path
                       && first.find(u"couplerattach")->main.path == second.find(u"couplerattach")->main.path
                       && near(first.find(u"curve")->pitchVariation, second.find(u"curve")->pitchVariation),
               "a car's seed gives it the same picks at every read");
    QSet<QString> horns;
    bool variationsInRange = true;
    for (unsigned seed = 1; seed <= 20; ++seed) {
        std::mt19937 random(seed);
        const Sounds car = read(vehicle, "veh", sounds, physics, &random);
        horns.insert(fileName(car.find(u"horn1")->main));
        const float v = car.find(u"curve")->pitchVariation;
        variationsInRange = variationsInRange && v >= 0.975f && v <= 1.025f;
    }
    test.check(horns == QSet<QString>({"h1m.ogg", "h2m.ogg"}) && variationsInRange,
               "cars pick among a set's alternatives; an unset pitch variation is 0.975 to 1.025");

    // Includes with parameters, a random include, an unpowered vehicle.
    const QString wrapper = dir.filePath("dynamic/test/wrap");
    writeFile(wrapper + "/wrap.mmd", "sounds:\n include body.inc tm.wav end\n include [ first.inc second.inc ] end\nendsounds\n");
    writeFile(wrapper + "/body.inc", "engine: (p1) 80 1 0 1 0\n");
    writeFile(wrapper + "/first.inc", "horn2: one.wav two.wav three.wav 500\n");
    writeFile(wrapper + "/second.inc", "horn2: uno.wav dos.wav tres.wav 600\n");
    const Sounds wrapped = read(wrapper, "wrap", sounds, physics);
    const Definition *engine = wrapped.find(u"engine");
    test.check(engine && fileName(engine->main) == "tm.ogg" && near(engine->range, 80)
                       && near(engine->amplitudeFactor, 1.0f / 3600) && wrapped.find(u"horn2")
                       && fileName(wrapped.find(u"horn2")->main) == "two.ogg",
               "an include's (p1); a random include's first; the engine's factor rescaled when not diesel");
    QSet<QString> includes;
    for (unsigned seed = 1; seed <= 20; ++seed) {
        std::mt19937 random(seed);
        const Sounds car = read(wrapper, "wrap", sounds, physics, &random);
        if (const Definition *h = car.find(u"horn2"))
            includes.insert(fileName(h->main));
    }
    test.check(includes == QSet<QString>({"two.ogg", "dos.ogg"}), "cars pick among a random include's files");
    Physics unpowered = physics;
    unpowered.power = 0;
    test.check(read(wrapper, "wrap", sounds, unpowered).find(u"engine") == nullptr, "no engine: on an unpowered vehicle");
    Physics diesel = physics;
    diesel.diesel = true;
    test.check(near(read(wrapper, "wrap", sounds, diesel).find(u"engine")->amplitudeFactor, 1.0f),
               "a diesel engine's factor stays");

    // The quirks: a } stuck to a value leaves its block open to the next
    // }, swallowing what follows for DynObj (Train still reads its keys);
    // a one-line entry short of numbers eats the next key.
    const QString quirks = dir.filePath("dynamic/test/quirks");
    writeFile(quirks + "/quirks.mmd", "sounds:\n brake: { soundmain: x.wav range: 200}\n curve: b.wav\nendsounds\n"
                                      "internaldata:\n couplerattach: a.wav\n buzzer: bz1.wav\n"
                                      " airsound: x.wav airsound2: c.wav 0.05 -0.01 airsound3: x.wav 0.15 -0.01\n");
    const Sounds q = read(quirks, "quirks", sounds, physics);
    test.check(q.find(u"brake") && near(q.find(u"brake")->range, 200) && q.find(u"curve") == nullptr
                       && q.find(u"couplerattach") == nullptr && q.findCab(u"buzzer") != nullptr,
               "an unclosed block swallows the rest for the vehicle; the cab's pass still reads its keys");
    const Definition *short1 = q.findCab(u"airsound");
    test.check(short1 && near(short1->amplitudeFactor, 0) && near(short1->amplitudeOffset, 0) && q.findCab(u"airsound2") == nullptr
                       && q.findCab(u"airsound3") && near(q.findCab(u"airsound3")->amplitudeFactor, 0.15f),
               "an entry short of numbers eats the next key and its file as numbers (0)");
    return test.finish();
}

int TsreTests::runMmdSoundCorpusSuite(bool verbose) {
    TokenTest::Suite test{"[tests:mmd-sound-corpus]", verbose};
    const QString root = QDir::cleanPath(Game::root);
    const QString dynamic = root + "/dynamic";
    QString sounds = root + "/SOUND/MASZYNA";
    if (!QFileInfo(sounds).isDir())
        sounds = root + "/sounds";
    if (!QFileInfo(dynamic).isDir() || !QFileInfo(sounds).isDir()) {
        qInfo() << "[tests:mmd-sound-corpus] SKIP: no dynamic/ and sounds/ in the game root" << Game::root;
        return 0;
    }
    QElapsedTimer timer;
    timer.start();
    int files = 0, withVehicle = 0, withCab = 0, definitions = 0, samples = 0, found = 0, typos = 0;
    int controlValues = 0, controlSamples = 0, controlFound = 0;
    QMap<QString, int> missing;
    qint64 cabMs = 0;
    QDirIterator it(dynamic, {"*.mmd"}, QDir::Files, QDirIterator::Subdirectories);
    const auto count = [&](const Definition &d, int &all, int &ok) {
        for (const Sample *s : {&d.begin, &d.main, &d.end}) {
            if (s->empty())
                continue;
            ++all;
            if (!s->path.isEmpty())
                ++ok;
            else
                missing[s->name]++;
        }
        for (const Chunk &c : d.chunks) {
            if (c.sample.empty())
                continue;
            ++all;
            if (!c.sample.path.isEmpty())
                ++ok;
            else
                missing[c.sample.name]++;
        }
    };
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        ++files;
        const QString type = info.completeBaseName();
        Physics physics;
        if (QFileInfo::exists(info.path() + "/" + type + ".fiz"))
            if (auto v = MaszynaVehicle::read(info.path(), type))
                physics = Physics::of(v->physics());
        const Sounds s = read(info.path(), type, sounds, physics);
        withVehicle += s.vehicle.empty() ? 0 : 1;
        withCab += s.cab.size() > 1 ? 1 : 0;
        for (const QString &p : s.problems)
            typos += p.startsWith("gengwaydetach:") ? 1 : 0;
        for (const auto *list : {&s.vehicle, &s.cab})
            for (const Definition &d : *list) {
                ++definitions;
                count(d, samples, found);
            }
        QElapsedTimer cabTimer;
        cabTimer.start();
        const MaszynaCab cab = MaszynaCab::read(info.path(), info.fileName());
        for (const MaszynaCab::End &end : cab.ends)
            for (const MaszynaCab::Control &c : end.controls)
                for (const auto &key : c.keys) {
                    if (!key.first.startsWith("sound") || key.first == "soundproofing" || key.second.isEmpty())
                        continue;
                    ++controlValues;
                    count(readValue(key.first, key.second, sounds), controlSamples, controlFound);
                }
        cabMs += cabTimer.elapsed();
    }
    const qint64 ms = timer.elapsed();
    const int all = samples + controlSamples, ok = found + controlFound;
    qInfo().noquote() << QString("[tests:mmd-sound-corpus] %1 .mmd in %2 ms (cabs %3 ms): %4 with vehicle sounds, %5 with "
                                 "cab sounds, %6 definitions, %7 cab control values")
                                 .arg(files).arg(ms).arg(cabMs).arg(withVehicle).arg(withCab).arg(definitions).arg(controlValues);
    qInfo().noquote() << QString("[tests:mmd-sound-corpus] samples %1, found %2 (%3 %), cab controls' %4 of %5; "
                                 "gengwaydetach: in %6")
                                 .arg(all).arg(ok).arg(100.0 * ok / std::max(all, 1), 0, 'f', 3).arg(controlFound)
                                 .arg(controlSamples).arg(typos);
    for (auto i = missing.cbegin(); i != missing.cend(); ++i)
        qInfo().noquote() << "[tests:mmd-sound-corpus]   missing" << i.key() << i.value();
    test.check(files >= 1400 && withVehicle >= 1400, "the pack's vehicles have their sounds read");
    test.check(all > 0 && double(ok) / all >= 0.999, "at least 99.9 % of the samples are found (the review: 99.98 %)");
    test.check(missing.size() <= 11, "no more names missing than the review's 11");
    test.check(typos == 801, "gengwaydetach: told in the 801 vehicles where MaSzyna meets it (the review's count)");

    // ET22's 201e (dynamic/pkp/et22_v3).
    const QString et22 = dynamic + "/pkp/et22_v3";
    if (QFileInfo::exists(et22 + "/201e.mmd")) {
        auto v = MaszynaVehicle::read(et22, "201e");
        const Physics physics = v ? Physics::of(v->physics()) : Physics();
        const Sounds s = read(et22, "201e", sounds, physics);
        const Definition *motor = s.find(u"tractionmotor");
        test.check(motor && motor->chunks.size() == 6 && near(motor->chunks[4].pitch, 60) && motor->crossfade == 80
                           && near(motor->frequencyFactor, 0.04f),
                   "ET22's traction motor: six entries, crossfade 80, recorded pitches");
        const Definition *vent = s.find(u"ventilator");
        test.check(vent && near(vent->range, 150) && fileName(vent->main) == "697_et22-rvent.ogg"
                           && physics.rventnmax > 1.0 && near(vent->amplitudeFactor, float(2.2 / physics.rventnmax)),
                   "ET22's ventilator: one line, its factor over RVentnmax "
                           + QString::number(physics.rventnmax * 60) + " rpm");
        const Definition *relay = s.find(u"relay");
        test.check(relay && relay->chunks.size() == 48 && relay->soundproofing, "ET22's relays: one sample per notch, 48");
        const Definition *horn = s.find(u"horn1");
        test.check(horn && fileName(horn->main) == "9629]201e_rp1_trwa_1.ogg" && !horn->begin.path.isEmpty()
                           && near(horn->range, 1000),
                   "ET22's horn from the first of its random includes");
        test.check(s.findCab(u"runningnoise") && s.findCab(u"runningnoise")->chunks.size() == 6
                           && s.findCab(u"buzzer") && !s.findCab(u"buzzer")->main.path.isEmpty(),
                   "ET22's cab: running noise table, buzzer");
        const MaszynaCab cab = MaszynaCab::read(et22, "201e.mmd");
        int notches = 0;
        for (const MaszynaCab::Control &c : cab.ends[1].controls)
            if (c.label == "mainctrl")
                for (const auto &key : c.keys)
                    notches += key.first.startsWith("sound") && !readValue(key.first, key.second, sounds).main.path.isEmpty();
        test.check(notches == 49, "ET22's main controller: a click for each of its 49 positions, all found");
        // A car's read (each car its own, when first heard): the time.
        QElapsedTimer readTimer;
        readTimer.start();
        for (unsigned seed = 1; seed <= 20; ++seed) {
            std::mt19937 random(seed);
            read(et22, "201e", sounds, physics, &random);
        }
        qInfo().noquote() << QString("[tests:mmd-sound-corpus] ET22's sounds read for a car in %1 ms")
                                     .arg(double(readTimer.nsecsElapsed()) / 20e6, 0, 'f', 2);
    }
    return test.finish();
}

int TsreTests::runMmdSoundPlayerSuite(bool verbose) {
    TokenTest::Suite test{"[tests:mmd-sound-player]", verbose};
    QTemporaryDir dir;
    if (!SoundSystem::start(false) || !dir.isValid()) {
        test.check(false, "the sound system runs without a device");
        return test.finish();
    }
    const QString sounds = dir.filePath("sounds");
    writeWave(sounds + "/begin.wav", 0.2);
    writeWave(sounds + "/main.wav", 0.5);
    writeWave(sounds + "/end.wav", 0.3);
    writeWave(sounds + "/click.wav", 0.4);
    for (const char *n : {"t0", "t10", "t20"})
        writeWave(sounds + "/" + n + ".wav", 1.0);
    const QString folder = dir.filePath("dynamic/test/p");
    writeFile(folder + "/p.mmd", R"(locations: doors: 3 both -3 right end tractionmotors: 4 -4 end bogies: 6 -6 end endlocations
sounds:
 compressor: begin.wav main.wav end.wav 100
 brakeacc: click.wav
 curve: { sound0: t0.wav sound10: t10.wav pitch10: 2 sound20: t20.wav pitch20: 4 crossfade: 50 range: 100 }
 engine: { sound300: t0.wav sound750: t10.wav crossfade: 90 range: 150 }
 tractionmotor: main.wav 50 1 0 1 0
 outernoise: main.wav 1 0 1 0
 dooropen: click.wav
 horn1: begin.wav main.wav end.wav 1000
endsounds
internaldata:
 couplerattach: click.wav
 buzzer: click.wav
)");
    Physics physics;
    physics.power = 1000;
    physics.vmax = 100;
    const auto read = std::make_shared<const Sounds>(MaszynaSound::read(folder, "p", sounds, physics));

    // Begin, main looping, end on stop.
    {
        MaszynaEmitter e(*read->find(u"compressor"), nullptr);
        e.hear(at(10));
        e.play(MaszynaEmitter::Looping);
        test.check(e.playing() && e.voiceCount() == 1, "a held sound starts: begin and main in one voice");
        run(e, 1.5);
        e.play(MaszynaEmitter::Looping);
        test.check(e.playing() && e.voiceCount() == 1, "its main part loops, played each frame without a second voice");
        e.stop();
        test.check(!e.playing() && e.voiceCount() == 1, "stopped: the main cut, the end part playing");
        run(e, 0.5);
        test.check(e.voiceCount() == 0, "the end part ends");
        e.play(MaszynaEmitter::Looping);
        e.stop(true);
        test.check(e.voiceCount() == 0, "stopped without its end part");
    }
    // One-shots overlap unless exclusive.
    {
        MaszynaEmitter e(*read->find(u"brakeacc"), nullptr);
        e.hear(at(5));
        e.play();
        e.play();
        test.check(e.voiceCount() == 2, "a one-shot played again while it plays: both heard");
        run(e, 0.6);
        test.check(e.voiceCount() == 0, "one-shots end by themselves");
        e.play(MaszynaEmitter::Exclusive);
        e.play(MaszynaEmitter::Exclusive);
        test.check(e.voiceCount() == 1, "an exclusive one plays once at a time");
    }
    // A table: entries by the value, crossfaded, pitched toward the
    // neighbour's recorded pitch.
    {
        const Definition &curve = *read->find(u"curve");
        MaszynaEmitter e(curve, nullptr);
        e.hear(at(1));
        e.pitch(0.12f);
        e.play(MaszynaEmitter::Looping);
        test.check(e.voiceCount() == 1, "value 12: the entry of 10 only (the next starts at 15)");
        e.pitch(0.17f);
        e.play(MaszynaEmitter::Looping);
        run(e, 0.02);
        const float out = (1.0f - 0.4f) / (1.0f - 0.4f * 0.57f), in = 0.4f / (1.0f - 0.6f * 0.57f);
        test.check(e.voiceCount() == 2 && near(MaszynaEmitter::crossfadeGain(curve, 1, 17), out)
                           && near(MaszynaEmitter::crossfadeGain(curve, 2, 17), in),
                   "value 17: entries 10 and 20 crossfaded, MaSzyna's curves");
        test.check(near(MaszynaEmitter::chunkPitch(curve, 1, 17), 1.0f + (2.0f - 1.0f) * 0.7f)
                           && near(MaszynaEmitter::chunkPitch(curve, 2, 17), 0.5f + 0.5f * 0.7f)
                           && near(MaszynaEmitter::chunkPitch(curve, 2, 25), 1.0f),
                   "an entry's pitch toward its neighbour's: 10 rises to 20's, 20 from 10's");
        e.pitch(0.25f);
        e.play(MaszynaEmitter::Looping);
        run(e, 0.02);
        test.check(e.voiceCount() == 1, "value 25: the entry of 10 out of its range stops");
    }
    // A table's last entry at its own threshold too (MaSzyna's stops there):
    // an engine's in RPM at its top speed (ST44's 750), past it none.
    {
        const Definition &engine = *read->find(u"engine");
        MaszynaEmitter e(engine, nullptr);
        e.hear(at(1));
        e.pitch(7.5f);
        e.play(MaszynaEmitter::Looping);
        const bool top = e.voiceCount() == 1;
        e.pitch(7.6f);
        e.play(MaszynaEmitter::Looping);
        run(e, 0.02);
        test.check(top && e.voiceCount() == 0 && MaszynaEmitter::covers(engine, 1, 750.0f)
                           && !MaszynaEmitter::covers(engine, 1, 751.0f),
                   "a table's last entry at its threshold (750 of 300 and 750), none past it");
    }
    // Distance and soundproofing.
    test.check(near(MaszynaEmitter::distanceGain(50, 100, 1), 6.25f / (6.25f + 1.75f * 43.75f))
                       && near(MaszynaEmitter::distanceGain(3, 100, 1), 1)
                       && near(MaszynaEmitter::distanceGain(110, 100, 1),
                               6.25f / (6.25f + 1.75f * 103.75f) * (1.0f - (12100.0f - 10000.0f) / 5625.0f))
                       && MaszynaEmitter::distanceGain(126, 100, 1) == 0.0f
                       && MaszynaEmitter::distanceGain(500, -1, 1) == 1.0f,
               "MaSzyna's distance: full to range / 16, rolloff 1.75, faded out by 1.25 range; -1 everywhere");
    {
        MaszynaEmitter e(*read->find(u"compressor"), nullptr);
        e.hear(at(600));
        e.play(MaszynaEmitter::Looping);
        test.check(e.voiceCount() == 0, "too far to start (beyond five times its range)");
        e.hear(at(10));
        e.play(MaszynaEmitter::Looping);
        run(e, 0.02);
        test.check(e.voiceCount() == 1 && e.voiceSpeed(0) > 0.0f, "coming near it starts, without its begin part");
    }
    {
        const auto table = defaultSoundproofing();
        SoundListener listener;
        int car = 0, other = 0;
        listener.attachedTo = &car;
        listener.view = SoundView::Cab;
        listener.cab = 1;
        const int own = MaszynaSounds::column(listener, &car);
        listener.cab = 2;
        const int rear = MaszynaSounds::column(listener, &car);
        listener.view = SoundView::External;
        const int outside = MaszynaSounds::column(listener, &car);
        test.check(own == 2 && rear == 0 && outside == 4 && MaszynaSounds::column(listener, &other) == 5,
                   "the listener's place: its car's front or rear cab, outside following it, elsewhere");
        const Definition &horn = *read->find(u"horn1");
        Definition general = horn;
        general.placement = Placement::General;
        test.check(near(MaszynaSounds::soundproofing(horn, table, 2, nullptr, 5), std::sqrt(0.2f))
                           && near(MaszynaSounds::soundproofing(horn, table, 5, &table, 2), std::sqrt(0.2f))
                           && near(MaszynaSounds::soundproofing(general, table, 2, &table, 2), 1.0f),
                   "an outside sound from its own cab, from another car's cab: strong; a general one never");
        // Its cab's windows open (task sound 03): toward the open window
        // column, Some (sqrt 0.5 here), never above outside; half open
        // halfway; an inside sound as shut; another car's through the
        // listener's window too.
        Definition inside = horn;
        inside.placement = Placement::Internal;
        const float shut = MaszynaSounds::soundproofing(horn, table, 2, nullptr, 5);
        const float open = MaszynaSounds::soundproofing(horn, table, 2, nullptr, 5, 1.0f);
        const float half = MaszynaSounds::soundproofing(horn, table, 2, nullptr, 5, 0.5f);
        const float outsideLevel = MaszynaSounds::soundproofing(horn, table, 4, nullptr, 5);
        test.check(near(open, table[2][3]) && open <= outsideLevel && near(half, 0.5f * (shut + open))
                           && near(MaszynaSounds::soundproofing(inside, table, 2, nullptr, 5, 1.0f),
                                   MaszynaSounds::soundproofing(inside, table, 2, nullptr, 5))
                           && near(MaszynaSounds::soundproofing(horn, table, 5, &table, 2, 1.0f), table[2][3]),
                   QString("its cab's windows open: an outside sound %1 (shut %2, half open %3, outside %4), an "
                           "inside one as shut, another car's through them")
                           .arg(open).arg(shut).arg(half).arg(outsideLevel));
    }
    // A car's emitters by place.
    {
        MaszynaSounds car(nullptr, read);
        test.check(car.emitters(u"tractionmotor").size() == 2 && car.emitters(u"outernoise").size() == 2
                           && car.emitters(u"dooropen").size() == 3 && car.emitters(u"couplerattach").size() == 2
                           && near(car.emitters(u"tractionmotor")[0]->place()[2], -4)
                           && near(car.emitters(u"dooropen")[2]->place()[1], 2.0f),
                   "a motor and a bogie noise per location, a door sound per side, a coupler at each end");
        CarSounds::Frame frame;
        frame.car = &car;
        frame.position[0] = 20;
        car.set(frame);
        SoundListener listener;
        SoundSystem::setListener(listener);
        car.emitters(u"brakeacc")[0]->play();
        std::vector<float> out(800 * 2);
        SoundSystem::update(1.0f / 60.0f);
        SoundEngine::get().render(out.data(), 800);
        test.check(car.heard() && car.voiceCount() == 1 && car.cab(u"buzzer") == nullptr,
                   "the car is updated by the sound system; no cab sounds unless it leads");
        frame.lead = true;
        car.set(frame);
        test.check(car.cab(u"buzzer") != nullptr, "its cab's sounds when it leads");
    }

    // What the host drives on every car.
    const QString hostFolder = dir.filePath("dynamic/test/h");
    writeFile(hostFolder + "/h.mmd", R"(locations: doors: 3 both end bogies: 6 -6 end endlocations
sounds:
 outernoise: main.wav 0.01 0.1 1 0
 brakesound: main.wav 1 0 1 0
 unbrake: main.wav 50
 curve: main.wav 200
 dooropen: click.wav
 doorclose: click.wav
endsounds
internaldata:
 runningnoise: main.wav 0.01 0.5 1 0
)");
    {
        auto hostRead = std::make_shared<const Sounds>(MaszynaSound::read(hostFolder, "h", sounds, physics));
        MaszynaSounds car(nullptr, hostRead);
        int train = 0, otherTrain = 0;
        SoundValues values;
        CarSounds::Frame frame;
        frame.car = &car;
        frame.train = &train;
        frame.values = &values;
        frame.position[0] = 10;
        SoundListener listener;
        double clock = 0.0;
        std::vector<float> out(800 * 2);
        const auto step = [&](int frames) {
            for (int i = 0; i < frames; ++i) {
                frame.seconds = 1.0f / 60.0f;
                car.set(frame);
                clock += 1.0 / 60.0;
                car.update(listener, clock);
                SoundEngine::get().render(out.data(), 800);
            }
        };
        const auto playing = [&](QStringView key) {
            for (MaszynaEmitter *e : car.emitters(key))
                if (!e->playing())
                    return false;
            return !car.emitters(key).empty();
        };
        values.set(Sms::Control::Speed, 20.0f);
        step(2);
        test.check(playing(u"outernoise") && car.emitters(u"outernoise").size() == 2 && !playing(u"brakesound"),
                   "running: each bogie's running noise, no brakes");
        listener.train = &train;
        listener.view = SoundView::Cab;
        step(2);
        test.check(!playing(u"outernoise"), "aboard in the cab: the outside running noise kept from the listener");
        // In its cab, its windows open (task sound 03; MaSzyna's head-out
        // view hears the driven train's outer noise): as at an open window
        // (the window column, Some as outside) times the share, so fully open
        // as outside, half open half; shut again, kept from it.
        const auto loudest = [&](QStringView key) {
            float gain = 0.0f;
            for (MaszynaEmitter *e : car.emitters(key))
                for (int k = 0; k < e->voiceCount(); ++k)
                    gain = std::max(gain, e->voiceGain(k));
            return gain;
        };
        listener.attachedTo = &car;
        listener.cab = 1;
        listener.windowCount = 1;
        listener.windows[0].open = 1.0f;
        step(2);
        const bool openPlays = playing(u"outernoise");
        const float open = loudest(u"outernoise");
        listener.windows[0].open = 0.5f;
        step(2);
        const float half = loudest(u"outernoise");
        listener.view = SoundView::External;
        step(2);
        const float outside = loudest(u"outernoise");
        listener.view = SoundView::Cab;
        listener.windows[0].open = 0.0f;
        step(2);
        test.check(openPlays && open > 0.0f && open < 1.0f && near(half, 0.5f * open) && near(open, outside)
                           && !playing(u"outernoise"),
                   QString("aboard, its cab's window open: the outside running noise %1 (half open %2, outside %3); "
                           "shut, kept from it")
                           .arg(open).arg(half).arg(outside));
        listener.windowCount = 0;
        listener.attachedTo = nullptr;
        listener.train = &otherTrain;
        step(2);
        test.check(playing(u"outernoise"), "in another train's cab: heard");
        values.set(Sms::Control::BrakeCyl, 0.3f / 0.00689476f);
        step(2);
        test.check(playing(u"brakesound"), "braking: the shoes heard");
        for (int i = 0; i < 40; ++i) {
            values.set(Sms::Control::BrakeCyl, (0.3f - 0.005f * float(i)) / 0.00689476f);
            step(1);
        }
        test.check(playing(u"unbrake"), "the cylinder emptying fast: the release hiss");
        values.set(Sms::Control::CurveForce, 50000.0f);
        step(2);
        test.check(playing(u"curve"), "a sharp curve at speed: the curve squeal");
        car.event(QStringLiteral("dooropen:left"));
        step(1);
        const auto &opens = car.emitters(u"dooropen");
        test.check(opens.size() == 2 && opens[0]->playing() && !opens[1]->playing(),
                   "a door opening on the left: the left door's sound only");
        frame.lead = true;
        listener.attachedTo = &car;
        listener.train = &train;
        step(2);
        test.check(car.cab(u"runningnoise") && car.cab(u"runningnoise")->playing(), "in its cab: the cab's running noise");
        listener.view = SoundView::External;
        step(2);
        test.check(!car.cab(u"runningnoise")->playing() && playing(u"outernoise"),
                   "outside following it: the cab's noise stops, the outside one plays");
    }

    // More of what the host drives (2026-11-13): the brake cylinder's
    // piston clicking at each fifteenth of its most, in and out; rain on the
    // ridden cab, heard in it; the cab's ambience (fadesound) while it leads.
    const QString moreFolder = dir.filePath("dynamic/test/m");
    writeFile(moreFolder + "/m.mmd", R"(sounds:
 brakecylinderinc: main.wav
 brakecylinderdec: main.wav
endsounds
internaldata:
 rainsound: main.wav
 fadesound: main.wav
)");
    {
        auto moreRead = std::make_shared<const Sounds>(MaszynaSound::read(moreFolder, "m", sounds, physics));
        MaszynaSounds car(nullptr, moreRead);
        int train = 0;
        SoundValues values;
        CarSounds::Frame frame;
        frame.car = &car;
        frame.train = &train;
        frame.values = &values;
        SoundListener listener;
        double clock = 0.0;
        std::vector<float> out(800 * 2);
        const auto step = [&](int frames) {
            for (int i = 0; i < frames; ++i) {
                frame.seconds = 1.0f / 60.0f;
                car.set(frame);
                clock += 1.0 / 60.0;
                car.update(listener, clock);
                SoundEngine::get().render(out.data(), 800);
            }
        };
        const auto playing = [&](QStringView key) {
            for (MaszynaEmitter *e : car.emitters(key))
                if (e->playing())
                    return true;
            return false;
        };
        step(2);
        // 0.5 bar of the default 3.8 bar most: the first fifteenth.
        values.set(Sms::Control::BrakeCyl, 0.05f / 0.00689476f);
        step(1);
        const bool in = playing(u"brakecylinderinc") && !playing(u"brakecylinderdec");
        step(120);
        values.set(Sms::Control::BrakeCyl, 0.0f);
        step(1);
        const bool out_ = playing(u"brakecylinderdec");
        test.check(in && out_, QString("the brake cylinder's piston: in %1, out %2").arg(in).arg(out_));
        frame.lead = true;
        listener.attachedTo = &car;
        listener.train = &train;
        listener.view = SoundView::Cab;
        step(2);
        const bool fade = car.cab(u"fadesound") != nullptr && car.cab(u"fadesound")->playing();
        const bool dry = car.cab(u"rainsound") != nullptr && !car.cab(u"rainsound")->playing();
        frame.rain = 0.5f;
        step(2);
        const bool raining = car.cab(u"rainsound")->playing();
        listener.view = SoundView::External;
        step(2);
        const bool outside = !car.cab(u"rainsound")->playing();
        test.check(fade && dry && raining && outside,
                   QString("the cab's ambience while it leads %1; rain in its cab: none dry %2, heard %3, not from "
                           "outside %4").arg(fade).arg(dry).arg(raining).arg(outside));
    }

    // The cab's controls' clicks (TGauge::UpdateValue, TButton).
    const QString cabFolder = dir.filePath("dynamic/test/c");
    writeFile(cabFolder + "/c.fiz", "Param. Category=train Type=et22 Vmax=100\n");
    writeFile(cabFolder + "/c.mmd", R"(internaldata:
 ctrl: t0.wav
 switch: t10.wav
cab1definition:
-1.2 1.5 7.35 1.2 1.5 8.5
driver1pos:
0.92 3.35 7.3
mainctrl: { nastawnik rot -0.0197 0.0 0.1 sound0: t0.wav sound1: click.wav }
dirkey: { nawrotnik rot 0.08 0.01 0.1 soundinc: begin.wav sounddec: end.wav }
compressor_sw: { sprezarka rot 0.16 0 0.1 }
i-mainbreaker: { lampka soundinc: begin.wav sounddec: end.wav }
)");
    {
        const auto vehicle = MaszynaVehicle::read(cabFolder, "c");
        if (vehicle == nullptr) {
            test.check(false, "the test vehicle reads");
            return test.finish();
        }
        const auto cabRead = std::make_shared<const Sounds>(MaszynaSound::read(cabFolder, "c", sounds, physics));
        MaszynaSounds car(vehicle, cabRead);
        MaszynaCabControls controls(vehicle, 1);
        CarSounds::Frame frame;
        frame.car = &car;
        frame.lead = true;
        car.set(frame);
        const float at[3] = {0.3f, 2.0f, 7.5f};
        int voices = car.voiceCount();
        const auto played = [&](int index, double value) {
            car.control(&controls, index, value, at);
            const int now = car.voiceCount();
            const int started = now - voices;
            voices = now;
            return started;
        };
        test.check(controls.end().controls.size() == 4 && controls.end().controls[3].lamp,
                   "the cab's four controls, the last a lamp");
        test.check(played(0, 0) == 0 && played(1, 0) == 0 && played(2, 0) == 0 && played(3, 0) == 0,
                   "a control's first value is taken silently (no clicks entering the cab)");
        test.check(car.cab(u"ctrl") && near(car.cab(u"ctrl")->place()[2], 7.5f),
                   "the main controller's fallback sound goes to its part");
        test.check(played(0, 1) == 1, "the main controller to 1: its sound1");
        test.check(played(0, 1.004) == 0, "a change below a hundredth: nothing");
        test.check(played(0, 1.5) == 1 && car.cab(u"ctrl")->playing(),
                   "between whole values, no soundinc: the cab's ctrl: falls back");
        test.check(played(1, 1) == 1 && played(1, 0) == 1, "the reverser: soundinc up, sounddec down");
        test.check(played(2, 1) == 1 && car.cab(u"switch")->playing(), "a switch without sounds: the cab's switch:");
        test.check(played(3, 1) == 1 && played(3, 1) == 0 && played(3, 0) == 1, "a lamp: on and off, once each");
    }
    return test.finish();
}
