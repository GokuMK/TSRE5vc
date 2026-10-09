/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/tdb/SigCfg.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/fileFunctions/SimisTextReader.h>
#include <tsre/tdb/SignalType.h>
#include <tsre/tdb/SignalShape.h>
#include <tsre/ErrorMessage.h>
#include <tsre/ErrorMessagesLib.h>
#include <tsre/Game.h>
#include <mzip/miniz/miniz.h>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>
#include <algorithm>

namespace {

using Kind = SimisTextReader::Kind;
constexpr qint64 MaxFileBytes = 64 * 1024 * 1024;

QByteArray utf16(const char *text) {
    QByteArray bytes;
    for (const char *c = text; *c; ++c)
        bytes.append(*c).append('\0');
    return bytes;
}

// Compressed SIMIS files (SIMISA@F) become the plain text they hold.
bool inflate(QByteArray &bytes, QString &error) {
    const bool wide = bytes.startsWith(QByteArray("\xff\xfe", 2)) && bytes.mid(2, 16) == utf16("SIMISA@F");
    if (!wide && !bytes.startsWith("SIMISA@F"))
        return true;
    const int header = wide ? 34 : 16;
    if (bytes.size() < header) { error = "Truncated compressed file"; return false; }
    const quint32 expected = qFromLittleEndian<quint32>(bytes.constData() + (wide ? 18 : 8));
    if (expected == 0 || expected > MaxFileBytes) { error = "Invalid compressed size"; return false; }
    QByteArray payload(int(expected), Qt::Uninitialized);
    mz_ulong length = expected;
    if (mz_uncompress(reinterpret_cast<unsigned char*>(payload.data()), &length,
                      reinterpret_cast<const unsigned char*>(bytes.constData() + header),
                      mz_ulong(bytes.size() - header)) != MZ_OK || length != expected) {
        error = "Cannot decompress";
        return false;
    }
    bytes = (wide ? QByteArray("\xff\xfe", 2) + utf16("SIMISA@@@@@@@@@@") : QByteArray("SIMISA@@@@@@@@@@")) + payload;
    return true;
}

// Reads sigcfg.dat blocks. Each reader starts after its block's '(' and
// consumes up to and including its ')'. Unknown blocks and comments (skip,
// comment, names starting with '#' or '_', as Open Rails ignores them) are
// skipped; a value of the wrong kind skips the rest of its block.
class Reader {
public:
    Reader(SimisTextReader &reader, SigCfg &config) : r(reader), cfg(config) {}

    void file() {
        QString block;
        while (nextBlock(block, true)) {
            if (block == "lighttextures") lightTextures();
            else if (block == "lightstab") lightsTable();
            else if (block == "ortssignalfunctions") signalFunctions();
            else if (block == "ortsnormalsubtypes") normalSubtypes();
            else if (block == "signaltypes") signalTypes();
            else if (block == "signalshapes") signalShapes();
            else if (block == "scriptfiles") scriptFiles();
            else unknown(block);
        }
    }

    bool failed() const { return lexFailed; }

private:
    SimisTextReader &r;
    SigCfg &cfg;
    bool lexFailed = false;
    QString context;

