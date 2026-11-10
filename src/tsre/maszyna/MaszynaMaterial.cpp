/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/maszyna/MaszynaMaterial.h>
#include <tsre/maszyna/Cp1250.h>
#include <tsre/maszyna/MaszynaPaths.h>
#include <tsre/Game.h>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <cstring>
#include <limits>

namespace {

// Tokens split by white space and ;, comments // and /* */, and { } [ ] each
// a token.
class Scanner {
public:
    explicit Scanner(const QByteArray &text) : p(text.constData()), end(text.constData() + text.size()) {}

    bool next(QString &token) {
        skip();
        if (p >= end)
            return false;
        if (punctuation(*p)) {
            token = Maszyna::fromCp1250(p, 1);
            ++p;
            return true;
        }
        const char *start = p;
        while (p < end && !space(*p) && !punctuation(*p))
            ++p;
        token = Maszyna::fromCp1250(start, int(p - start)).toLower();
        return true;
    }

    // What is left of the line.
    QString restOfLine() {
        while (p < end && (*p == ' ' || *p == '\t'))
            ++p;
        const char *start = p;
        while (p < end && *p != '\n' && *p != '\r' && *p != ';')
            ++p;
        return Maszyna::fromCp1250(start, int(p - start)).toLower();
    }

private:
    static bool space(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ';';
    }
    static bool punctuation(char c) {
        return c == '{' || c == '}' || c == '[' || c == ']';
    }
    void skip() {
        while (p < end) {
            if (space(*p)) {
                ++p;
            } else if (end - p >= 2 && p[0] == '/' && p[1] == '/') {
                while (p < end && *p != '\n')
                    ++p;
            } else if (end - p >= 2 && p[0] == '/' && p[1] == '*') {
                p += 2;
                while (end - p >= 2 && !(p[0] == '*' && p[1] == '/'))
                    ++p;
                p = std::min(p + 2, end);
            } else {
                return;
            }
        }
    }

