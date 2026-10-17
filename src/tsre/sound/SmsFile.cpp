/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/sound/SmsFile.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/fileFunctions/SimisTextReader.h>
#include <tsre/units/UnitConversion.h>

#include <QDebug>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <cmath>

namespace {

using namespace Sms;
using Kind = SimisTextReader::Kind;

// The quantity of a control's values, the units its numbers may have (task
// core si-units): SI, but brake pressures in psi, as MSTS's sounds compare
// them; a number without a unit is in those already.
Units::Quantity unitsOf(Control control) {
    switch (control) {
    case Control::Distance:
    case Control::CarDistanceTrack:
    case Control::CarTunnelDistance:
        return Units::Quantity::Distance;
    case Control::Speed:
        return Units::Quantity::Speed;
    case Control::BrakeCyl:
    case Control::BackPressure:
        return Units::Quantity::Pressure;
    case Control::TractiveEffort:
    case Control::CurveForce:
        return Units::Quantity::Force;
    case Control::TractivePower:
    case Control::EnginePower:
        return Units::Quantity::Power;
    default:
        return Units::Quantity::Any;
    }
}

// A token's text as a string of its own (atoms borrow the reader's text).
QString own(const SimisTextReader::Token &token) {
    return QString(token.text.constData(), token.text.size());
}

// The .sms grammar on TSRE's SIMIS text reader.
class Parser {
public:
    Parser(const QString &text, File &file) : reader(text), file(file) {}

    void run() {
        for (;;) {
            const Kind kind = reader.peekKind();
            if (kind == Kind::End || kind == Kind::Error)
                return;
            const SimisTextReader::Token token = reader.next();
            if (token.kind == Kind::Open) {
                reader.skipBlock();
            } else if (token.kind == Kind::Atom && token.text.compare(QLatin1String("tr_sms"), Qt::CaseInsensitive) == 0) {
                found = true;
                if (open())
                    block([this](const QString &t) {
                        if (t != "scalabiltygroup")
                            return false;
                        file.groups.push_back(group());
                        return true;
                    });
            }
        }
    }

    // A Tr_SMS block was there (it may be empty).
    bool found = false;

private:
    void warn(const QString &message) {
        if (file.warnings.size() < 100)
            file.warnings.append(message);
    }

    bool open() {
        if (reader.peekKind() != Kind::Open)
            return false;
        reader.next();
        return true;
    }

    // Inside a block: past its ")".
    void skipRest() {
        reader.skipBlock();
    }

    // After a token: its block, if it has one.
    void skipBlockAfter() {
        if (open())
            reader.skipBlock();
    }

    // The block's ")" next (taken), or the end of the text.
    bool closing() {
        const Kind kind = reader.peekKind();
        if (kind == Kind::Close) {
            reader.next();
            return true;
        }
        return kind == Kind::End || kind == Kind::Error;
    }

    QString word() {
        const SimisTextReader::Token token = reader.next();
        return token.kind == Kind::Atom || token.kind == Kind::String ? own(token) : QString();
    }

    // The tokens of the block just opened, to its ")": handle() takes a
    // lower-case token and reads its block; others are skipped with theirs
    // (comments, Skip blocks, tokens of other programs).
    template <class Handle>
    void block(Handle handle) {
        for (;;) {
            const Kind kind = reader.peekKind();
            if (kind == Kind::End || kind == Kind::Error)
                return;
            if (kind == Kind::Close) {
                reader.next();
                return;
            }
            if (kind == Kind::Open) {
                reader.next();
                reader.skipBlock();
                continue;
            }
            const QString lower = word().toLower();
            if (!handle(lower)) {
                if (lower != "comment" && lower != "skip" && !lower.startsWith('_') && !lower.startsWith('#'))
                    warn("skipped " + lower);
                skipBlockAfter();
            }
        }
    }