    void warn(const QString &message) {
        cfg.warnings << (context.isEmpty() ? message : context + ": " + message);
    }
    static bool comment(const QString &name) {
        return name == "skip" || name == "comment" || name.startsWith('_') || name.startsWith('#');
    }
    // The next child block of the current one, its '(' consumed. False at the
    // current block's end (its ')' consumed) or at the end of the file.
    bool nextBlock(QString &name, bool topLevel = false) {
        for (;;) {
            const Kind kind = r.peekKind();
            if (kind == Kind::Error) { lexFailed = true; return false; }
            if (kind == Kind::End) {
                // The file ends inside a block: it is truncated.
                if (!topLevel) lexFailed = true;
                return false;
            }
            if (kind == Kind::Close) {
                r.next();
                if (topLevel) { warn("Skipped an unmatched ')'"); continue; }
                return false;
            }
            const SimisTextReader::Token token = r.next();
            if (kind == Kind::Open) { skip(); continue; }
            if (kind != Kind::Atom || r.peekKind() != Kind::Open) continue;
            r.next();
            name = token.text.toLower();
            if (comment(name)) { skip(); continue; }
            return true;
        }
    }
    void skip() {
        if (!r.skipBlock()) lexFailed = true;
    }
    void unknown(const QString &block) {
        warn("Skipped unknown block " + block);
        skip();
    }
    // Values. They consume nothing when the next token is not a value, so the
    // caller's skip() still ends at the block's ')'.
    bool word(QString &out) {
        const Kind kind = r.peekKind();
        if (kind != Kind::Atom && kind != Kind::String) return false;
        const SimisTextReader::Token token = r.next();
        out = QString(token.text.constData(), token.text.size());
        return true;
    }
    bool number(float &out) {
        double value;
        if (r.peekKind() != Kind::Atom || !r.readNumber(value)) return false;
        out = float(value);
        return true;
    }
    bool integer(int &out) {
        qint32 value;
        if (r.peekKind() != Kind::Atom || !r.readInteger(value)) return false;
        out = value;
        return true;
    }
    // The words of a flag block, ')' consumed.
    QStringList words() {
        QStringList list;
        QString w;
        while (word(w)) list << w.toLower();
        skip();
        return list;
    }
    float numberBlock(float fallback) {
        float value = fallback;
        if (!number(value)) warn("Expected a number");
        skip();
        return value;
    }
    QString wordBlock() {
        QString value;
        if (!word(value)) warn("Expected a name");
        skip();
        return value;
    }
    // Reads the leading count of a list block; Open Rails warns on lists
    // longer than it, and so does this.
    int count() {
        int n = -1;
        if (!integer(n)) n = -1;
        return n;
    }
    void checkCount(int declared, int read, const QString &what) {
        if (declared >= 0 && read != declared)
            warn(QString("%1 %2 declared, %3 read").arg(declared).arg(what).arg(read));
    }

    void lightTextures() {
        const int declared = count();
        int read = 0;
        QString block;
        while (nextBlock(block)) {
            if (block != "lighttex") { unknown(block); continue; }
            SigCfg::LightTexture t;
            if (!word(t.name) || !word(t.file) || !number(t.u0) || !number(t.v0) || !number(t.u1) || !number(t.v1))
                warn("Incomplete LightTex " + t.name);
            skip();
            t.name = t.name.toLower();
            ++read;
            if (cfg.lightTextures.contains(t.name)) warn("Skipped duplicate LightTex " + t.name);
            else cfg.lightTextures.insert(t.name, t);
        }
        checkCount(declared, read, "LightTex");
    }

    void lightsTable() {
        const int declared = count();
        int read = 0;
        QString block;
        while (nextBlock(block)) {
            if (block != "lightstabentry") { unknown(block); continue; }
            SigCfg::LightColour c;
            if (!word(c.name)) warn("LightsTabEntry without a name");
            c.name = c.name.toLower();
            QString child;
            while (nextBlock(child)) {
                if (child == "colour") {
                    int argb[4] = {255, 255, 255, 255};
                    for (int &v : argb)
                        if (!integer(v)) { warn("Incomplete colour of " + c.name); break; }
                    skip();
                    c.a = uchar(std::clamp(argb[0], 0, 255));
                    c.r = uchar(std::clamp(argb[1], 0, 255));
                    c.g = uchar(std::clamp(argb[2], 0, 255));
                    c.b = uchar(std::clamp(argb[3], 0, 255));
                } else {
                    unknown(child);
                }
            }
            ++read;
            if (cfg.lightsTable.contains(c.name)) warn("Skipped duplicate LightsTabEntry " + c.name);
            else cfg.lightsTable.insert(c.name, c);
        }
        checkCount(declared, read, "LightsTabEntry");
    }

    void signalFunctions() {
        count();
        static const QStringList msts = {"NORMAL", "DISTANCE", "REPEATER", "SHUNTING", "INFO", "SPEED", "ALERT", "UNKNOWN"};
        QString block;
        while (nextBlock(block)) {
            if (block != "ortssignalfunctiontype") { unknown(block); continue; }
            QString name, base = "INFO";
            word(name);
            word(base);
            skip();
            name = name.toUpper();
            base = base.toUpper();
            if (name.isEmpty() || msts.contains(name) || name.startsWith("OR_") || name.startsWith("ORTS"))
                warn("Invalid ORTSSignalFunctionType " + name);
            else if (!msts.contains(base) || base == "NORMAL")
                warn("Invalid MSTS function of ORTSSignalFunctionType " + name);
            else if (cfg.signalFunctions.contains(name))
                warn("Skipped duplicate ORTSSignalFunctionType " + name);
            else
                cfg.signalFunctions.insert(name, base);
        }
    }

