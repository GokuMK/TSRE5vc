#include "SoundTestSuite.h"

#include <QByteArray>
#include <QDebug>
#include <QDirIterator>
#include <QFileInfo>
#include <QMap>
#include <cmath>
#include <cstring>
#include <tsre/sound/SoundBank.h>
#include <tsre/sound/SoundEngine.h>
#include <tsre/sound/StreamPlayer.h>
#include <tsre/sound/WavFile.h>

namespace {

// RIFF chunk builders for wave files written by the tests.
void put16(QByteArray &b, uint16_t v) {
    b.append(char(v & 0xFF));
    b.append(char(v >> 8));
}

void put32(QByteArray &b, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b.append(char((v >> (8 * i)) & 0xFF));
}

QByteArray chunk(const char *id, const QByteArray &body) {
    QByteArray c(id, 4);
    put32(c, uint32_t(body.size()));
    c.append(body);
    if (body.size() & 1)
        c.append('\0');
    return c;
}

QByteArray fmtChunk(uint16_t format, uint16_t channels, uint32_t rate, uint16_t bits) {
    QByteArray b;
    put16(b, format);
    put16(b, channels);
    put32(b, rate);
    put32(b, rate * channels * bits / 8);
    put16(b, uint16_t(channels * bits / 8));
    put16(b, bits);
    return chunk("fmt ", b);
}

QByteArray pcm16(const std::vector<int16_t> &samples) {
    QByteArray b;
    for (int16_t s : samples)
        put16(b, uint16_t(s));
    return b;
}

QByteArray cueChunk(const std::vector<uint32_t> &positions) {
    QByteArray b;
    put32(b, uint32_t(positions.size()));
    for (size_t i = 0; i < positions.size(); ++i) {
        put32(b, uint32_t(i + 1));
        put32(b, positions[i]);
        b.append("data", 4);
        put32(b, 0);
        put32(b, 0);
        put32(b, positions[i]);
    }
    return chunk("cue ", b);
}

QByteArray smplChunk(const std::vector<std::pair<uint32_t, uint32_t>> &loops) {
    QByteArray b;
    for (int i = 0; i < 7; ++i)
        put32(b, 0);
    put32(b, uint32_t(loops.size()));
    put32(b, 0);
    for (size_t i = 0; i < loops.size(); ++i) {
        put32(b, uint32_t(i));
        put32(b, 0);
        put32(b, loops[i].first);
        put32(b, loops[i].second);
        put32(b, 0);
        put32(b, 0);
    }
    return chunk("smpl", b);
}

QByteArray riff(const QByteArray &chunks) {
    QByteArray b("RIFF", 4);
    put32(b, uint32_t(4 + chunks.size()));
    b.append("WAVE", 4);
    b.append(chunks);
    return b;
}

struct Checker {
    const char *suite;
    bool verbose;
    int passed = 0;
    int failed = 0;
    void operator()(bool ok, const char *name) {
        if (ok) {
            ++passed;
            if (verbose)
                qInfo().noquote() << QString("[tests:%1] PASS").arg(suite) << name;
        } else {
            ++failed;
            qWarning().noquote() << QString("[tests:%1] FAIL").arg(suite) << name;
        }
    }
};

void waveCases(Checker &check) {
    WavData w;
    QString error;
    // Plain 16-bit mono.
    const std::vector<int16_t> ramp = {0, 1000, -1000, 32767, -32768, 5};
    check(WavFile::parse(riff(fmtChunk(1, 1, 11025, 16) + chunk("data", pcm16(ramp))), w, &error)
                  && w.sampleRate == 11025 && w.channels == 1 && w.sourceBits == 16 && w.samples == ramp
                  && w.cuePoints.empty(),
          "a 16-bit mono wave reads its rate and samples");
    // Stereo with the chunks in another order and a fact chunk first.
    QByteArray fact;
    put32(fact, 3);
    check(WavFile::parse(riff(chunk("fact", fact) + chunk("LIST", QByteArray("INFOabc", 7))
                              + fmtChunk(1, 2, 22050, 16) + chunk("data", pcm16(ramp))),
                         w, &error)
                  && w.channels == 2 && w.frames() == 3 && w.samples == ramp,
          "chunks before fmt and data, odd-sized ones padded, are skipped");
    // 8-bit unsigned.
    check(WavFile::parse(riff(fmtChunk(1, 1, 8000, 8) + chunk("data", QByteArray("\x00\x80\xff", 3))), w, &error)
                  && w.sourceBits == 8 && w.samples == std::vector<int16_t>({-32768, 0, 32512}),
          "8-bit samples become 16-bit around 128");
    // 32-bit float.
    QByteArray floats;
    for (float f : {0.0f, 0.5f, -1.0f, 2.0f}) {
        uint32_t word;
        std::memcpy(&word, &f, 4);
        put32(floats, word);
    }
    check(WavFile::parse(riff(fmtChunk(3, 1, 44100, 32) + chunk("data", floats)), w, &error)
                  && w.samples == std::vector<int16_t>({0, 16384, -32767, 32767}),
          "float samples are scaled and clipped");
    // WAVE_FORMAT_EXTENSIBLE with a PCM subformat.
    QByteArray extensible;
    put16(extensible, 0xFFFE);
    put16(extensible, 1);
    put32(extensible, 11025);
    put32(extensible, 22050);
    put16(extensible, 2);
    put16(extensible, 16);
    put16(extensible, 22);
    put16(extensible, 16);
    put32(extensible, 4);
    put16(extensible, 1);
    extensible.append(QByteArray(14, '\0'));
    check(WavFile::parse(riff(chunk("fmt ", extensible) + chunk("data", pcm16(ramp))), w, &error)
                  && w.samples == ramp,
          "an extensible PCM wave reads as PCM");
    // Cue points after the data, sorted, and past-the-end ones dropped.
    std::vector<int16_t> ten(10, 7);
    check(WavFile::parse(riff(fmtChunk(1, 1, 11025, 16) + chunk("data", pcm16(ten)) + cueChunk({8, 2, 50})), w,
                         &error)
                  && w.cuePoints == std::vector<uint32_t>({2, 8}),
          "cue points are read after the data, sorted, within the sound");
    // A smpl loop gives two points; the later of cue and smpl wins.
    check(WavFile::parse(riff(fmtChunk(1, 1, 11025, 16) + chunk("data", pcm16(ten)) + smplChunk({{3, 9}})), w,
                         &error)
                  && w.cuePoints == std::vector<uint32_t>({3, 9}),
          "a smpl loop's start and end are loop points");
    check(WavFile::parse(riff(fmtChunk(1, 1, 11025, 16) + chunk("data", pcm16(ten)) + smplChunk({{3, 9}})
                              + cueChunk({1, 4, 6})),
                         w, &error)
                  && w.cuePoints == std::vector<uint32_t>({1, 4, 6}),
          "a cue chunk after a smpl chunk replaces its points");
    check(WavFile::parse(riff(fmtChunk(1, 1, 11025, 16) + chunk("data", pcm16(ten)) + cueChunk({1, 4})
                              + smplChunk({})),
                         w, &error)
                  && w.cuePoints == std::vector<uint32_t>({1, 4}),
          "a smpl chunk without loops keeps the cue points");
    // Stereo cue points count frames: a point at the last frame stays.
    check(WavFile::parse(riff(fmtChunk(1, 2, 11025, 16) + chunk("data", pcm16(ten)) + cueChunk({1, 5, 6})), w,
                         &error)
                  && w.cuePoints == std::vector<uint32_t>({1, 5}),
          "stereo cue points count frames");
    // A data chunk claiming more than the file holds.
    QByteArray shortData("data", 4);
    put32(shortData, 1000);
    shortData.append(pcm16({1, 2, 3}));
    check(WavFile::parse(riff(fmtChunk(1, 1, 11025, 16) + shortData), w, &error) && w.samples.size() == 3,
          "a data chunk longer than the file is cut to what is there");
    // Failures.
    check(!WavFile::parse(QByteArray("RIFX....WAVE"), w, &error), "a non-RIFF file fails");
    check(!WavFile::parse(riff(fmtChunk(2, 1, 11025, 4) + chunk("data", QByteArray(16, '\0'))), w, &error)
                  && error.contains("unsupported"),
          "MS ADPCM is reported as unsupported");
    check(!WavFile::parse(riff(fmtChunk(1, 6, 11025, 16) + chunk("data", QByteArray(24, '\0'))), w, &error),
          "six channels are refused");
    check(!WavFile::parse(riff(fmtChunk(1, 1, 11025, 16)), w, &error) && error == "no data",
          "a wave without data fails");
    check(!WavFile::parse(riff(chunk("data", pcm16(ramp))), w, &error), "a wave without fmt fails");
    // Mono for 3D: the second channel, cue points kept.
    WavData stereo;
    stereo.channels = 2;
    stereo.sampleRate = 11025;
    stereo.samples = {1, 2, 3, 4, 5, 6};
    stereo.cuePoints = {1, 2};
    const WavData mono = WavFile::toMono(stereo);
    check(mono.channels == 1 && mono.samples == std::vector<int16_t>({2, 4, 6}) && mono.cuePoints == stereo.cuePoints,
          "mono keeps the second channel and the cue points");
}

// A clip whose samples are their own values (read back exactly at speed 1),
// split at the given loop points.
SoundClip numbered(int rate, std::vector<int> values, std::vector<uint32_t> points = {}) {
    WavData wave;
    wave.sampleRate = rate;
    wave.channels = 1;
    for (int v : values)
        wave.samples.push_back(int16_t(v));
    wave.cuePoints = points;
    return *SoundClip::fromWave("test", wave);
}

std::vector<int> range(int from, int to) {
    std::vector<int> values;
    for (int v = from; v <= to; ++v)
        values.push_back(v);
    return values;
}

// The next frames as the clips' sample values; the resampler holds two
// frames back, so the first read of a player drops them.
std::vector<int> play(StreamPlayer &player, int frames, bool first = false) {
    std::vector<float> out(size_t(frames + (first ? 2 : 0)));
    player.read(out.data(), out.size());
    std::vector<int> values;
    for (size_t i = first ? 2 : 0; i < out.size(); ++i)
        values.push_back(int(std::lround(out[i] * 32768.0f)));
    return values;
}

std::vector<int> join(std::initializer_list<std::vector<int>> parts) {
    std::vector<int> all;
    for (const auto &p : parts)
        all.insert(all.end(), p.begin(), p.end());
    return all;
}

void playerCases(Checker &check) {
    using Mode = StreamPlayer::Mode;
    const SoundClip a = numbered(1000, range(1, 5));
    // Intro 10-11, sustain 12-13 and 14-15, release 16-19.
    const SoundClip b = numbered(1000, range(10, 19), {2, 4, 6});
    const std::vector<int> silence(4, 0);
    {
        StreamPlayer player(1000, 1);
        player.queue(&a, Mode::OneShot);
        check(player.isPlaying() && player.busy(), "a queued one-shot is playing");
        check(play(player, 9, true) == join({range(1, 5), silence}) && !player.busy() && !player.isPlaying(),
              "a one-shot plays once");
    }
    {
        StreamPlayer player(1000, 1);
        player.queue(&a, Mode::Loop);
        const std::vector<int> start = play(player, 7, true);
        player.queue(nullptr, Mode::Release);
        check(!player.isPlaying() && player.busy(), "a released loop is on its way out");
        check(start == join({range(1, 5), {1, 2}}) && play(player, 7) == join({{3, 4, 5}, silence}),
              "a loop repeats and a release lets it finish the pass");
    }
    // The player reads two frames ahead of what is heard: commands act on
    // the part being read.
    {
        // Read up to the second pass's first sustain part (12).
        StreamPlayer player(1000, 1);
        player.queue(&b, Mode::LoopRelease);
        const std::vector<int> start = play(player, 5, true);
        player.queue(nullptr, Mode::Release);
        check(start == std::vector<int>({10, 11, 12, 13, 14})
                      && play(player, 11) == std::vector<int>({15, 12, 13, 14, 15, 16, 17, 18, 19, 0, 0}),
              "loop-release: intro, sustain looping, a release plays on to the sustain's end");
    }
    {
        StreamPlayer player(1000, 1);
        player.queue(&b, Mode::LoopRelease);
        const std::vector<int> start = play(player, 5, true);
        player.queue(nullptr, Mode::ReleaseWithJump);
        check(start == std::vector<int>({10, 11, 12, 13, 14})
                      && play(player, 9) == std::vector<int>({15, 12, 13, 16, 17, 18, 19, 0, 0}),
              "a release with jump goes to the release after the part playing");
    }
    {
        // Read into the last sustain part (14).
        StreamPlayer player(1000, 1);
        player.queue(&b, Mode::LoopRelease);
        const std::vector<int> start = play(player, 7, true);
        player.queue(nullptr, Mode::Release);
        check(start == std::vector<int>({10, 11, 12, 13, 14, 15, 12})
                      && play(player, 9) == std::vector<int>({13, 14, 15, 16, 17, 18, 19, 0, 0}),
              "a release in the last sustain part goes on to the release");
    }
    {
        StreamPlayer player(1000, 1);
        player.queue(&b, Mode::LoopRelease);
        play(player, 0, true);
        player.queue(nullptr, Mode::Release);
        check(play(player, 7) == std::vector<int>({10, 11, 16, 17, 18, 19, 0}),
              "a release in the intro goes straight to the release");
    }
    {
        StreamPlayer player(1000, 1);
        player.queue(&a, Mode::OneShot);
        player.queue(&b, Mode::LoopRelease);
        check(play(player, 9, true) == std::vector<int>({1, 2, 3, 4, 5, 10, 11, 12, 13}),
              "queued clips follow each other without a gap");
    }
    {
        StreamPlayer player(1000, 1);
        player.queue(&a, Mode::OneShot);
        player.queue(&b, Mode::Loop);
        player.queue(nullptr, Mode::Release);
        check(play(player, 7, true) == join({range(1, 5), {0, 0}}), "a release drops loops still waiting");
    }
    {
        StreamPlayer player(1000, 1);
        player.queue(&a, Mode::OneShot);
        player.queue(&a, Mode::OneShot);
        player.queue(&a, Mode::OneShot, true);
        check(play(player, 12, true) == join({range(1, 5), range(1, 5), {0, 0}}),
              "one-shots stack, but not from a trigger repeating");
    }
    {
        StreamPlayer player(1000, 1);
        player.queue(&b, Mode::OneShot);
        player.queue(&a, Mode::Loop);
        player.queue(&a, Mode::Loop);
        player.queue(&b, Mode::LoopRelease);
        player.queue(&b, Mode::Loop);
        const std::vector<int> start = play(player, 17, true);
        check(start == join({range(10, 19), range(1, 5), {1, 2}}), "a repeated loop is queued once");
        player.queue(nullptr, Mode::Release);
        check(play(player, 5) == std::vector<int>({3, 4, 5, 0, 0}),
              "a waiting loop-release turned loop is released with the rest");
    }
    {
        StreamPlayer player(1000, 1);
        player.queue(&b, Mode::LoopRelease);
        play(player, 7, true);
        player.deactivate();
        check(player.busy() && play(player, 4, true) == std::vector<int>({10, 11, 12, 13}),
              "out of reach, a loop starts again from its beginning");
        player.queue(nullptr, Mode::Release);
        play(player, 1);
        player.dropIfReleasing();
        check(!player.busy(), "a releasing loop without volume stops at once");
    }
    {
        StreamPlayer player(1000, 1);
        player.queue(&a, Mode::OneShot);
        player.deactivate();
        check(!player.busy(), "out of reach, short one-shots are dropped");
    }
    {
        // Speed 2 reads every other frame (the cubic passes through them).
        const SoundClip c = numbered(1000, range(1, 12));
        StreamPlayer player(1000, 1);
        player.setSpeed(2.0f);
        player.queue(&c, Mode::OneShot);
        std::vector<float> out(9);
        player.read(out.data(), out.size());
        check(std::lround(out[2] * 32768) == 3 && std::lround(out[3] * 32768) == 5
                      && std::lround(out[6] * 32768) == 11,
              "speed 2 plays the clip twice as fast");
    }
    {
        // A clip at half the output rate: each frame lasts two.
        const SoundClip c = numbered(500, {0, 1000, 2000, 3000, 4000});
        StreamPlayer player(1000, 1);
        player.queue(&c, Mode::OneShot);
        std::vector<float> out(9);
        player.read(out.data(), out.size());
        check(std::lround(out[6] * 32768) == 1000 && std::lround(out[7] * 32768) == 1500
                      && std::lround(out[8] * 32768) == 2000,
              "a clip's own rate is kept, frames in between interpolated");
    }
}

// Frequency of a channel by its rising zero crossings.
double frequency(const std::vector<float> &out, int channels, int channel, int rate) {
    int crossings = 0;
    size_t firstCrossing = 0;
    size_t lastCrossing = 0;
    for (size_t i = 1; i < out.size() / size_t(channels); ++i) {
        const float before = out[(i - 1) * channels + channel];
        const float now = out[i * channels + channel];
        if (before < 0.0f && now >= 0.0f) {
            if (crossings == 0)
                firstCrossing = i;
            lastCrossing = i;
            ++crossings;
        }
    }
    return crossings > 1 ? double(crossings - 1) * rate / double(lastCrossing - firstCrossing) : 0.0;
}

double peak(const std::vector<float> &out, int channels, int channel) {
    double value = 0.0;
    for (size_t i = size_t(channel); i < out.size(); i += size_t(channels))
        value = std::max(value, double(std::abs(out[i])));
    return value;
}

void engineCases(Checker &check) {
    SoundEngine &engine = SoundEngine::get();
    const bool started = engine.start(false, 48000);
    check(started && engine.sampleRate() == 48000 && engine.channels() == 2, "the engine runs without a device");
    if (!started)
        return;
    WavData wave;
    wave.sampleRate = 11025;
    wave.channels = 1;
    for (int i = 0; i < 11025; ++i)
        wave.samples.push_back(int16_t(std::lround(16000.0 * std::sin(2.0 * M_PI * 441.0 * i / 11025.0))));
    const auto clip = SoundClip::fromWave("sine", wave);
    std::vector<float> out(48000 * 2);
    {
        SoundVoice voice(false);
        check(voice.valid(), "a voice heard as it is");
        voice.player().queue(clip.get(), StreamPlayer::Mode::Loop);
        voice.setGain(0.5f);
        voice.update();
        engine.render(out.data(), 48000);
        const double f = frequency(out, 2, 0, 48000);
        const double a = peak(out, 2, 0);
        check(std::abs(f - 441.0) < 2.0 && std::abs(a - 0.5 * 16000.0 / 32768.0) < 0.02,
              "a clip at 11025 Hz plays at its pitch and gain through the engine");
        voice.player().setSpeed(1.5f);
        engine.render(out.data(), 48000);
        check(std::abs(frequency(out, 2, 1, 48000) - 661.5) < 3.0, "the speed raises the pitch");
    }
    {
        SoundVoice voice(true);
        const float position[3] = {0, 0, 0};
        const float forward[3] = {0, 0, -1};
        const float up[3] = {0, 1, 0};
        engine.setListener(position, forward, up);
        voice.setPosition(10.0f, 0.0f, 0.0f);
        voice.setGain(1.0f);
        voice.player().queue(clip.get(), StreamPlayer::Mode::Loop);
        voice.update();
        std::vector<float> placed(4800 * 2);
        engine.render(placed.data(), 4800);
        engine.render(placed.data(), 4800);
        check(peak(placed, 2, 1) > 2.0 * peak(placed, 2, 0), "a sound to the right is heard on the right");
        voice.setPosition(0.0f, 0.0f, -10.0f);
        engine.render(placed.data(), 4800);
        engine.render(placed.data(), 4800);
        check(std::abs(peak(placed, 2, 1) - peak(placed, 2, 0)) < 0.02 && peak(placed, 2, 0) > 0.2,
              "a sound ahead is heard on both sides, its gain kept");
        voice.player().stop();
        voice.update();
        engine.render(placed.data(), 4800);
        check(peak(placed, 2, 0) == 0.0 && peak(placed, 2, 1) == 0.0, "a stopped voice is silent");
    }
    engine.shutdown();
    check(!engine.running(), "the engine shuts down");
}

}

