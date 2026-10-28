#include <tsre/trains/VehicleDefinition.h>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <algorithm>
#include <cmath>
#include <tsre/trains/EngLights.h>
#include <tsre/trains/VehicleKeys.h>
#include <tsre/units/UnitConversion.h>

using Item = SimisDocument::Item;
using Units::Quantity;

namespace {

// The lights blocks read (main thread only), by the tree and node of their
// block, and the vehicle's folder where an include inside goes through
// [[Vehicle]]: vehicles whose Lights come from one include share one,
// written, compressed and parsed once. A block lives as long as a definition
// holding it, and so its tree (whose address is then not reused); an expired
// entry is made again.
QHash<QString, std::weak_ptr<EngLightsBlock>> lightsBlocks;

std::shared_ptr<EngLightsBlock> lightsOf(const SimisDocument &doc, SimisDocument::Item block, const QString &folder) {
    const QString key = QString("%1:%2:%3")
                                .arg(quintptr(block.tree))
                                .arg(block.node)
                                .arg(doc.readsVehicleRoot(block) ? folder : QString());
    if (auto shared = lightsBlocks.value(key).lock())
        return shared;
    auto made = std::make_shared<EngLightsBlock>(doc.write(block));
    lightsBlocks.insert(key, made);
    return made;
}

constexpr double PoundForce = 4.44822162;
constexpr double Mile = 0.44704;

// Reads the typed fields from the document, in file order as Open Rails
// does: a key given again replaces the earlier value.
struct Reader {
    const SimisDocument &doc;
    VehicleDefinition &def;
    QString game;
    std::shared_ptr<VehicleDefinition::Cab3D> cab3D;
    QStringList cab3DFiles;

    // A sound: the vehicle's SOUND folder, then the game's.
    QString sound(const QString &written, const QString &holder) {
        const QString path = def.file(written, holder, {def.folder + "/SOUND", game + "/SOUND"}, game);
        return QFileInfo(path).isFile() ? path : QString();
    }