    void normalSubtypes() {
        count();
        QString block;
        while (nextBlock(block)) {
            if (block != "ortsnormalsubtype") { unknown(block); continue; }
            const QString name = wordBlock().toUpper();
            if (cfg.normalSubtypes.contains(name)) warn("Skipped duplicate ORTSNormalSubtype " + name);
            else if (!name.isEmpty()) cfg.normalSubtypes << name;
        }
    }

    void scriptFiles() {
        QString block;
        while (nextBlock(block)) {
            if (block != "scriptfile") { unknown(block); continue; }
            const QString name = wordBlock();
            if (!name.isEmpty()) cfg.scriptFiles << name;
        }
    }

    void signalTypes() {
        const int declared = count();
        int read = 0;
        QString block;
        while (nextBlock(block)) {
            if (block != "signaltype") { unknown(block); continue; }
            auto *type = new SignalType();
            if (!word(type->name)) warn("SignalType without a name");
            type->name = type->name.toLower();
            context = "SignalType " + type->name;
            signalType(*type);
            context.clear();
            ++read;
            if (cfg.signalType.contains(type->name)) {
                warn("Skipped duplicate SignalType " + type->name);
                delete type;
            } else {
                cfg.signalType.insert(type->name, type);
            }
        }
        checkCount(declared, read, "SignalType");
    }

    void signalType(SignalType &type) {
        static const QStringList msts = {"NORMAL", "DISTANCE", "REPEATER", "SHUNTING", "INFO", "SPEED", "ALERT", "UNKNOWN"};
        QString block;
        while (nextBlock(block)) {
            if (block == "signalfntype") {
                const QString function = wordBlock().toUpper();
                if (msts.contains(function) || cfg.signalFunctions.contains(function)) {
                    type.function = function;
                } else {
                    warn("Unknown SignalFnType " + function + ", INFO used");
                    type.function = "INFO";
                }
            } else if (block == "signallighttex") {
                type.lightTexture = wordBlock().toLower();
            } else if (block == "signallights") {
                signalLights(type);
            } else if (block == "signaldrawstates") {
                drawStates(type);
            } else if (block == "signalaspects") {
                aspects(type);
            } else if (block == "signalflags") {
                for (const QString &flag : words()) {
                    if (flag == "abs") type.abs = true;
                    else if (flag == "no_gantry") type.noGantry = true;
                    else if (flag == "semaphore") type.semaphore = true;
                    else warn("Unknown SignalType flag " + flag);
                }
            } else if (block == "sigflashduration") {
                if (!number(type.flashTimeOn) || !number(type.flashTimeOff)) warn("Incomplete SigFlashDuration");
                skip();
            } else if (block == "signalnumclearahead") {
                int n = 0;
                if (integer(n)) type.numClearAhead << n;
                else warn("Expected a number");
                skip();
            } else if (block == "semaphoreinfo") {
                type.semaphoreInfo = numberBlock(type.semaphoreInfo);
            } else if (block == "ortsdayglow") {
                type.dayGlow = numberBlock(type.dayGlow);
            } else if (block == "ortsnightglow") {
                type.nightGlow = numberBlock(type.nightGlow);
            } else if (block == "ortsdaylight") {
                const QString value = wordBlock().toLower();
                type.dayLight = !(value == "false" || value == "0");
            } else if (block == "ortsnormalsubtype") {
                const QString subtype = wordBlock().toUpper();
                if (cfg.normalSubtypes.contains(subtype)) type.normalSubtype = subtype;
                else warn("Unknown ORTSNormalSubtype " + subtype);
            } else if (block == "ortsonofftimes") {
                type.onOffTime = numberBlock(type.onOffTime);
            } else if (block == "ortsscript") {
                type.script = wordBlock().toLower();
            } else if (block == "ortsreqstopvisdistance") {
                type.reqStopVisDistance = numberBlock(type.reqStopVisDistance);
            } else if (block == "ortsreqstopanndistance") {
                type.reqStopAnnDistance = numberBlock(type.reqStopAnnDistance);
            } else if (block == "approachcontrolsettings") {
                approachControl(type);
            } else {
                unknown(block);
            }
        }
    }

