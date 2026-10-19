/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/shape/Impostor.h>
#include <tsre/environment/EnvNode.h>
#include <tsre/fileFunctions/ContentPath.h>
#include <QFileInfo>
#include <cmath>

namespace {

const EnvNode *child(const EnvNode &block, const QString &name) {
    for (const EnvNode &item : block.items)
        if (item.block && item.text.compare(name, Qt::CaseInsensitive) == 0)
            return &item;
    return nullptr;
}

// A block's first values as finite numbers.
bool numbers(const EnvNode *block, float *out, int count) {
    if (block == nullptr)
        return false;
    for (int i = 0; i < count; ++i) {
        double value = 0.0;
        if (!block->numberAt(i, value) || !std::isfinite(value))
            return false;
        out[i] = float(value);
    }
    return true;
}

QString text(const EnvNode *block) {
    const QStringList values = block != nullptr ? block->values() : QStringList();
    return values.isEmpty() ? QString() : values.first();
}

}

void Impostor::Frame::axes(float right[3], float up[3]) const {
    const float *d = direction;
    // up x D; along the up axis itself, x.
    float r[3] = {d[2], 0.0f, -d[0]};
    float length = std::sqrt(r[0] * r[0] + r[2] * r[2]);
    if (length < 1e-6f) {
        r[0] = 1.0f;
        r[2] = 0.0f;
        length = 1.0f;
    }
    for (int i = 0; i < 3; ++i)
        right[i] = r[i] / length;
    up[0] = d[1] * right[2] - d[2] * right[1];
    up[1] = d[2] * right[0] - d[0] * right[2];
    up[2] = d[0] * right[1] - d[1] * right[0];
}

QString Impostor::besideShape(const QString &shapePath) {
    const QFileInfo info(ContentPath::normalize(shapePath));
    return ContentPath::join(info.path(), info.completeBaseName() + ".impostor");
}

bool Impostor::read(const QString &path, Impostor &out, QString &error) {
    QVector<EnvNode> items;
    if (!EnvText::read(path, items, error))
        return false;
    const EnvNode *root = nullptr;
    for (const EnvNode &item : items)
        if (item.block && item.text.compare("Impostor", Qt::CaseInsensitive) == 0)
            root = &item;
    if (root == nullptr) {
        error = "no Impostor block";
        return false;
    }
    float version = 0.0f;
    if (!numbers(child(*root, "Version"), &version, 1) || version != 1.0f) {
        error = "not version 1";
        return false;
    }
    Impostor impostor;
    impostor.colorTexture = text(child(*root, "ColorTexture"));
    if (impostor.colorTexture.isEmpty()) {
        error = "no ColorTexture";
        return false;
    }
    impostor.normalTexture = text(child(*root, "NormalTexture"));
    impostor.backlightTexture = text(child(*root, "BacklightTexture"));
    // A backlight texture shines at full strength unless told otherwise.
    if (!impostor.backlightTexture.isEmpty())
        impostor.backlightStrength = 1.0f;
    numbers(child(*root, "BacklightStrength"), &impostor.backlightStrength, 1);
    numbers(child(*root, "AlphaCutoff"), &impostor.alphaCutoff, 1);
    numbers(child(*root, "Roughness"), &impostor.roughness, 1);
    numbers(child(*root, "Specular"), &impostor.specular, 1);
    const QString binding = text(child(*root, "LodBinding"));
    if (!binding.isEmpty() && binding.compare("Append", Qt::CaseInsensitive) != 0
            && binding.compare("ReplaceLast", Qt::CaseInsensitive) != 0) {
        error = "unknown LodBinding " + binding;
        return false;
    }
    impostor.replaceLast = binding.compare("ReplaceLast", Qt::CaseInsensitive) == 0;
    if (child(*root, "Pivot") != nullptr && !numbers(child(*root, "Pivot"), impostor.pivot, 3)) {
        error = "invalid Pivot";
        return false;
    }
    if (!numbers(child(*root, "Bounds"), impostor.bounds, 6)) {
        error = "no valid Bounds";
        return false;
    }
    const EnvNode *frames = child(*root, "Frames");
    for (int i = 0; frames != nullptr && i < frames->items.size(); ++i) {
        const EnvNode &block = frames->items[i];
        if (!block.block || block.text.compare("Frame", Qt::CaseInsensitive) != 0)
            continue;
        Frame frame;
        if (!numbers(child(block, "Direction"), frame.direction, 3) || !numbers(child(block, "UVRect"), frame.uv, 4)
                || !numbers(child(block, "Extent"), frame.extent, 4)) {
            error = QString("frame %1: missing or invalid values").arg(impostor.frames.size());
            return false;
        }
        const float *d = frame.direction;
        const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        const float *uv = frame.uv;
        const float *e = frame.extent;
        if (std::abs(length - 1.0f) > 0.01f || uv[0] < 0.0f || uv[1] < 0.0f || uv[2] > 1.0f || uv[3] > 1.0f
                || uv[0] >= uv[2] || uv[1] >= uv[3] || e[0] >= e[1] || e[2] >= e[3]) {
            error = QString("frame %1: a direction not of unit length, a cell outside the atlas or an empty extent")
                    .arg(impostor.frames.size());
            return false;
        }
        for (int c = 0; c < 3; ++c)
            frame.direction[c] /= length;
        impostor.frames.push_back(frame);
    }
    if (impostor.frames.isEmpty()) {
        error = "no frames";
        return false;
    }
    out = impostor;
    return true;
}

int Impostor::frameFor(const float direction[3]) const {
    int best = 0;
    float bestDot = -2.0f;
    for (int i = 0; i < frames.size(); ++i) {
        const float *d = frames[i].direction;
        const float dot = d[0] * direction[0] + d[1] * direction[1] + d[2] * direction[2];
        if (dot > bestDot) {
            bestDot = dot;
            best = i;
        }
    }
    return best;
}
