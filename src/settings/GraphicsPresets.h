#ifndef TSRE_GRAPHICSPRESETS_H
#define TSRE_GRAPHICSPRESETS_H

#include <QPair>
#include <QString>
#include <QVariant>
#include <QVector>

// Rendering settings set together for one look. A preset sets only the
// effects that make the look: shadows, reflections, water, lamps, sky,
// clouds, fog, the image's exposure. Sizes, distances and the time keep
// their values.
struct GraphicsPreset {
    QString id;
    const char *nameId;
    const char *descriptionId;
    QVector<QPair<QString, QVariant>> values;
};

namespace GraphicsPresets {
// Open Rails Basic, Open Rails Extended, Advanced Environment.
const QVector<GraphicsPreset> &all();
}

#endif