    void signalLights(SignalType &type) {
        const int declared = count();
        QString block;
        while (nextBlock(block)) {
            if (block != "signallight") { unknown(block); continue; }
            SignalType::Light light;
            if (!integer(light.index) || !word(light.name)) warn("Incomplete SignalLight");
            light.name = light.name.toLower();
            QString child;
            while (nextBlock(child)) {
                if (child == "position") {
                    if (!number(light.position[0]) || !number(light.position[1]) || !number(light.position[2]))
                        warn("Incomplete Position of light " + light.name);
                    skip();
                } else if (child == "radius") {
                    light.radius = numberBlock(light.radius);
                } else if (child == "signalflags") {
                    for (const QString &flag : words()) {
                        if (flag == "semaphore_change") light.semaphoreChange = true;
                        else warn("Unknown SignalLight flag " + flag);
                    }
                } else if (child == "ortssignallighttex") {
                    light.lightTexture = wordBlock().toLower();
                } else {
                    unknown(child);
                }
            }
            if (type.light(light.index) != nullptr) warn(QString("Skipped duplicate SignalLight %1").arg(light.index));
            else type.lights << light;
        }
        std::sort(type.lights.begin(), type.lights.end(),
                  [](const SignalType::Light &a, const SignalType::Light &b) { return a.index < b.index; });
        for (int i = 0; i < type.lights.size(); ++i)
            if (type.lights[i].index != i)
                warn(QString("SignalLight index %1 where %2 was expected").arg(type.lights[i].index).arg(i));
        checkCount(declared, type.lights.size(), "SignalLight");
    }

    void drawStates(SignalType &type) {
        const int declared = count();
        int read = 0;
        QString block;
        while (nextBlock(block)) {
            if (block != "signaldrawstate") { unknown(block); continue; }
            SignalType::DrawState state;
            if (!integer(state.index) || !word(state.name)) warn("Incomplete SignalDrawState");
            state.name = state.name.toLower();
            QString child;
            while (nextBlock(child)) {
                if (child == "drawlights") {
                    count();
                    QString item;
                    while (nextBlock(item)) {
                        if (item != "drawlight") { unknown(item); continue; }
                        SignalType::DrawLight draw;
                        if (!integer(draw.light)) warn("DrawLight without a light index");
                        QString flags;
                        while (nextBlock(flags)) {
                            if (flags != "signalflags") { unknown(flags); continue; }
                            for (const QString &flag : words()) {
                                if (flag == "flashing") draw.flashing = true;
                                else warn("Unknown DrawLight flag " + flag);
                            }
                        }
                        state.lights << draw;
                    }
                } else if (child == "semaphorepos") {
                    state.semaphorePos = numberBlock(0.0f);
                } else {
                    unknown(child);
                }
            }
            ++read;
            // Open Rails keeps a duplicate name under a generated one.
            if (type.drawState(state.name) != nullptr) {
                const QString renamed = QString("dst%1").arg(type.drawStates.size());
                warn("Duplicate SignalDrawState " + state.name + ", named " + renamed);
                state.name = renamed;
            }
            type.drawStates << state;
        }
        checkCount(declared, read, "SignalDrawState");
    }