int TsreTests::runSoundSuite(bool verbose) {
    Checker check{"sound", verbose};
    waveCases(check);
    playerCases(check);
    engineCases(check);
    qInfo().noquote() << QString("[tests:sound] passed %1 failed %2").arg(check.passed).arg(check.failed);
    return check.failed == 0 ? 0 : 1;
}

int TsreTests::runSoundCorpus(const QString &root, bool verbose) {
    if (root.isEmpty() || !QFileInfo(root).isDir()) {
        qWarning() << "[tests:sound-corpus] give a content folder with --test-cases";
        return 1;
    }
    int waves = 0;
    int waveFailures = 0;
    QMap<QString, int> reasons;
    QMap<int, int> rates;
    int withPoints = 0;
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (!path.endsWith(".wav", Qt::CaseInsensitive))
            continue;
        ++waves;
        WavData w;
        QString error;
        if (!WavFile::load(path, w, &error)) {
            ++waveFailures;
            reasons[error]++;
            if (verbose)
                qWarning().noquote() << "[tests:sound-corpus] wave" << path << error;
            continue;
        }
        rates[w.sampleRate]++;
        if (!w.cuePoints.empty())
            ++withPoints;
    }
    qInfo().noquote() << QString("[tests:sound-corpus] waves %1 unreadable %2 with loop points %3")
                                 .arg(waves).arg(waveFailures).arg(withPoints);
    for (auto i = rates.cbegin(); i != rates.cend(); ++i)
        qInfo().noquote() << QString("[tests:sound-corpus]   %1 Hz: %2").arg(i.key()).arg(i.value());
    for (auto i = reasons.cbegin(); i != reasons.cend(); ++i)
        qInfo().noquote() << QString("[tests:sound-corpus]   unreadable, %1: %2").arg(i.key()).arg(i.value());
    return waveFailures == 0 ? 0 : 1;
}
