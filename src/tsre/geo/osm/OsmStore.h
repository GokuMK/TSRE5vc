/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#pragma once

// Query interface over OSM data. Consumers (tile map, procedural generation, vector
// rendering) depend only on this; SortedPbfStore is the first implementation and a
// derived grid store can be added behind the same interface.

#include <tsre/geo/osm/OsmPbf.h>
#include <QString>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Osm {

// One node, way or relation. Valid only during the callback that receives it.
struct Feature {
    ItemType type = ItemType::Node;
    int64_t id = 0;
    Box extent;                              // node: its location; way: its points; relation: its members
    Location location;                       // nodes
    const int64_t *refs = nullptr;           // ways: node ids
    const Location *locations = nullptr;     // ways: coordinates, parallel to refs
    uint32_t refCount = 0;
    const Member *members = nullptr;         // relations
    uint32_t memberCount = 0;

    uint32_t tagCount() const { return tagCount_; }
    Tag tag(uint32_t i) const { return block_->tag(tagFirst_ + i); }
    // Value of key, or an empty view when absent.
    std::string_view value(std::string_view key) const {
        for (uint32_t i = 0; i < tagCount_; ++i) { const Tag t = tag(i); if (t.key == key) return t.value; }
        return {};
    }
    bool has(std::string_view key) const {
        for (uint32_t i = 0; i < tagCount_; ++i) if (tag(i).key == key) return true;
        return false;
    }
    std::string_view role(const Member &m) const { return block_->role(m); }

    // Set by stores.
    void bind(const PrimitiveBlock *block, uint32_t tagFirst, uint32_t tagCount) { block_ = block; tagFirst_ = tagFirst; tagCount_ = tagCount; }

private:
    const PrimitiveBlock *block_ = nullptr;
    uint32_t tagFirst_ = 0, tagCount_ = 0;
};

enum FeatureTypes : uint32_t { Nodes = 1, Ways = 2, Relations = 4, AllTypes = 7 };

struct Filter {
    uint32_t types = AllTypes;
    // A feature matches when it has at least one of these keys; empty matches every feature.
    std::vector<std::string> keys;
    // Only these ids (sorted ascending, any type); empty means no id restriction.
    std::vector<int64_t> ids;
    // Blocks lying wholly inside this box are skipped: the caller read that area already.
    Box readAlready;

    bool matchesTags(const Feature &f) const {
        if (keys.empty()) return true;
        for (uint32_t i = 0; i < f.tagCount(); ++i) {
            const std::string_view k = f.tag(i).key;
            for (const std::string &want : keys) if (k == want) return true;
        }
        return false;
    }
};

using FeatureCallback = std::function<void(const Feature &)>;

class OsmStore {
public:
    virtual ~OsmStore() = default;
    // Calls fn on the calling thread, in a stable order, for each feature whose extent
    // intersects area. A feature present in several sources is reported once.
    virtual bool forEach(const Box &area, const Filter &filter, const FeatureCallback &fn, QString &error) const = 0;
    // Union of the sources' extents; invalid when empty.
    virtual Box bounds() const = 0;
};

}
