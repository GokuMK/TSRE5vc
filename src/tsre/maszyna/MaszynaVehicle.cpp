/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/maszyna/MaszynaVehicle.h>
#include <tsre/maszyna/Cp1250.h>
#include <tsre/maszyna/MaszynaPaths.h>
#include <tsre/maszyna/MaszynaText.h>
#include <tsre/trains/VehicleDefinition.h>
#include <tsre/Game.h>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <cstdlib>

namespace {

constexpr double KmhToMps = 1.0 / 3.6;
constexpr double KwToW = 1000.0;

QMutex vehicleMutex;
// Each by its files' modification times, so a changed file is read again.
template <typename T>
struct Held {
    std::shared_ptr<const T> value;
    QDateTime modified, modified2;
};
QHash<QString, Held<MaszynaVehicle>> vehicles;
QHash<QString, Held<MaszynaCatalogue>> catalogues;

// " Key=value" in a line, as MaSzyna finds them: empty for none.
QByteArrayView valueOf(QByteArrayView line, const char *key) {
    const QByteArray wanted = QByteArray(key) + '=';
    qsizetype from = 0;
    while (true) {
        const qsizetype at = line.indexOf(QByteArrayView(wanted), from);
        if (at < 0)
            return {};
        if (at == 0 || line[at - 1] == ' ' || line[at - 1] == '\t') {
            qsizetype start = at + wanted.size();
            while (start < line.size() && line[start] == ' ')
                ++start;
            qsizetype end = start;
            while (end < line.size() && line[end] != ' ' && line[end] != '\t')
                ++end;
            return line.sliced(start, end - start);
        }
        from = at + 1;
    }
}

double number(QByteArrayView text, double otherwise = 0.0) {
    bool ok = false;
    const double value = text.toByteArray().toDouble(&ok);
    return ok ? value : otherwise;
}

void takeText(QString &to, QByteArrayView line, const char *key) {
    if (to.isEmpty()) {
        const QByteArrayView value = valueOf(line, key);
        if (!value.isEmpty())
            to = MaszynaText::decode(value);
    }
}

template <typename T>
void takeNumber(T &to, QByteArrayView line, const char *key) {
    if (to == T(0)) {
        const QByteArrayView value = valueOf(line, key);
        if (!value.isEmpty())
            to = T(number(value));
    }
}

// The lines the editors and the AI need; a line's section is its first word.
void readFiz(MaszynaVehicle &vehicle) {
    MaszynaText text(vehicle.folder, vehicle.type + QLatin1String(".fiz"));
    if (!text.opened())
        return;
    QByteArrayView line;
    while (text.nextLine(line)) {
        if (line.contains('#'))
            continue;
        if (MaszynaText::startsWith(line, "Param.")) {
            takeText(vehicle.category, line, "Category");
            takeText(vehicle.fizType, line, "Type");
            takeNumber(vehicle.massKg, line, "M");
            takeNumber(vehicle.maxSpeedKmh, line, "Vmax");
            takeNumber(vehicle.powerKw, line, "PWR");
        } else if (MaszynaText::startsWith(line, "Dimensions:")) {
            takeNumber(vehicle.length, line, "L");
            takeNumber(vehicle.height, line, "H");
            takeNumber(vehicle.width, line, "W");
        } else if (MaszynaText::startsWith(line, "Wheels:")) {
            takeNumber(vehicle.wheelDiameter, line, "D");
        } else if (MaszynaText::startsWith(line, "BuffCoupl.")) {
            takeText(vehicle.couplerType, line, "CType");
        } else if (MaszynaText::startsWith(line, "Engine:")) {
            takeText(vehicle.engineType, line, "EngineType");
        } else if (MaszynaText::startsWith(line, "Power:")) {
            takeText(vehicle.pantType, line, "PantType");
        } else if (MaszynaText::startsWith(line, "Wipers:")) {
            takeNumber(vehicle.wiperAngle, line, "Angle");
        } else if (MaszynaText::startsWith(line, "Load:")) {
            takeText(vehicle.loadAccepted, line, "LoadAccepted");
        } else if (MaszynaText::startsWith(line, "Doors:")) {
            takeText(vehicle.doorOpenMethod, line, "DoorOpenMethod");
            takeNumber(vehicle.doorShiftLeft, line, "DoorMaxShiftL");
            takeNumber(vehicle.doorShiftRight, line, "DoorMaxShiftR");
            takeNumber(vehicle.doorPlugShift, line, "DoorMaxShiftPlug");
            takeNumber(vehicle.stepShift, line, "PlatformMaxShift");
            takeText(vehicle.stepOpenMethod, line, "PlatformOpenMethod");
            takeNumber(vehicle.mirrorShift, line, "MirrorMaxShift");
        }
    }
    vehicle.problems += text.problems();
}

QString cleanName(QByteArrayView token) {
    QString name = MaszynaText::decode(token).toLower();
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (name.startsWith(QLatin1Char('/')))
        name.remove(0, 1);
    return name;
}

// A tsre_...: key, wherever it appears.
bool readOwnKey(MaszynaText &text, MaszynaVehicle &vehicle, QByteArrayView token) {
    if (!MaszynaText::startsWith(token, "tsre_") || !token.endsWith(":"))
        return false;
    const QString key = MaszynaText::decode(token.chopped(1)).toLower();
    QByteArrayView value;
    if (text.next(value))
        vehicle.tsre.insert(key, MaszynaText::decode(value));
    return true;
}

void readModels(MaszynaText &text, MaszynaVehicle &vehicle) {
    QByteArrayView token;
    if (!text.next(token))
        return;
    vehicle.model = cleanName(token);
    if (vehicle.model.endsWith(QLatin1Char('#')))
        vehicle.model.chop(1);
    while (text.next(token) && !MaszynaText::same(token, "endmodels")) {
        if (readOwnKey(text, vehicle, token))
            continue;
        if (MaszynaText::same(token, "animations:")) {
            for (int kind = 0; kind < MaszynaVehicle::AnimationKinds; ++kind) {
                if (!text.next(token))
                    return;
                const int count = int(number(token, -1.0));
                vehicle.animations[kind] = count;
                if (count < 0)
                    break;
            }
        } else if (MaszynaText::same(token, "lowpolyinterior:")) {
            if (text.next(token))
                vehicle.lowpolyInterior = cleanName(token);
        } else if (MaszynaText::same(token, "attachments:")) {
            while (text.next(token) && !MaszynaText::same(token, "}"))
                if (!MaszynaText::same(token, "{"))
                    vehicle.attachments.push_back(cleanName(token));
        } else if (MaszynaText::same(token, "loads:")) {
            while (text.next(token) && !MaszynaText::same(token, "}")) {
            }
        } else if (MaszynaText::same(token, "pantfactors:")) {
            for (float &factor : vehicle.pantFactors) {
                if (!text.next(token))
                    return;
                factor = float(number(token));
            }
            vehicle.hasPantFactors = true;
        } else if (MaszynaText::startsWith(token, "anim") && token.endsWith("prefix:")) {
            const QString key = MaszynaText::decode(token.chopped(1)).toLower();
            if (text.next(token))
                vehicle.prefixes.insert(key, MaszynaText::decode(token));
        }
    }
}

// (p1) is a placed vehicle's name and (p3) its skin: left as they are, for
// its livery row to fill (modelFile). A few types name their model by them.
void readMmd(MaszynaVehicle &vehicle) {
    MaszynaText text(vehicle.folder, vehicle.type + QLatin1String(".mmd"),
                     {QStringLiteral("(p1)"), vehicle.type, QStringLiteral("(p3)")});
    if (!text.opened())
        return;
    QByteArrayView token;
    bool inside = false;
    while (text.next(token)) {
        if (MaszynaText::same(token, "models:")) {
            readModels(text, vehicle);
        } else if (MaszynaText::same(token, "internaldata:")) {
            inside = true;
        } else if (MaszynaText::same(token, "bogies:") && !inside) {
            while (text.next(token) && !MaszynaText::same(token, "end"))
                vehicle.bogieOffsets.push_back(float(number(token)));
        } else if (inside && MaszynaText::startsWith(token, "cab") && token.endsWith("model:")) {
            if (text.next(token))
                vehicle.cabModels.push_back(cleanName(token));
        } else {
            readOwnKey(text, vehicle, token);
        }
    }
    vehicle.problems += text.problems();
}

// A model's file: .e3d in place of what the .mmd names (.t3d), a row's livery
// for a placed vehicle's name.
QString modelFile(const QString &folder, const QString &written, const QString &skin) {
    QString name = written;
    name.replace(QLatin1String("(p1)"), skin);
    name.replace(QLatin1String("(p3)"), skin);
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > name.lastIndexOf(QLatin1Char('/')))
        name.truncate(dot);
    return QDir::cleanPath(folder + QLatin1Char('/') + name + QLatin1String(".e3d"));
}