    // A number with an optional unit suffix ("50m", "30mph").
    float number(Units::Quantity units, float fallback, bool *ok = nullptr) {
        if (ok != nullptr)
            *ok = false;
        if (reader.peekKind() != Kind::Atom)
            return fallback;
        const QString token = own(reader.peek());
        static const QRegularExpression pattern("^([-+]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][-+]?\\d+)?)(.*)$");
        const QRegularExpressionMatch match = pattern.match(token);
        SimisTextReader::Token digits;
        digits.kind = Kind::Atom;
        digits.text = match.captured(1);
        double value = 0.0;
        if (!match.hasMatch() || !SimisTextReader::number(digits, value))
            return fallback;
        reader.next();
        // MSTS lists numbers with commas too ("0.0, 0.4,").
        QString suffix = match.captured(2);
        while (suffix.startsWith(','))
            suffix.remove(0, 1);
        if (!suffix.isEmpty()) {
            double si = value;
            if (units == Units::Quantity::Any || !Units::fromFile(value, suffix, units, si))
                warn("unknown unit " + token);
            else if (units == Units::Quantity::Pressure)
                Units::toFile(si, u"psi", units, value);
            else
                value = si;
        }
        if (ok != nullptr)
            *ok = true;
        return float(value);
    }

    int integer(int fallback) {
        bool ok = false;
        const float value = number(Units::Quantity::Any, float(fallback), &ok);
        return ok ? int(std::lround(value)) : fallback;
    }

    float floatBlock(Units::Quantity units, float fallback) {
        if (!open())
            return fallback;
        const float value = number(units, fallback);
        skipRest();
        return value;
    }

    int intBlock(int fallback) {
        if (!open())
            return fallback;
        const int value = integer(fallback);
        skipRest();
        return value;
    }

    // "Stereo ( )" is true, "( 0 )" or "( false )" false.
    bool boolBlock() {
        if (!open())
            return true;
        bool value = true;
        if (reader.peekKind() == Kind::Atom) {
            const QString token = own(reader.peek()).toLower();
            value = !(token == "0" || token == "false");
        }
        skipRest();
        return value;
    }

    Group group() {
        Group g;
        if (!open())
            return g;
        g.detailLevel = integer(0);
        block([this, &g](const QString &t) {
            if (t == "activation") {
                g.activation = activation();
            } else if (t == "deactivation") {
                g.deactivation = activation();
            } else if (t == "streams") {
                g.hasStreams = true;
                streams(g.streams);
            } else if (t == "volume") {
                g.volume = floatBlock(Units::Quantity::Any, 1.0f);
            } else if (t == "stereo") {
                g.stereo = boolBlock();
            } else if (t == "ignore3d") {
                g.ignore3d = boolBlock();
            } else if (t == "loadallwaves") {
                // MSTS's preloading: all waves are read when first played.
                skipBlockAfter();
            } else {
                return false;
            }
            return true;
        });
        return g;
    }

    Activation activation() {
        Activation a;
        if (!open())
            return a;
        a.present = true;
        block([this, &a](const QString &t) {
            if (t == "externalcam")
                a.externalCam = boolBlock();
            else if (t == "cabcam")
                a.cabCam = boolBlock();
            else if (t == "passengercam")
                a.passengerCam = boolBlock();
            else if (t == "distance")
                a.distance = floatBlock(Units::Quantity::Distance, a.distance);
            else if (t == "tracktype")
                a.trackType = intBlock(-1);
            else
                return false;
            return true;
        });
        return a;
    }

    void streams(std::vector<Stream> &out) {
        if (!open())
            return;
        int count = integer(0);
        block([this, &out, &count](const QString &t) {
            if (t != "stream")
                return false;
            // MSTS takes as many streams as the count says.
            if (--count < 0) {
                warn("skipped an extra stream");
                skipBlockAfter();
            } else {
                out.push_back(stream());
            }
            return true;
        });
        if (count > 0)
            warn(QString("%1 streams missing").arg(count));
    }