    const char *p;
    const char *end;
};

bool isSeason(const QString &key) {
    return key == QLatin1String("winter:") || key == QLatin1String("spring:") || key == QLatin1String("summer:")
            || key == QLatin1String("autumn:");
}

bool isWeather(const QString &key) {
    return key == QLatin1String("clear:") || key == QLatin1String("cloudy:") || key == QLatin1String("rain:")
            || key == QLatin1String("snow:");
}

// A value kept by its block's priority: seasonal over plain.
template <typename T>
struct Kept {
    T value{};
    int priority = -1;
    void take(const T &v, int p) {
        if (p > priority) {
            value = v;
            priority = p;
        }
    }
    bool set() const { return priority >= 0; }
};

struct Values {
    Kept<QString> first, second, normalMap, orm, shader;
    Kept<float> opacity, selfIllum, glossiness, metalness, roughness;
};

// A value, or the first of a [ a b c ] set: a model looks the same every load.
QString valueOf(Scanner &scanner) {
    QString token;
    if (!scanner.next(token))
        return QString();
    if (token != QLatin1String("["))
        return token;
    QString first, item;
    while (scanner.next(item) && item != QLatin1String("]"))
        if (first.isEmpty())
            first = item;
    return first;
}

float numberOf(const QString &text, float otherwise) {
    bool ok = false;
    const float value = text.section(QLatin1Char(' '), 0, 0).toFloat(&ok);
    return ok ? value : otherwise;
}

void readBlock(Scanner &scanner, Values &values, const QString &season, int priority);

void skipBlock(Scanner &scanner) {
    QString token;
    int depth = 1;
    while (depth > 0 && scanner.next(token)) {
        if (token == QLatin1String("{"))
            ++depth;
        else if (token == QLatin1String("}"))
            --depth;
    }
}

void openedBlock(Scanner &scanner, Values &values, const QString &season, int priority) {
    QString open;
    if (scanner.next(open) && open == QLatin1String("{"))
        readBlock(scanner, values, season, priority);
}

void readTexture(Scanner &scanner, Values &values, QString key, int priority) {
    key.remove(QLatin1String("_variants"));
    const QString value = valueOf(scanner);
    if (key == QLatin1String("texture1:") || key == QLatin1String("texture_diffuse:")) {
        values.first.take(value, priority);
    } else if (key == QLatin1String("texture2:")) {
        values.second.take(value, priority);
    } else if (key == QLatin1String("texture_normalmap:")) {
        values.normalMap.take(value, priority);
    }
}

void readBlock(Scanner &scanner, Values &values, const QString &season, int priority) {
    QString key;
    while (scanner.next(key)) {
        if (key == QLatin1String("}"))
            return;
        if (isSeason(key)) {
            if (key == season) {
                openedBlock(scanner, values, season, priority + 1);
            } else {
                QString open;
                if (scanner.next(open) && open == QLatin1String("{"))
                    skipBlock(scanner);
            }
        } else if (isWeather(key)) {
            QString open;
            if (scanner.next(open) && open == QLatin1String("{"))
                skipBlock(scanner);
        } else if (key.startsWith(QLatin1String("texture"))) {
            readTexture(scanner, values, key, priority);
        } else if (key.startsWith(QLatin1String("param"))) {
            const QString value = scanner.restOfLine();
            if (key == QLatin1String("param_glossiness:"))
                values.glossiness.take(numberOf(value, NAN), priority);
        } else if (key == QLatin1String("shader:")) {
            values.shader.take(valueOf(scanner), priority);
        } else if (key == QLatin1String("opacity:")) {
            values.opacity.take(numberOf(valueOf(scanner), 0.5f), priority);
        } else if (key == QLatin1String("selfillum:")) {
            values.selfIllum.take(numberOf(valueOf(scanner), NAN), priority);
        } else if (key == QLatin1String("glossiness:")) {
            values.glossiness.take(numberOf(valueOf(scanner), NAN), priority);
        } else if (key == QLatin1String("tsre_metalness:")) {
            values.metalness.take(numberOf(valueOf(scanner), NAN), priority);
        } else if (key == QLatin1String("tsre_roughness:")) {
            values.roughness.take(numberOf(valueOf(scanner), NAN), priority);
        } else if (key == QLatin1String("tsre_orm:")) {
            values.orm.take(valueOf(scanner), priority);
        } else if (key == QLatin1String("size:")) {
            valueOf(scanner);
            valueOf(scanner);
        } else {
            const QString value = valueOf(scanner);
            if (value == QLatin1String("{"))
                skipBlock(scanner);
        }
    }
}

QString seasonKey() {
    return Game::season.isEmpty() ? QStringLiteral("summer:") : Game::season.toLower() + QLatin1Char(':');
}

// MaSzyna's glossiness is a specular power.
float roughnessOf(float glossiness) {
    return float(std::pow(2.0f / (std::max(glossiness, 0.0f) + 2.0f), 0.25f));
}

// The file's header only: DDS by its pixel format, others as Qt reads them.
bool fileHasAlphaChannel(const QString &path) {
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QLatin1String(".dds"), Qt::CaseInsensitive)) {
        const QImage::Format format = QImageReader(path).imageFormat();
        return format != QImage::Format_Invalid && QImage::toPixelFormat(format).alphaUsage() == QPixelFormat::UsesAlpha;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray header = file.read(128);
    if (header.size() < 128 || !header.startsWith("DDS "))
        return false;
    quint32 flags;
    std::memcpy(&flags, header.constData() + 80, sizeof flags);
    constexpr quint32 AlphaPixels = 0x1, FourCc = 0x4;
    if ((flags & FourCc) != 0)
        return !header.mid(84, 4).startsWith("DXT1");
    return (flags & AlphaPixels) != 0;
}

