/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/traffic/LevelCrossings.h>
#include <tsre/world/objects/LevelCrObj.h>

#include <QVector>
#include <algorithm>
#include <unordered_map>

namespace LevelCrossings {

namespace {

const StateSource *source = nullptr;
unsigned long long sourceGeneration = 0;
std::vector<LevelCrObj *> loaded;
unsigned long long objectsVersion = 0;
// Road item to object, made again when the version changes.
std::unordered_map<int, LevelCrObj *> byRoadItem;
unsigned long long byRoadItemVersion = ~0ull;
double seconds = 0.0;

}

void setStateSource(const StateSource *s) {
    if (s == source)
        return;
    source = s;
    ++sourceGeneration;
}

const StateSource *stateSource() {
    return source;
}

unsigned long long stateSerial() {
    return source != nullptr ? (sourceGeneration << 40) ^ source->crossingSerial() : sourceGeneration << 40;
}

void add(LevelCrObj *object) {
    if (object == nullptr || std::find(loaded.begin(), loaded.end(), object) != loaded.end())
        return;
    loaded.push_back(object);
    ++objectsVersion;
}

void remove(LevelCrObj *object) {
    auto found = std::find(loaded.begin(), loaded.end(), object);
    if (found == loaded.end())
        return;
    loaded.erase(found);
    ++objectsVersion;
}

void changed() {
    ++objectsVersion;
}

unsigned long long version() {
    return objectsVersion;
}

const std::vector<LevelCrObj *> &objects() {
    return loaded;
}

LevelCrObj *ofRoadItem(int roadItem) {
    if (byRoadItemVersion != objectsVersion) {
        byRoadItem.clear();
        QVector<int> items;
        for (LevelCrObj *object : loaded) {
            items.clear();
            object->getTrackItemIds(items, 1);
            for (int item : items)
                if (item >= 0)
                    byRoadItem[item] = object;
        }
        byRoadItemVersion = objectsVersion;
    }
    auto found = byRoadItem.find(roadItem);
    if (found == byRoadItem.end() || !found->second->loaded)
        return nullptr;
    return found->second;
}

void setFrameSeconds(double s) {
    seconds = std::max(0.0, s);
}

double frameSeconds() {
    return seconds;
}

}
