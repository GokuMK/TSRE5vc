#pragma once

#include <array>
#include <cstdint>

class FileBuffer;
class QTextStream;

// Narrow adapters for integer fields in the legacy text reader. Scientific
// integer literals from older TSRE saves are accepted without a float32 step.
namespace TrackNodeText {
std::int32_t readInt(FileBuffer *data);
std::uint32_t readUInt(FileBuffer *data, bool optional = false);
}

// Serialized TDB coordinates and angles; keep their existing axis conventions.
// World ownership is independent of the tile containing the geometric point.
struct TrackNodeUid {
    std::int32_t worldTileX = 0, worldTileZ = 0;
    std::uint32_t worldObjectId = 0, worldEndpointIndex = 0;
    std::int32_t tileX = 0, tileZ = 0;
    float x = 0, y = 0, z = 0;
    float ax = 0, ay = 0, az = 0;

    void load(FileBuffer *data);
    void save(QTextStream &out) const;
    std::array<float, 3> frame() const { return {ax, ay, az}; }
};

struct TrackVectorSection {
    std::uint32_t sectionIndex = 0, shapeIndex = 0;
    std::int32_t worldTileX = 0, worldTileZ = 0;
    std::uint32_t worldObjectId = 0;
    std::uint32_t startEndpointIndex = 0, endEndpointIndex = 0;
    std::uint8_t opaqueByte = 0; // Native hexadecimal byte; bit meanings unknown.
    std::int32_t tileX = 0, tileZ = 0;
    float x = 0, y = 0, z = 0;
    float ax = 0, ay = 0, az = 0;

    void load(FileBuffer *data);
    void save(QTextStream &out) const;
    std::array<float, 3> frame() const { return {ax, ay, az}; }
    void setFrame(const std::array<float, 3> &value) {
        ax = value[0]; ay = value[1]; az = value[2];
    }
};

struct TrackPin {
    int link = 0;
    int direction = 0;
};

struct JunctionData {
    std::uint32_t unknown0 = 0;
    std::uint32_t shapeIndex = 0;
    std::uint32_t unknown2 = 0;
};