    QString string(Item block) const { return doc.text(doc.values(block).value(0)).toString(); }
    // The file where a block's first value was written.
    QString holder(Item block) const {
        const Item value = doc.values(block).value(0);
        return value.tree ? doc.file(value) : doc.file(block);
    }
    // The value at `index` of a block in SI; 0 and false when missing.
    double number(Item block, int index, Quantity quantity, bool *ok = nullptr) const {
        double si = 0.0;
        const bool read = doc.number(doc.values(block).value(index), quantity, si);
        if (ok)
            *ok = read;
        return read ? si : 0.0;
    }
    void cab3d(Item block) {
        // Open Rails' MSTSWagon.Parse3DCab: a shape file and a viewpoint.
        VehicleDefinition::Cab3D::Viewpoint viewpoint;
        QString file;
        for (const Item &item : doc.items(block)) {
            if (doc.is(item, "orts3dcabfile"))
                file = string(item);
            else if (doc.is(item, "orts3dcabheadpos"))
                for (int k = 0; k < 3; ++k)
                    viewpoint.position[k] = float(number(item, k, Quantity::Length));
            else if (doc.is(item, "rotationlimit"))
                for (int k = 0; k < 3; ++k)
                    viewpoint.limit[k] = float(number(item, k, Quantity::Any));
            else if (doc.is(item, "startdirection"))
                for (int k = 0; k < 3; ++k)
                    viewpoint.start[k] = float(number(item, k, Quantity::Any));
        }
        if (!file.trimmed().isEmpty()) {
            cab3D->shapes << file.trimmed();
            cab3DFiles << def.file(file.trimmed(), holder(block), {def.folder + "/CABVIEW3D"}, game);
        }
        cab3D->viewpoints << viewpoint;
    }
    void coupling(Item block) {
        VehicleDefinition::Coupling c;
        for (const Item &item : doc.items(block)) {
            if (doc.is(item, "type"))
                c.type = string(item);
            else if (doc.is(item, "velocity"))
                c.velocity = float(number(item, 0, Quantity::Any));
            else if (doc.is(item, "spring"))
                for (const Item &spring : doc.items(item))
                    if (doc.is(spring, "r0"))
                        for (int k = 0; k < 2; ++k)
                            c.r0[k] = float(number(spring, k, Quantity::Any));
        }
        def.couplings << c;
    }
    void freightAnims(Item block) {
        for (const Item &item : doc.items(block)) {
            if (doc.is(item, "mstsfreightanimenabled")) {
                // Off: the MSTS animations given before it are dropped.
                if (int(number(item, 0, Quantity::Any)) == 0)
                    def.freightAnims.clear();
            } else if (doc.is(item, "freightanimstatic")) {
                VehicleDefinition::FreightAnim anim;
                for (const Item &part : doc.items(item)) {
                    if (doc.is(part, "shape")) {
                        anim.shape = string(part);
                        anim.path = def.file(anim.shape, holder(part), {def.folder}, game);
                    }
                    else if (doc.is(part, "offset"))
                        for (int k = 0; k < 3; ++k)
                            anim.offset[k] = float(number(part, k, Quantity::Any));
                }
                def.freightAnims << anim;
            }
        }
    }
    void wagon(Item block) {
        if (def.engName.isEmpty()) {
            def.engName = string(block).trimmed();
            def.displayName = def.engName;
        }
        for (const Item &item : doc.items(block)) {
            if (!doc.isBlock(item))
                continue;
            if (doc.is(item, "name")) {
                def.displayName = string(item);
            } else if (doc.is(item, "tsrekeywords")) {
                def.searchKeywords = string(item);
            } else if (doc.is(item, "freightanim")) {
                VehicleDefinition::FreightAnim anim;
                anim.shape = string(item);
                anim.path = def.file(anim.shape, holder(item), {def.folder}, game);
                def.freightAnims << anim;
            } else if (doc.is(item, "size")) {
                for (int k = 0; k < 3; ++k)
                    def.size[k] = float(number(item, k, Quantity::Length));
            } else if (doc.is(item, "wagonshape")) {
                def.shape = string(item);
                def.shapePath = def.file(def.shape, holder(item), {def.folder}, game);
            } else if (doc.is(item, "mass")) {
                def.massKg = number(item, 0, Quantity::Mass);
            } else if (doc.is(item, "ortsdavis_a") || doc.is(item, "ortsdavis_b") || doc.is(item, "ortsdavis_c")) {
                const int k = doc.text(item).back().toLower().toLatin1() - 'a';
                const Quantity kinds[3] = {Quantity::Force, Quantity::Resistance, Quantity::ResistanceSquared};
                bool read = false;
                def.davis[k] = number(item, 0, kinds[k], &read);
                def.hasDavis = def.hasDavis || read;
            } else if (doc.is(item, "friction")) {
                const Quantity kinds[5] = {Quantity::Resistance, Quantity::Any, Quantity::Speed, Quantity::Resistance,
                                           Quantity::Any};
                bool all = true;
                for (int k = 0; k < 5; ++k) {
                    bool read = false;
                    def.friction[k] = number(item, k, kinds[k], &read);
                    all = all && read;
                }
                def.hasFriction = all;
            } else if (doc.is(item, "passengercapacity")) {
                def.passengerCapacity = true;
            } else if (doc.is(item, "sound")) {
                def.soundFile = string(item);
                def.soundPath = sound(def.soundFile, holder(item));
            } else if (doc.is(item, "wheelradius")) {
                def.wheelRadius = float(number(item, 0, Quantity::Length));
            } else if (doc.is(item, "coupling")) {
                coupling(item);
            } else if (doc.is(item, "ortsfreightanims")) {
                freightAnims(item);
            } else if (doc.is(item, "type")) {
                def.type = string(item);
                const QString t = def.type.toLower();
                def.wagonTypeId = t == "engine" ? 4 : t == "carriage" ? 1 : t == "freight" ? 2 : t == "tender" ? 3
                                                                                                 : def.wagonTypeId;
            } else if (doc.is(item, "brakesystemtype")) {
                def.brakeSystemType = string(item);
            } else if (doc.is(item, "orts3dcab")) {
                // Its alternate viewpoints only after one, as Open Rails
                // reads them.
                if (!cab3D)
                    cab3D = std::make_shared<VehicleDefinition::Cab3D>();
                cab3d(item);
            } else if (doc.is(item, "ortsalternate3dcabviewpoints")) {
                if (cab3D)
                    for (const Item &viewpoint : doc.items(item))
                        if (doc.is(viewpoint, "ortsalternate3dcabviewpoint"))
                            cab3d(viewpoint);
            } else if (doc.is(item, "lights")) {
                // Kept as its text, read on first use; a later block
                // replaces an earlier one.
                def.lights = lightsOf(doc, item, def.folder);
            }
        }
        def.typeHash = def.type;
    }
    void engine(Item block) {
        for (const Item &item : doc.items(block)) {
            if (!doc.isBlock(item))
                continue;
            if (doc.is(item, "type")) {
                def.engType = string(item);
                def.typeHash += "-" + def.engType;
                const QString t = def.engType.toLower();
                if (def.wagonTypeId == 4)
                    def.wagonTypeId += t == "diesel" ? 1 : t == "steam" ? 2 : 0;
            } else if (doc.is(item, "name")) {
                def.displayName = string(item);
            } else if (doc.is(item, "maxvelocity")) {
                def.maxSpeedMps = number(item, 0, Quantity::Speed);
            } else if (doc.is(item, "maxpower")) {
                def.powerW = number(item, 0, Quantity::Power);
            } else if (doc.is(item, "maxcurrent")) {
                def.maxCurrent = number(item, 0, Quantity::Current);
            } else if (doc.is(item, "maxforce")) {
                def.forceN = number(item, 0, Quantity::Force);
            } else if (doc.is(item, "sound")) {
                def.cabSoundFile = string(item);
                def.cabSoundPath = sound(def.cabSoundFile, holder(item));
            } else if (doc.is(item, "cabview")) {
                def.cabViewFile = string(item);
                def.cabViewPath = def.file(def.cabViewFile, holder(item), {def.folder + "/CABVIEW"}, game);
            } else if (doc.is(item, "wheelradius")) {
                def.driverWheelRadius = float(number(item, 0, Quantity::Length));
            }
        }
    }
    void read() {
        for (const Item &item : doc.items()) {
            if (!doc.isBlock(item))
                continue;
            if (doc.is(item, "wagon"))
                wagon(item);
            else if (doc.is(item, "engine"))
                engine(item);
            else if (doc.is(item, "name"))
                def.displayName = string(item);
            else if (doc.is(item, "brakesystemtype"))
                def.brakeSystemType = string(item);
        }
        def.cab3D = cab3D;
        for (int i = cab3DFiles.size() - 1; i >= 0 && def.cab3DShapePath.isEmpty(); --i)
            if (QFileInfo(cab3DFiles[i]).isFile())
                def.cab3DShapePath = cab3DFiles[i];
    }
};

// Its resistance as Davis coefficients (N, N/(m/s), N/(m/s)²): Open Rails'
// ORTSDavis values where the file has them; else MSTS's Friction, where its
// second speed is a low one (FCalc's), fitted by least squares from there to
// 80 mph; else the Davis equation for plain bearings (four axles, a frontal
// area of 10 m², the leading vehicle's air drag higher).
VehicleDefinition::Resistance resistanceOf(const VehicleDefinition &def, bool leading, double davis[3]) {
    if (def.hasDavis) {
        std::copy(def.davis, def.davis + 3, davis);
        return VehicleDefinition::Resistance::Davis;
    }
    const double *f = def.friction;
    if (def.hasFriction && f[2] >= 0.0 && f[2] <= 10.0 * Mile) {
        // Its force above the second speed.
        const auto force = [f](double v) {
            return f[0] * std::pow(f[2], f[1]) + f[3] * f[2] + f[3] * (std::pow(v, f[4] + 1.0) - std::pow(f[2], f[4] + 1.0));
        };
        // Normal equations of a + b v + c v².
        double m[3][4] = {};
        for (double v = std::max(f[2], 0.5); v <= 80.0 * Mile; v += 0.5) {
            const double row[3] = {1.0, v, v * v};
            const double y = force(v);
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c)
                    m[r][c] += row[r] * row[c];
                m[r][3] += row[r] * y;
            }
        }
        for (int k = 0; k < 3; ++k) {
            for (int r = k + 1; r < 3; ++r) {
                const double scale = m[r][k] / m[k][k];
                for (int c = k; c < 4; ++c)
                    m[r][c] -= scale * m[k][c];
            }
        }
        for (int k = 2; k >= 0; --k) {
            double sum = m[k][3];
            for (int c = k + 1; c < 3; ++c)
                sum -= m[k][c] * davis[c];
            davis[k] = sum / m[k][k];
        }
        return VehicleDefinition::Resistance::Friction;
    }
    const double tons = def.massKg / 907.18474;
    const double areaFt2 = 10.0 / 0.09290304;
    davis[0] = (1.3 * tons + 29.0 * 4.0) * PoundForce;
    davis[1] = 0.045 * tons * PoundForce / Mile;
    davis[2] = (leading ? 0.0005 : 0.00035) * areaFt2 * PoundForce / (Mile * Mile);
    return VehicleDefinition::Resistance::Default;
}

} // namespace