    Stream stream() {
        Stream s;
        if (!open())
            return s;
        block([this, &s](const QString &t) {
            if (t == "priority") {
                s.priority = intBlock(0);
            } else if (t == "triggers") {
                triggers(s.triggers);
            } else if (t == "volumecurve") {
                s.volumeCurves.push_back(curve());
            } else if (t == "frequencycurve") {
                s.frequencyCurve = curve();
            } else if (t == "volume") {
                s.volume = floatBlock(Units::Quantity::Any, s.volume);
            } else if (t == "ortstimeofday") {
                if (open()) {
                    const int from = integer(0);
                    const int to = integer(24);
                    s.hours.push_back({from, to});
                    skipRest();
                }
            } else if (t == "ortsseason" || t == "ortsweather") {
                const bool season = t == "ortsseason";
                int &mask = season ? s.seasons : s.weathers;
                mask = 0;
                const QStringList names = season ? QStringList{"spring", "summer", "autumn", "winter"}
                                                 : QStringList{"clear", "snow", "rain"};
                if (open())
                    block([this, &mask, &names](const QString &n) {
                        const int bit = int(names.indexOf(n));
                        if (bit < 0)
                            return false;
                        if (boolBlock())
                            mask |= 1 << bit;
                        return true;
                    });
            } else {
                return false;
            }
            return true;
        });
        return s;
    }

    Curve curve() {
        Curve c;
        if (!open())
            return c;
        const QString type = word().toLower();
        if (type.endsWith("controlled"))
            c.control = controlFromName(type.left(type.size() - 10), &c.index);
        if (c.control == Control::None) {
            warn("unknown curve " + type);
            skipRest();
            return c;
        }
        block([this, &c](const QString &t) {
            if (t == "granularity") {
                c.granularity = floatBlock(Units::Quantity::Any, 1.0f);
            } else if (t == "curvepoints") {
                if (!open())
                    return true;
                const int count = integer(0);
                const Units::Quantity units = unitsOf(c.control);
                for (int i = 0; i < count; ++i) {
                    bool okX = false;
                    bool okY = false;
                    float x = number(units, 0.0f, &okX);
                    const float y = number(Units::Quantity::Any, 0.0f, &okY);
                    if (!okX || !okY) {
                        warn("curve points missing");
                        break;
                    }
                    // Distance is compared squared, keeping its sign.
                    if (c.control == Control::Distance)
                        x = x >= 0.0f ? x * x : -x * x;
                    c.points.push_back({x, y});
                }
                skipRest();
            } else {
                return false;
            }
            return true;
        });
        return c;
    }

    void triggers(std::vector<Trigger> &out) {
        if (!open())
            return;
        integer(0);
        block([this, &out](const QString &t) {
            static const QHash<QString, Trigger::Type> types = {
                    {"initial_trigger", Trigger::Type::Initial},
                    {"discrete_trigger", Trigger::Type::Discrete},
                    {"variable_trigger", Trigger::Type::Variable},
                    {"random_trigger", Trigger::Type::Random},
                    {"dist_travelled_trigger", Trigger::Type::DistanceTravelled}};
            Trigger trigger;
            if (types.contains(t)) {
                trigger.type = types.value(t);
            } else {
                // joint_2axle_trigger, switch_4axle_trigger, xover_8axle_trigger...
                static const QRegularExpression axles("^(joint|switch|xover)_(\\d)axle_trigger$");
                const QRegularExpressionMatch match = axles.match(t);
                if (!match.hasMatch())
                    return false;
                const QString kind = match.captured(1);
                trigger.type = kind == "joint" ? Trigger::Type::Joint
                        : kind == "switch" ? Trigger::Type::Switch : Trigger::Type::Crossover;
                trigger.axles = match.captured(2).toInt();
            }
            out.push_back(readTrigger(trigger));
            return true;
        });
    }