QMutex readMutex;
QHash<QString, std::shared_ptr<const MaszynaMaterial>> materials;

std::shared_ptr<const MaszynaMaterial> read(const QString &name, const QString &vehicleFolder) {
    auto material = std::make_shared<MaszynaMaterial>();
    const MaszynaPaths::Name cleaned = MaszynaPaths::clean(name);
    if (cleaned.text.isEmpty())
        return material;
    const QStringList places = MaszynaPaths::places(cleaned.text, vehicleFolder);
    const QString file = MaszynaPaths::findMaterial(places);
    if (file.isEmpty()) {
        material->baseName = cleaned.text;
        material->base = MaszynaPaths::findTexture(places);
        material->found = !material->base.isEmpty();
        material->clampS = cleaned.clampS;
        material->clampT = cleaned.clampT;
        material->alphaChannel = fileHasAlphaChannel(material->base);
        return material;
    }
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly))
        return material;
    *material = MaszynaMaterial::parse(in.readAll(), seasonKey());
    material->found = true;
    material->file = file;
    const auto locate = [&vehicleFolder](const QString &written, QString &path, bool *clampS, bool *clampT) {
        if (written.isEmpty())
            return;
        const MaszynaPaths::Name texture = MaszynaPaths::clean(written);
        path = MaszynaPaths::findTexture(MaszynaPaths::places(texture.text, vehicleFolder));
        if (clampS != nullptr) {
            *clampS = texture.clampS;
            *clampT = texture.clampT;
        }
    };
    locate(material->baseName, material->base, &material->clampS, &material->clampT);
    locate(material->normalName, material->normal, nullptr, nullptr);
    locate(material->ormName, material->orm, nullptr, nullptr);
    material->alphaChannel = fileHasAlphaChannel(material->base);
    return material;
}

}

MaszynaMaterial MaszynaMaterial::parse(const QByteArray &text, const QString &season) {
    Scanner scanner(text);
    Values values;
    readBlock(scanner, values, season, 0);

    MaszynaMaterial material;
    material.baseName = values.first.value;
    material.shader = values.shader.value;
    material.normalName = values.normalMap.value;
    if (material.normalName.isEmpty() && material.shader.contains(QLatin1String("normalmap")))
        material.normalName = values.second.value;
    material.ormName = values.orm.value;
    material.hasOpacity = values.opacity.set();
    if (material.hasOpacity)
        material.opacity = values.opacity.value;
    if (values.selfIllum.set())
        material.selfIllum = values.selfIllum.value;
    if (values.metalness.set())
        material.metalness = values.metalness.value;
    if (values.roughness.set())
        material.roughness = values.roughness.value;
    else if (values.glossiness.set() && !std::isnan(values.glossiness.value))
        material.roughness = roughnessOf(values.glossiness.value);
    return material;
}

std::shared_ptr<const MaszynaMaterial> MaszynaMaterial::find(const QString &name, const QString &vehicleFolder) {
    const QString key = vehicleFolder + QLatin1Char('\n') + name + QLatin1Char('\n') + Game::season;
    {
        QMutexLocker lock(&readMutex);
        const auto found = materials.constFind(key);
        if (found != materials.constEnd())
            return found.value();
    }
    const std::shared_ptr<const MaszynaMaterial> material = read(name, vehicleFolder);
    QMutexLocker lock(&readMutex);
    materials.insert(key, material);
    return material;
}

bool MaszynaMaterial::exists(const QString &name, const QString &vehicleFolder) {
    const MaszynaPaths::Name cleaned = MaszynaPaths::clean(name);
    const QStringList places = MaszynaPaths::places(cleaned.text, vehicleFolder);
    return !MaszynaPaths::findMaterial(places).isEmpty() || !MaszynaPaths::findTexture(places).isEmpty();
}

void MaszynaMaterial::forget() {
    QMutexLocker lock(&readMutex);
    materials.clear();
}