const std::shared_ptr<const VehicleDefinition> &VehicleDefinition::empty() {
    static const std::shared_ptr<const VehicleDefinition> none = std::make_shared<VehicleDefinition>();
    return none;
}

std::shared_ptr<const VehicleDefinition> VehicleDefinition::read(const QString &folder, const QString &name,
                                                                 const QString &game, bool openRails) {
    auto def = std::make_shared<VehicleDefinition>();
    def->folder = QFileInfo(QString(folder).replace(u'\\', u'/')).absoluteFilePath();
    const QString copy = def->folder + "/OPENRAILS/" + name;
    def->path = openRails && QFileInfo(copy).isFile() ? copy : def->folder + "/" + name;
    def->document = SimisDocument::read(def->path, {game, def->folder});
    if (!def->document->ok())
        return def;
    Reader{*def->document, *def, QDir::cleanPath(game), {}, {}}.read();
    if (def->massKg > 0.0) {
        double lead[3] = {0.0, 0.0, 0.0};
        def->resistanceFrom = resistanceOf(*def, false, def->resistance);
        resistanceOf(*def, true, lead);
        def->resistanceLeading = lead[2];
    }
    return def;
}

QString VehicleDefinition::file(const QString &written, const QString &holder, const QStringList &folders,
                                const QString &game) {
    const QString name = written.trimmed();
    if (name.isEmpty())
        return {};
    if (name.startsWith(u"[[")) {
        QString problem;
        const QString path = SimisDocument::resolve(name, holder, {game, folder}, &problem);
        if (path.isEmpty())
            problems.push_back({holder, 0, problem});
        return path;
    }
    QString first;
    for (const QString &base : folders) {
        const QString path = QDir::cleanPath(base + u'/' + QString(name).replace(u'\\', u'/'));
        if (first.isEmpty())
            first = path;
        if (QFileInfo(path).isFile())
            return path;
    }
    return first;
}

double VehicleDefinition::number(QAnyStringView path, int index, double otherwise) const {
    if (!document)
        return otherwise;
    const SimisDocument::Item block = document->last(path);
    const SimisDocument::Item value = document->values(block).value(index);
    const VehicleKeys::Value key = VehicleKeys::value(path, index);
    double si = 0.0;
    const Units::Quantity quantity = key.kind == VehicleKeys::Value::Kind::Number ? key.quantity : Units::Quantity::Any;
    return value.tree && document->number(value, quantity, si, key.defaultUnit) ? si : otherwise;
}

QString VehicleDefinition::text(QAnyStringView path, int index) const {
    if (!document)
        return QString();
    return document->text(document->values(document->last(path)).value(index)).toString();
}