    void aspects(SignalType &type) {
        count();
        QString block;
        while (nextBlock(block)) {
            if (block != "signalaspect") { unknown(block); continue; }
            SignalType::AspectEntry entry;
            QString aspect;
            if (!word(aspect) || !word(entry.drawState)) warn("Incomplete SignalAspect");
            entry.aspect = SignalType::aspectFromName(aspect);
            if (entry.aspect == SignalType::UNKNOWN_ASPECT) warn("Unknown signal aspect " + aspect);
            entry.drawState = entry.drawState.toLower();
            QString child;
            while (nextBlock(child)) {
                if (child == "speedmph") {
                    entry.speedMpS = numberBlock(0.0f) * 0.44704f;
                } else if (child == "speedkph") {
                    entry.speedMpS = numberBlock(0.0f) / 3.6f;
                } else if (child == "signalflags") {
                    for (const QString &flag : words()) {
                        if (flag == "asap") entry.asap = true;
                        else if (flag == "or_speedreset") entry.speedReset = true;
                        else if (flag == "or_nospeedreduction") entry.noSpeedReduction = true;
                        else warn("Unknown SignalAspect flag " + flag);
                    }
                } else {
                    unknown(child);
                }
            }
            bool duplicate = false;
            for (const SignalType::AspectEntry &other : type.aspects)
                duplicate = duplicate || (entry.aspect != SignalType::UNKNOWN_ASPECT && other.aspect == entry.aspect);
            if (duplicate) warn("Skipped duplicate SignalAspect " + aspect);
            else type.aspects << entry;
        }
    }

    void approachControl(SignalType &type) {
        QString block;
        while (nextBlock(block)) {
            if (block == "positionmiles") type.approachControlPositionM = numberBlock(0.0f) * 1609.344f;
            else if (block == "positionkm") type.approachControlPositionM = numberBlock(0.0f) * 1000.0f;
            else if (block == "positionm") type.approachControlPositionM = numberBlock(0.0f);
            else if (block == "positionyd") type.approachControlPositionM = numberBlock(0.0f) * 0.9144f;
            else if (block == "speedmph") type.approachControlSpeedMpS = numberBlock(0.0f) * 0.44704f;
            else if (block == "speedkph") type.approachControlSpeedMpS = numberBlock(0.0f) / 3.6f;
            else if (block == "speedmps") type.approachControlSpeedMpS = numberBlock(0.0f);
            else unknown(block);
        }
    }

    void signalShapes() {
        const int declared = count();
        int read = 0;
        QString block;
        while (nextBlock(block)) {
            if (block != "signalshape") { unknown(block); continue; }
            auto *shape = new SignalShape();
            if (!word(shape->name)) warn("SignalShape without a file name");
            word(shape->desc);
            context = "SignalShape " + shape->name;
            QString child;
            while (nextBlock(child)) {
                if (child == "signalsubobjs") subObjects(*shape);
                else unknown(child);
            }
            context.clear();
            ++read;
            const QString key = shape->name.toLower();
            if (shape->name.isEmpty() || cfg.signalShape.contains(key)) {
                warn("Skipped duplicate SignalShape " + shape->name);
                delete shape;
                continue;
            }
            shape->listId = cfg.signalShapeById.size();
            cfg.signalShape.insert(key, shape);
            cfg.signalShapeById.insert(shape->listId, shape);
        }
        checkCount(declared, read, "SignalShape");
    }

    void subObjects(SignalShape &shape) {
        int declared = count();
        if (declared < 0 || declared > 32) {
            warn(QString("SignalSubObjs count %1 outside 0 to 32").arg(declared));
            skip();
            return;
        }
        shape.iSubObj = declared;
        shape.subObj = new SignalShape::SubObj[declared];
        QVector<bool> seen(declared, false);
        int front = 0, back = 0;
        QString block;
        while (nextBlock(block)) {
            if (block != "signalsubobj") { unknown(block); continue; }
            int index = -1;
            SignalShape::SubObj sub;
            if (!integer(index) || !word(sub.type)) warn("Incomplete SignalSubObj");
            word(sub.desc);
            QString child;
            while (nextBlock(child)) {
                if (child == "sigsubtype") {
                    sub.sigSubType = wordBlock().toUpper();
                    sub.sigSubTypeId = SignalShape::SigSubTypeStringToId.value(sub.sigSubType, SignalShape::UNDEFINED);
                } else if (child == "sigsubstype") {
                    sub.sigSubSType = wordBlock();
                } else if (child == "signalflags") {
                    for (const QString &flag : words()) {
                        if (flag == "jn_link") sub.isJnLink = true;
                        else if (flag == "default") sub.defaultt = true;
                        else if (flag == "optional") sub.optional = true;
                        else if (flag == "back_facing") sub.backFacing = true;
                        else warn("Unknown SignalSubObj flag " + flag);
                    }
                } else if (child == "sigsubjnlinkif") {
                    QVector<int> links;
                    int n = count(), link = 0;
                    while (integer(link)) links << link;
                    skip();
                    if (n >= 0 && n != links.size()) warn("SigSubJnLinkIf count differs from its links");
                    delete[] sub.sigSubJnLinkIf;
                    sub.iLink = links.size();
                    sub.sigSubJnLinkIf = new int[std::max(1, int(links.size()))];
                    std::copy(links.begin(), links.end(), sub.sigSubJnLinkIf);
                } else {
                    unknown(child);
                }
            }
            if (index < 0 || index >= declared || seen[index]) {
                warn(QString("Skipped SignalSubObj with index %1").arg(index));
                delete[] sub.sigSubJnLinkIf;
                continue;
            }
            seen[index] = true;
            sub.faceidx = sub.backFacing ? back++ : front++;
            shape.subObj[index] = sub;
        }
        for (int i = 0; i < declared; ++i)
            if (!seen[i]) warn(QString("Missing SignalSubObj %1").arg(i));
    }
};

}