    Trigger readTrigger(Trigger t) {
        if (!open())
            return t;
        if (t.type == Trigger::Type::Discrete)
            t.event = integer(0);
        if (t.type == Trigger::Type::Variable) {
            const QString name = word().toLower();
            QString variable;
            if (name.endsWith("_inc_past")) {
                t.increasing = true;
                variable = name.left(name.size() - 9);
            } else if (name.endsWith("_dec_past")) {
                t.increasing = false;
                variable = name.left(name.size() - 9);
            }
            t.control = controlFromName(variable, &t.index);
            if (t.control == Control::None) {
                warn("unknown variable trigger " + name);
                t.type = Trigger::Type::Unknown;
                skipRest();
                return t;
            }
            t.threshold = number(unitsOf(t.control), 0.0f);
            if (t.control == Control::Distance)
                t.threshold *= t.threshold;
        }
        int commands = 0;
        while (!closing()) {
            if (reader.peekKind() == Kind::Open) {
                reader.next();
                skipRest();
                continue;
            }
            const QString lower = word().toLower();
            if (lower == "delay_min_max" || lower == "dist_min_max" || lower == "volume_min_max") {
                const Units::Quantity units = lower == "dist_min_max" ? Units::Quantity::Distance : Units::Quantity::Any;
                if (open()) {
                    const float a = number(units, 0.0f);
                    const float b = number(units, a);
                    if (lower == "volume_min_max") {
                        t.volumeMin = a;
                        t.volumeMax = b;
                    } else {
                        t.min = a;
                        t.max = b;
                    }
                    skipRest();
                }
            } else if (lower == "car_camera_dist") {
                t.cameraDistance = floatBlock(Units::Quantity::Distance, t.cameraDistance);
            } else if (command(lower, t.command)) {
                if (++commands > 1)
                    warn("a trigger's command replaced");
            } else {
                skipBlockAfter();
            }
        }
        if (t.command.type == Command::Type::None)
            warn("a trigger without a command");
        return t;
    }

    bool command(const QString &t, Command &c) {
        static const QHash<QString, Command::Type> types = {
                {"playoneshot", Command::Type::PlayOneShot},
                {"startloop", Command::Type::StartLoop},
                {"startlooprelease", Command::Type::StartLoopRelease},
                {"releaselooprelease", Command::Type::ReleaseLoopRelease},
                {"releaseloopreleasewithjump", Command::Type::ReleaseLoopReleaseWithJump},
                {"enabletrigger", Command::Type::EnableTrigger},
                {"disabletrigger", Command::Type::DisableTrigger},
                {"setstreamvolume", Command::Type::SetStreamVolume}};
        if (!types.contains(t))
            return false;
        c = Command();
        c.type = types.value(t);
        if (!open())
            return true;
        switch (c.type) {
        case Command::Type::PlayOneShot:
        case Command::Type::StartLoop:
        case Command::Type::StartLoopRelease: {
            const int count = integer(0);
            block([this, &c, count](const QString &n) {
                if (n == "file") {
                    if (!open())
                        return true;
                    const QString name = closing() ? QString() : word();
                    // MSTS takes as many files as the count says.
                    if (c.files.size() < count && !name.isEmpty())
                        c.files.append(name);
                    else
                        warn("skipped an extra file");
                    if (!name.isEmpty())
                        skipRest();
                } else if (n == "selectionmethod") {
                    if (!open())
                        return true;
                    if (closing())
                        return true;
                    const QString method = word().toLower();
                    if (method == "randomselection")
                        c.randomSelection = true;
                    else if (method != "sequentialselection")
                        warn("unknown selection method " + method);
                    skipRest();
                } else {
                    return false;
                }
                return true;
            });
            break;
        }
        case Command::Type::EnableTrigger:
        case Command::Type::DisableTrigger:
            c.trigger = integer(0);
            skipRest();
            break;
        case Command::Type::SetStreamVolume:
            c.volume = number(Units::Quantity::Any, 1.0f);
            skipRest();
            break;
        default:
            skipRest();
            break;
        }
        return true;
    }

    SimisTextReader reader;
    File &file;
};

}

