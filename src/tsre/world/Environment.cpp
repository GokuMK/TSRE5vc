/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/fileFunctions/ContentPath.h>
#include <tsre/world/Environment.h>
#include <tsre/Game.h>
#include <QDebug>
#include <QFileInfo>
#include <algorithm>

Environment::Environment(QString path, Env::SlotWeather slot) : slot(slot) {
    loaded = false;
    texturePath = Game::root + "/ROUTES/" + Game::route + "/ENVFILES/TEXTURES/";
    path = ContentPath::normalize(path);
    this->path = path;
    QString error;
    if (!file.read(path, error)) {
        qWarning() << "Environment file" << path << "not read:" << error;
        weather = file.weatherOrDerived(slot);
        return;
    }
    for (const QString &warning : file.warnings)
        qWarning() << "Environment file" << path << warning;
    if (file.world && file.world->water) {
        const Env::Water &w = *file.world->water;
        waterCount = w.layerCount;
        for (const Env::WaterLayer &layer : w.layers) {
            WaterLayer drawn;
            drawn.height = layer.height.value_or(0.0f);
            // The last texture slot, as the previous reader took it.
            if (layer.shader && layer.shader->shader && !layer.shader->shader->texSlots.isEmpty())
                drawn.tex = texturePath + layer.shader->shader->texSlots.back().file;
            water.push_back(drawn);
        }
    }
    weather = file.weatherOrDerived(slot);
    QString mapError;
    if (!weather.loadMaps(QFileInfo(path).absolutePath(), mapError))
        qWarning() << "Environment file" << path << mapError;
    loaded = true;
}

Environment::~Environment() {
}

int Environment::surfaceWaterLayer() const {
    int surface = -1;
    for (int i = 0; i < waterCount && i < water.size(); i++)
        if (surface < 0 || water[i].height >= water[surface].height)
            surface = i;
    return surface;
}

QVector<int> Environment::lowerWaterLayers(int count) const {
    const int surface = surfaceWaterLayer();
    QVector<int> layers;
    for (int i = 0; i < waterCount && i < water.size(); i++)
        if (i != surface)
            layers.push_back(i);
    std::stable_sort(layers.begin(), layers.end(), [this](int a, int b) {
        return water[a].height < water[b].height;
    });
    return layers.mid(0, count);
}
