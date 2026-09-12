/* This file is part of TSRE5. Licensed under GPL-3.0-or-later; see LICENSE.md. */
#ifndef TELEPOLEOBJ_H
#define TELEPOLEOBJ_H

#include <tsre/world/objects/WorldObj.h>
#include <array>
#include <optional>

// Preserves the FFEDIT Telepole world form. Pole/wire generation is not implemented.
class TelepoleObj : public WorldObj {
public:
    TelepoleObj();
    WorldObj* clone() override;
    void load(int x, int y) override;
    using WorldObj::set;
    void set(TS::TokenId token, FileBuffer* data) override;
    void set(QString token, FileBuffer* data) override;
    void save(QTextStream* out) override;
    bool allowNew() override { return false; }
    void pushRenderItems(float, float, float, float*, float*, float, quint32) override {}
    void render(GLUU*, float, float, float, float*, float*, float, quint32, int) override {}

private:
    using Vector = std::array<float, 3>;
    std::optional<unsigned int> population, startType, endType, config, quality;
    // These fields remain in source coordinates; only WorldObj::position is converted.
    std::optional<Vector> startPosition, endPosition, direction;
    std::optional<float> startDirection, endDirection, maxVisDistance;
    bool hasUid = false, hasPosition = false, hasVdbId = false;
    bool coordinatesConverted = false;
    void readField(TS::TokenId token, FileBuffer* data, bool binary);
};

#endif
