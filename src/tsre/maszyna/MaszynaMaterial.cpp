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
// a token. Values as written: names keep their case (task shapes 18), keys
// are compared in lower case.
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
        token = Maszyna::fromCp1250(start, int(p - start));
        return true;
    }

    // What is left of the line.
    QString restOfLine() {
        while (p < end && (*p == ' ' || *p == '\t'))
            ++p;
        const char *start = p;
        while (p < end && *p != '\n' && *p != '\r' && *p != ';')
            ++p;
        return Maszyna::fromCp1250(start, int(p - start));
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
    Kept<QString> first, second, normalMap, orm, shader, specGloss, reflMap;
    Kept<float> opacity, selfIllum, glossiness, metalness, roughness, reflection, specular;
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
    } else if (key == QLatin1String("texture_specgloss:")) {
        values.specGloss.take(value, priority);
    } else if (key == QLatin1String("texture_reflmap:")) {
        values.reflMap.take(value, priority);
    }
}

void readBlock(Scanner &scanner, Values &values, const QString &season, int priority) {
    QString key;
    while (scanner.next(key)) {
        key = key.toLower();
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
            else if (key == QLatin1String("param_reflection:"))
                values.reflection.take(numberOf(value, NAN), priority);
            else if (key == QLatin1String("param_specular:"))
                values.specular.take(numberOf(value, NAN), priority);
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

std::shared_ptr<const MaszynaMaterial> read(const QString &name, const MaszynaPaths::Lookup &lookup) {
    auto material = std::make_shared<MaszynaMaterial>();
    const MaszynaPaths::Name cleaned = MaszynaPaths::clean(name, lookup.rules);
    if (cleaned.text.isEmpty())
        return material;
    const QStringList places = MaszynaPaths::places(cleaned.text, lookup);
    const QString file = MaszynaPaths::findMaterial(places);
    if (file.isEmpty()) {
        material->baseName = cleaned.text;
        material->textureCount = 1;
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
    const auto locate = [&lookup](const QString &written, QString &path, bool *clampS, bool *clampT) {
        if (written.isEmpty())
            return;
        const MaszynaPaths::Name texture = MaszynaPaths::clean(written, lookup.rules);
        path = MaszynaPaths::findTexture(MaszynaPaths::places(texture.text, lookup));
        if (clampS != nullptr) {
            *clampS = texture.clampS;
            *clampT = texture.clampT;
        }
    };
    locate(material->baseName, material->base, &material->clampS, &material->clampT);
    locate(material->normalName, material->normal, nullptr, nullptr);
    locate(material->ormName, material->orm, nullptr, nullptr);
    locate(material->maskName, material->mask, nullptr, nullptr);
    if (material->specGloss())
        locate(material->specGlossName, material->specGlossMap, nullptr, nullptr);
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
    material.shader = values.shader.value.toLower();
    material.secondName = values.second.value;
    material.specGlossName = values.specGloss.value;
    material.reflMapName = values.reflMap.value;
    for (const QString *name : {&material.baseName, &material.secondName, &values.normalMap.value,
                                &material.specGlossName, &material.reflMapName})
        material.textureCount += name->isEmpty() ? 0 : 1;
    if (values.reflection.set())
        material.reflection = values.reflection.value;
    if (values.specular.set())
        material.specular = values.specular.value;
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
    // The map masking its reflection.
    switch (material.family()) {
    case Family::Reflective:
        // Its reflmap; without a shader, slot 1's texture (texture2, or a
        // normal map named there): MaSzyna's default_2 reads it as one.
        material.maskName = !material.reflMapName.isEmpty() ? material.reflMapName
                          : !material.secondName.isEmpty() ? material.secondName : values.normalMap.value;
        break;
    case Family::NormalMapped:
        material.maskName = material.normalName;
        break;
    default:
        break;
    }
    return material;
}

float MaszynaMaterial::reflectionStrength() const {
    if (!std::isnan(reflection))
        return std::clamp(reflection, 0.0f, 1.0f);
    const Family f = family();
    return f == Family::Reflective || f == Family::NormalMapped ? 1.0f : 0.0f;
}

float MaszynaMaterial::presetRoughness() const {
    if (specGloss() && !specGlossName.isEmpty())
        return NAN;
    const Family f = family();
    return f == Family::Reflective || f == Family::NormalMapped ? 0.15f : NAN;
}

MaszynaMaterial::Family MaszynaMaterial::family() const {
    // Without a shader named, by the textures it fills (model/material.cpp):
    // none colored, one default, more default_2, a reflection map.
    if (shader.isEmpty())
        return textureCount == 0 ? Family::Colored : textureCount == 1 ? Family::Diffuse : Family::Reflective;
    if (shader == QLatin1String("colored") || shader == QLatin1String("default_0"))
        return Family::Colored;
    if (shader.startsWith(QLatin1String("default_2")) || shader.startsWith(QLatin1String("reflmap")))
        return Family::Reflective;
    if (shader.startsWith(QLatin1String("default")))
        return Family::Diffuse;
    if (shader.startsWith(QLatin1String("sunless")) || shader.startsWith(QLatin1String("shadowless")))
        return Family::MatteNormalMapped;
    if (shader.contains(QLatin1String("normalmap")) || shader.contains(QLatin1String("parallax")))
        return Family::NormalMapped;
    return Family::Other;
}

std::shared_ptr<const MaszynaMaterial> MaszynaMaterial::find(const QString &name, const QString &vehicleFolder) {
    return find(name, MaszynaPaths::Lookup::maszyna(vehicleFolder));
}

std::shared_ptr<const MaszynaMaterial> MaszynaMaterial::find(const QString &name, const MaszynaPaths::Lookup &lookup) {
    const QString key = lookup.key() + QLatin1Char('\n') + name + QLatin1Char('\n') + Game::season;
    {
        QMutexLocker lock(&readMutex);
        const auto found = materials.constFind(key);
        if (found != materials.constEnd())
            return found.value();
    }
    const std::shared_ptr<const MaszynaMaterial> material = read(name, lookup);
    QMutexLocker lock(&readMutex);
    materials.insert(key, material);
    return material;
}

bool MaszynaMaterial::exists(const QString &name, const QString &vehicleFolder) {
    return exists(name, MaszynaPaths::Lookup::maszyna(vehicleFolder));
}

bool MaszynaMaterial::exists(const QString &name, const MaszynaPaths::Lookup &lookup) {
    const MaszynaPaths::Name cleaned = MaszynaPaths::clean(name, lookup.rules);
    const QStringList places = MaszynaPaths::places(cleaned.text, lookup);
    return !MaszynaPaths::findMaterial(places).isEmpty() || !MaszynaPaths::findTexture(places).isEmpty();
}

void MaszynaMaterial::forget() {
    QMutexLocker lock(&readMutex);
    materials.clear();
}
