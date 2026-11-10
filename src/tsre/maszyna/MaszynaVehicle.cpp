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
#include <tsre/maszyna/MaszynaMaterial.h>
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
#include <cmath>
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
        } else if (MaszynaText::startsWith(line, "Cntrl.")) {
            takeNumber(vehicle.controllerPositions, line, "MCPN");
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
    vehicle.files += text.files();
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
    vehicle.files += text.files();
    const QString catalogue = vehicle.folder + QLatin1String("/textures.txt");
    if (QFile::exists(catalogue))
        vehicle.files.push_back(catalogue);
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

const MaszynaFiz &MaszynaVehicle::physics() const {
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    if (physicsRead == nullptr)
        physicsRead = std::make_shared<const MaszynaFiz>(MaszynaFiz::read(folder, type + QLatin1String(".fiz")));
    return *physicsRead;
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
        // A catalogue may list more than was copied: only what is there.
        if (!MaszynaMaterial::exists(livery.skin, folder)
                && !MaszynaMaterial::exists(livery.skin + QLatin1String(",1"), folder))
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

namespace {

// MaSzyna's brake block friction (friction.cpp) at `n` kN on a block and
// `kmh` km/h, by the .fiz's BM.
double friction(const QString &material, double n, double kmh) {
    const QString m = material.section(QLatin1Char('+'), 0, 0);
    if (m == QLatin1String("P10-Bgu"))
        return std::exp(-0.017 * n) * (0.18 - 0.09 * std::exp(-kmh / 25.7)) + 0.381 * std::exp(-kmh / 25.7) - 0.022;
    if (m == QLatin1String("P10yBg") || m == QLatin1String("P10yBgu")) {
        const bool u = m.endsWith(QLatin1Char('u'));
        const double a = (u ? 1.68 : 2.135) * std::exp((u ? -0.02735 : -0.03726) * n) - 0.5;
        const double c = 0.353 - a * (u ? 0.044 : 0.029);
        return 0.41 - c + c * std::exp(-kmh / (25.7 + (u ? 21.0 : 20.0) * a));
    }
    if (m == QLatin1String("P10"))
        return 0.60 * ((1.6 * n + 100) / (8.0 * n + 100)) * ((kmh + 100) / (5 * kmh + 100));
    if (m == QLatin1String("FR513"))
        return 0.3 - kmh * 0.00081;
    if (m == QLatin1String("Cosid") || m == QLatin1String("Disk2"))
        return 0.27;
    if (m == QLatin1String("Disk1"))
        return 0.2375 + 0.000885 * n - 0.000345 * n * n;
    if (m == QLatin1String("FR510"))
        return 0.15;
    // P10-Bg, and without one.
    return std::exp(-0.022 * n) * (0.19 - 0.095 * std::exp(-kmh / 25.7)) + 0.384 * std::exp(-kmh / 25.7) - 0.028;
}

// All its axles from Axle (Co'Co': a letter A one, B two..., a digit as
// many).
int axlesOf(const QString &axle) {
    int n = 0;
    for (const QChar c : axle) {
        if (c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
            n += c.unicode() - 'A' + 1;
        else if (c.isDigit())
            n += c.digitValue();
    }
    return n;
}

}

double engValue(const MaszynaVehicle &vehicle, QAnyStringView path, int index, double otherwise) {
    const MaszynaFiz &fiz = vehicle.physics();
    const QString key = path.toString();
    const double high = fiz.number(u"Brake", u"HiPP", 500000.0);
    if (key == QLatin1String("wagon(maxbrakeforce")) {
        // MaSzyna's BrakeForce at the cylinders' most: the blocks' force
        // ((MaxBP·πBCR² - BCS)·BCM - BSA)·BRE on each of BCN cylinders, times
        // the blocks' friction, here at 50 km/h (TSRE's brake force has no
        // speed in it).
        const MaszynaFiz::Section *brake = fiz.section(u"Brake");
        if (brake == nullptr)
            return otherwise;
        const double cylinders = brake->number(u"BCN");
        const double radius = brake->number(u"BCR");
        const double pressure = brake->number(u"MaxBP");
        if (cylinders <= 0.0 || radius <= 0.0 || pressure <= 0.0)
            return otherwise;
        const double cylinder = pressure * M_PI * radius * radius - brake->number(u"BCS");
        const double blocks = (cylinder * brake->number(u"BCM", 1.0) - brake->number(u"BSA")) * brake->number(u"BRE", 1.0);
        if (blocks <= 0.0)
            return otherwise;
        const int axles = std::max(axlesOf(fiz.text(u"Wheels", u"Axle")), 1);
        const double perBlock = blocks / 1000.0 * cylinders / (axles * std::max(brake->number(u"NBpA", 1.0), 1.0));
        return friction(brake->text(u"BM"), perBlock, 50.0) * blocks * cylinders;
    }
    if (key == QLatin1String("wagon(brakecylinderpressureformaxbrakebrakeforce"))
        return fiz.number(u"Brake", u"MaxBP", otherwise);
    if (key == QLatin1String("engine(trainbrakescontrollermaxsystempressure"))
        return high;
    if (key == QLatin1String("engine(trainbrakescontrollerfullservicepressuredrop"))
        return high - fiz.number(u"Brake", u"LoPP", 350000.0);
    if (key == QLatin1String("engine(airbrakesmainmaxairpressure")
        || key == QLatin1String("engine(airbrakemaxmainrespipepressure"))
        return fiz.number(u"Brake", u"MaxCP", otherwise);
    // A dynamic brake lever's step, as electric-induction-mmd.lua gives an
    // ED brake one.
    if (key == QLatin1String("engine(enginecontrollers(brake_dynamic") && index == 2)
        return fiz.text(u"Engine", u"EngineType") == QLatin1String("ElectricInductionMotor")
                       && fiz.number(u"Engine", u"Fh") > 0.0 ? 0.1 : otherwise;
    return otherwise;
}

QString brakeSystem(const MaszynaVehicle &vehicle) {
    const MaszynaFiz &fiz = vehicle.physics();
    const bool ep = fiz.text(u"Cntrl", u"BrakeSystem") == QLatin1String("ElectroPneumatic");
    const bool twin = (qint64(fiz.number(u"BuffCoupl", u"AllowedFlag")) & 0x20) != 0;
    if (ep)
        return twin ? QStringLiteral("ep") : QStringLiteral("ep_single_pipe");
    return twin ? QStringLiteral("air_twin_pipe") : QStringLiteral("air_single_pipe");
}

QString engText(const MaszynaVehicle &vehicle, QAnyStringView path, int index) {
    if (index != 0 || path.toString() != QLatin1String("wagon(brakeequipmenttype"))
        return QString();
    // MaSzyna's valves (BrakeValve) are graduated release distributors, but
    // Westinghouse's W (and W_Lu_...), a direct release triple valve.
    const QString valve = vehicle.physics().text(u"Brake", u"BrakeValve");
    if (valve.isEmpty())
        return QString();
    if (valve == QLatin1String("W") || valve.startsWith(QLatin1String("W_")))
        return QStringLiteral("triple_valve");
    return QStringLiteral("distributor");
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
    } else if (engine.isEmpty() && v.controllerPositions > 0.0 && !v.cabModels.isEmpty()) {
        // A control car (EN57's ra and rb, a push-pull's driving trailer):
        // an engine with no force of its own, its cab driving the train's
        // motor cars on the control line (task maszyna 03).
        def->wagonTypeId = 4;
    } else {
        def->wagonTypeId = isPassengers(v.loadAccepted) ? 1 : 2;
    }
    def->type = def->wagonTypeId >= 4 ? QStringLiteral("Engine") : def->wagonTypeId == 1 ? QStringLiteral("Carriage")
                                                                                          : QStringLiteral("Freight");
    def->engType = def->wagonTypeId == 4 ? (engine.isEmpty() ? QStringLiteral("Control") : QStringLiteral("Electric"))
                 : def->wagonTypeId == 5 ? QStringLiteral("Diesel")
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