SignalShape* SigCfg::findSignalShape(const QString &fileName) const {
    // Signal names are logical references; retain authored spelling for I/O.
    return signalShape.value(fileName.toLower(), nullptr);
}

const SignalType* SigCfg::findSignalType(const QString &name) const {
    return signalType.value(name.toLower(), nullptr);
}

SigCfg::SigCfg() {
    load(ContentPath::normalize(Game::root + "/ROUTES/" + Game::route + "/sigcfg.dat"));
}

SigCfg::SigCfg(const QString &path) {
    load(path);
}

void SigCfg::load(const QString &path) {
    qDebug() << path;
    QFile file(path);
    sourceFileExists = file.exists();
    if(!sourceFileExists) {
        qDebug() << "sigcfg.dat does not exist; using an empty signal configuration";
        loaded = true;
        return;
    }
    auto reportLoadFailure = [&](const QString &reason) {
        qWarning() << "Failed to load existing sigcfg.dat" << path << reason;
        ErrorMessagesLib::PushErrorMessage(new ErrorMessage(
            ErrorMessage::Type_Error,
            ErrorMessage::Source_TDB,
            QString("Failed to load existing signal configuration: %1").arg(path),
            reason
        ));
    };
    if(!file.open(QIODevice::ReadOnly)) {
        reportLoadFailure(file.errorString());
        return;
    }
    if(file.size() > MaxFileBytes) {
        reportLoadFailure("File is too large for a signal configuration.");
        return;
    }
    QByteArray bytes = file.readAll();
    file.close();
    QString text, error;
    if(!inflate(bytes, error) || !SimisTextReader::decode(bytes, text, error)) {
        reportLoadFailure(error);
        return;
    }
    SimisTextReader reader(text);
    const SimisTextReader::Token header = reader.next();
    if(header.kind != Kind::Atom || !header.text.startsWith("SIMISA", Qt::CaseInsensitive)) {
        reportLoadFailure("Missing SIMIS header.");
        return;
    }
    Reader sigcfg(reader, *this);
    sigcfg.file();
    qDebug() << "sigcfg:" << signalType.size() << "signal types," << signalShape.size() << "signal shapes,"
             << lightsTable.size() << "lights," << lightTextures.size() << "light textures,"
             << warnings.size() << "warnings";
    const int logged = std::min<int>(warnings.size(), 20);
    for(int i = 0; i < logged; ++i)
        qDebug() << "sigcfg:" << warnings[i];
    if(warnings.size() > logged)
        qDebug() << "sigcfg:" << warnings.size() - logged << "more warnings";
    // A truncated file, or one with an unterminated string, is not a valid
    // configuration (the definitions read before the fault stay usable).
    if(sigcfg.failed()) {
        QString reason = "The file is truncated or has an unterminated string.";
        for(const SimisTextReader::Diagnostic &d : reader.diagnostics())
            reason += QString(" Line %1, column %2: %3.").arg(d.line).arg(d.column).arg(d.message);
        reportLoadFailure(reason);
        return;
    }
    loaded = true;
}

SigCfg::~SigCfg() {
    qDeleteAll(signalShape);
    qDeleteAll(signalType);
}
