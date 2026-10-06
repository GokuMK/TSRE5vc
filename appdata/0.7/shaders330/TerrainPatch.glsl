// Records of the patches of a paged terrain page: the origin of each patch
// and the mapping of its samples to texture coordinates.
struct TerrainPatchParams {
    vec4 uvAndOriginX;
    vec4 uvAndOriginZ;
};
#ifdef TSRE_RHI
// QRhi: two RGBA32F texels per patch. QRhi's OpenGL backend sets uniform
// blocks member by member on every draw, too slow for 256 records.
uniform sampler2D terrainPatchData;

TerrainPatchParams terrainPatchParams(int slot) {
    return TerrainPatchParams(texelFetch(terrainPatchData, ivec2(slot * 2, 0), 0),
                              texelFetch(terrainPatchData, ivec2(slot * 2 + 1, 0), 0));
}
#else
layout(std140) uniform TerrainPatchBlock {
    TerrainPatchParams terrainPatch[256];
};

TerrainPatchParams terrainPatchParams(int slot) {
    return terrainPatch[slot];
}
#endif