bool isPassengers(const QString &loadAccepted) {
    return loadAccepted.contains(QLatin1String("passengers"), Qt::CaseInsensitive);
}

}

std::shared_ptr<const MaszynaVehicle> MaszynaVehicle::read(const QString &folder, const QString &type) {
    const QString key = folder + QLatin1Char('\n') + type;
    const QString fiz = folder + QLatin1Char('/') + type + QLatin1String(".fiz");
    const QString mmd = folder + QLatin1Char('/') + type + QLatin1String(".mmd");
    const QDateTime fizTime = QFileInfo(fiz).lastModified();
    const QDateTime mmdTime = QFileInfo(mmd).lastModified();
    {
        QMutexLocker lock(&vehicleMutex);
        const auto found = vehicles.constFind(key);
        if (found != vehicles.constEnd() && found->modified == fizTime && found->modified2 == mmdTime)
            return found->value;
    }
    auto vehicle = std::make_shared<MaszynaVehicle>();
    vehicle->folder = folder;
    vehicle->type = type;
    vehicle->fizPath = fiz;
    vehicle->mmdPath = mmd;
    std::shared_ptr<const MaszynaVehicle> result;
    if (fizTime.isValid() && mmdTime.isValid()) {
        readFiz(*vehicle);
        readMmd(*vehicle);
        result = vehicle;
    }
    QMutexLocker lock(&vehicleMutex);
    vehicles.insert(key, Held<MaszynaVehicle>{result, fizTime, mmdTime});
    return result;
}