Control Sms::controlFromName(const QString &name, int *index) {
    static const QHash<QString, Control> names = {
            {"distance", Control::Distance}, {"speed", Control::Speed}, {"variable1", Control::Variable1},
            {"variable2", Control::Variable2}, {"variable2booster", Control::Variable2Booster},
            {"variable3", Control::Variable3}, {"brakecyl", Control::BrakeCyl},
            {"curveforce", Control::CurveForce}, {"angleofattack", Control::AngleOfAttack},
            {"carfriction", Control::CarFriction}, {"wheelrpm", Control::WheelRpm},
            {"concretesleepers", Control::ConcreteSleepers}, {"carintunnel", Control::CarInTunnel},
            {"cardistancetrack", Control::CarDistanceTrack}, {"cartunneldistance", Control::CarTunnelDistance},
            {"backpressure", Control::BackPressure}, {"tractiveeffort", Control::TractiveEffort},
            {"tractivepower", Control::TractivePower}};
    const QString lower = name.toLower();
    if (index != nullptr)
        *index = 0;
    if (names.contains(lower))
        return names.value(lower);
    // Variable1_<n> and Engine<n>RPM / Power / Torque, 1 based in the file.
    auto numbered = [&](const QString &middle) {
        bool ok = false;
        const int n = middle.toInt(&ok);
        if (index != nullptr)
            *index = ok ? std::max(n - 1, 0) : 0;
    };
    if (lower.startsWith("variable1_")) {
        numbered(lower.mid(10));
        return Control::Variable1;
    }
    if (lower.startsWith("engine")) {
        for (const auto &suffix : {std::make_pair(QString("rpm"), Control::EngineRpm),
                                   std::make_pair(QString("power"), Control::EnginePower),
                                   std::make_pair(QString("torque"), Control::EngineTorque)}) {
            if (lower.endsWith(suffix.first)) {
                numbered(lower.mid(6, lower.size() - 6 - suffix.first.size()));
                return suffix.second;
            }
        }
    }
    return Control::None;
}

float Sms::Curve::valueAt(float x) const {
    if (points.empty())
        return 1.0f;
    if (points.size() < 2 || x < points.front().first)
        return points.front().second;
    if (x > points.back().first)
        return points.back().second;
    size_t i = 1;
    while (i < points.size() - 1 && points[i].first < x)
        ++i;
    const float span = points[i].first - points[i - 1].first;
    if (span == 0.0f)
        return points[i].second;
    const float r = (x - points[i - 1].first) / span;
    return points[i - 1].second + r * (points[i].second - points[i - 1].second);
}

const Sms::Group *Sms::File::group(int detailLevel) const {
    for (const Group &g : groups)
        if (g.detailLevel <= detailLevel)
            return g.hasStreams ? &g : nullptr;
    return nullptr;
}

bool Sms::parse(const QString &text, File &out) {
    Parser parser(text, out);
    parser.run();
    return parser.found;
}

bool Sms::load(const QString &path, File &out, QString *error) {
    out = File();
    out.path = path;
    QFile file(ContentPath::normalize(path));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr)
            *error = "cannot open " + path;
        return false;
    }
    QByteArray bytes = file.readAll();
    // UTF-16 without its byte order mark.
    if (bytes.size() >= 2 && bytes[0] != 0 && bytes[1] == 0 && !bytes.startsWith("\xff\xfe"))
        bytes.prepend("\xff\xfe");
    QString text;
    QString decodeError;
    if (!SimisTextReader::decode(bytes, text, decodeError)) {
        if (error != nullptr)
            *error = decodeError + " in " + path;
        return false;
    }
    // Compressed: TSRE's file reader inflates it.
    if (text.startsWith("SIMISA@F") || text.mid(1).startsWith("SIMISA@F")) {
        file.seek(0);
        std::unique_ptr<FileBuffer> buffer(ReadFile::read(&file));
        if (buffer == nullptr) {
            if (error != nullptr)
                *error = "cannot inflate " + path;
            return false;
        }
        buffer->toUtf16();
        buffer->skipBOM();
        text = QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer->data + buffer->off),
                                  (buffer->length - buffer->off) / 2);
    }
    if (!parse(text, out)) {
        if (error != nullptr)
            *error = "no Tr_SMS in " + path;
        return false;
    }
    return true;
}

namespace {
QHash<QString, std::shared_ptr<const Sms::File>> &sharedFiles() {
    static QHash<QString, std::shared_ptr<const Sms::File>> files;
    return files;
}
}

std::shared_ptr<const Sms::File> Sms::shared(const QString &path) {
    const QString key = ContentPath::key(path);
    auto found = sharedFiles().constFind(key);
    if (found != sharedFiles().constEnd())
        return found.value();
    auto file = std::make_shared<File>();
    QString error;
    if (!load(path, *file, &error)) {
        qWarning().noquote() << "Sound:" << error;
        file.reset();
    }
    sharedFiles().insert(key, file);
    return file;
}

void Sms::clearShared() {
    sharedFiles().clear();
}