void MaszynaVehicle::forget() {
    QMutexLocker lock(&vehicleMutex);
    vehicles.clear();
    catalogues.clear();
}

std::shared_ptr<const MaszynaCatalogue> MaszynaCatalogue::read(const QString &folder) {
    const QString path = folder + QLatin1String("/textures.txt");
    const QDateTime modified = QFileInfo(path).lastModified();
    {
        QMutexLocker lock(&vehicleMutex);
        const auto found = catalogues.constFind(folder);
        if (found != catalogues.constEnd() && found->modified == modified)
            return found->value;
    }
    auto made = std::make_shared<MaszynaCatalogue>();
    MaszynaCatalogue &catalogue = *made;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMutexLocker lock(&vehicleMutex);
        catalogues.insert(folder, Held<MaszynaCatalogue>{made, modified, QDateTime()});
        return made;
    }
    QChar kind;
    const QByteArray bytes = file.readAll();
    for (const QByteArray &raw : bytes.split('\n')) {
        QByteArrayView line(raw);
        while (!line.isEmpty() && (line.back() == '\r' || line.back() == ' '))
            line.chop(1);
        if (line.isEmpty())
            continue;
        const qsizetype comment = line.indexOf("//");
        const QByteArrayView content = comment >= 0 ? line.first(comment) : line;
        const qsizetype equals = content.indexOf('=');
        if (equals < 0)
            continue;
        const QByteArrayView target = content.first(equals);
        const QByteArrayView parameter = content.sliced(equals + 1);
        if (target.startsWith('!')) {
            kind = parameter.isEmpty() ? QChar() : QChar::fromLatin1(parameter[0]);
            continue;
        }
        if (target.startsWith('*') || target.startsWith('@'))
            continue;

        const QStringList fields = MaszynaText::decode(parameter).split(QLatin1Char(','));
        Livery livery;
        livery.skin = MaszynaText::decode(target).toLower();
        if (livery.skin.endsWith(QLatin1String(".mat")))
            livery.skin.chop(4);
        livery.type = fields.value(0).trimmed().toLower();
        QStringList words = {fields.value(1)};
        if (comment >= 0) {
            const QStringList meta = MaszynaText::decode(line.sliced(comment + 2)).split(QLatin1Char(','));
            livery.name = meta.value(1).trimmed();
            words << meta.mid(2);
        }
        livery.keywords = words.join(QLatin1Char(' ')).replace(QLatin1Char('_'), QLatin1Char(' ')).simplified();
        if (!catalogue.kinds.contains(livery.type))
            catalogue.kinds.insert(livery.type, kind);
        catalogue.liveries.push_back(livery);
    }
    QMutexLocker lock(&vehicleMutex);
    catalogues.insert(folder, Held<MaszynaCatalogue>{made, modified, QDateTime()});
    return made;
}

namespace MaszynaVehicles {

bool holdsVehicle(const QString &folder) {
    const QDir dir(folder);
    for (const QString &mmd : dir.entryList(QStringList() << "*.mmd", QDir::Files))
        if (dir.exists(mmd.chopped(4) + QLatin1String(".fiz")))
            return true;
    return false;
}

QStringList rows(const QString &folder, int *unlisted) {
    const QDir dir(folder);
    QStringList out;
    const std::shared_ptr<const MaszynaCatalogue> held = MaszynaCatalogue::read(folder);
    const MaszynaCatalogue &catalogue = *held;
    QSet<QString> withLivery, listedSkins;
    const auto isTrain = [&folder](const QString &type) {
        const std::shared_ptr<const MaszynaVehicle> vehicle = MaszynaVehicle::read(folder, type);
        return vehicle != nullptr && vehicle->category.compare(QLatin1String("train"), Qt::CaseInsensitive) == 0;
    };
    for (const MaszynaCatalogue::Livery &livery : catalogue.liveries) {
        listedSkins.insert(livery.skin);
        if (!isTrain(livery.type))
            continue;
        withLivery.insert(livery.type);
        out.push_back(livery.type + QLatin1Char('#') + livery.skin + QLatin1String(".mmd"));
    }
    for (const QString &mmd : dir.entryList(QStringList() << "*.mmd", QDir::Files)) {
        const QString type = mmd.chopped(4);
        if (!withLivery.contains(type.toLower()) && isTrain(type))
            out.push_back(type + QLatin1String("#.mmd"));
    }
    if (unlisted != nullptr) {
        QSet<QString> skins;
        for (QString mat : dir.entryList(QStringList() << "*.mat", QDir::Files)) {
            mat = mat.chopped(4).toLower();
            const qsizetype comma = mat.lastIndexOf(QLatin1Char(','));
            skins.insert(comma > 0 ? mat.first(comma) : mat);
        }
        *unlisted = int((skins - listedSkins).size());
    }
    return out;
}

QString firstRow(const QString &folder, const QString &type) {
    const QString wanted = type.toLower() + QLatin1Char('#');
    for (const QString &row : rows(folder))
        if (row.startsWith(wanted, Qt::CaseInsensitive))
            return row;
    return type + QLatin1String("#.mmd");
}

std::shared_ptr<const VehicleDefinition> definition(const QString &folder, const QString &row) {
    auto def = std::make_shared<VehicleDefinition>();
    const QString stem = row.endsWith(QLatin1String(".mmd"), Qt::CaseInsensitive) ? row.chopped(4) : row;
    const QString type = stem.section(QLatin1Char('#'), 0, 0);
    const QString skin = stem.section(QLatin1Char('#'), 1);
    def->path = folder + QLatin1Char('/') + row;
    def->folder = folder;
    def->maszyna = MaszynaVehicle::read(folder, type);
    def->skin = skin;
    if (def->maszyna == nullptr)
        return def;
    const MaszynaVehicle &v = *def->maszyna;

    const std::shared_ptr<const MaszynaCatalogue> held = MaszynaCatalogue::read(folder);
    const MaszynaCatalogue &catalogue = *held;
    def->engName = stem;
    def->displayName = type + (skin.isEmpty() ? QString() : QLatin1Char(' ') + skin);
    const QString folderName = QDir(Game::root + QLatin1String("/TRAINS/TRAINSET")).relativeFilePath(folder);
    for (const MaszynaCatalogue::Livery &livery : catalogue.liveries)
        if (livery.skin == skin) {
            if (!livery.name.isEmpty())
                def->displayName = livery.name;
            def->searchKeywords = livery.keywords;
            break;
        }
    def->displayName += QLatin1String(" (") + folderName + QLatin1Char(')');

    const QString engine = v.engineType.toLower();
    const QChar kind = catalogue.kinds.value(type.toLower());
    if (engine.contains(QLatin1String("electric")) && !engine.contains(QLatin1String("diesel"))) {
        def->wagonTypeId = 4;
    } else if (engine.contains(QLatin1String("diesel")) || engine == QLatin1String("dumbde")) {
        def->wagonTypeId = 5;
    } else if (engine == QLatin1String("dumb") || engine == QLatin1String("wheelsdriven")) {
        def->wagonTypeId = kind == QLatin1Char('p') ? 6 : kind == QLatin1Char('s') ? 5 : 4;
    } else {
        def->wagonTypeId = isPassengers(v.loadAccepted) ? 1 : 2;
    }
    def->type = def->wagonTypeId >= 4 ? QStringLiteral("Engine") : def->wagonTypeId == 1 ? QStringLiteral("Carriage")
                                                                                          : QStringLiteral("Freight");
    def->engType = def->wagonTypeId == 4 ? QStringLiteral("Electric") : def->wagonTypeId == 5 ? QStringLiteral("Diesel")
                 : def->wagonTypeId == 6 ? QStringLiteral("Steam") : QString();
    def->typeHash = def->engType.isEmpty() ? def->type : def->type + QLatin1Char('-') + def->engType;

    def->shape = v.model;
    def->shapePath = modelFile(folder, v.model, skin);
    const auto extra = [&](const QString &written) {
        VehicleDefinition::FreightAnim anim;
        anim.shape = written;
        anim.path = modelFile(folder, written, skin);
        def->freightAnims.push_back(anim);
    };
    if (!v.lowpolyInterior.isEmpty())
        extra(v.lowpolyInterior);
    for (const QString &attachment : v.attachments)
        extra(attachment);

    def->size[0] = v.width;
    def->size[1] = v.height;
    def->size[2] = v.length;
    def->massKg = v.massKg;
    def->wheelRadius = def->driverWheelRadius = v.wheelDiameter * 0.5f;
    VehicleDefinition::Coupling coupling;
    coupling.type = v.couplerType;
    def->couplings.push_back(coupling);
    def->maxSpeedMps = v.maxSpeedKmh * KmhToMps;
    def->powerW = v.powerKw * KwToW;
    def->passengerCapacity = isPassengers(v.loadAccepted);
    VehicleDefinition::fillDefaultResistance(*def);
    return def;
}

}
