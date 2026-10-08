// Records of the patches of a paged terrain page: the origin of each patch
// and the mapping of its samples to texture coordinates.
struct TerrainPatchParams {
    vec4 uvAndOriginX;
    vec4 uvAndOriginZ;
};
// The vertex's index in the page, its patch slot times the vertices per patch
// plus its place in the patch. Direct3D leaves the draw's base vertex out of
// SV_VertexID; the QRhi renderer passes it there (zero elsewhere).
#ifdef TSRE_RHI
uniform int terrainVertexBase;

int terrainVertexId() {
    return gl_VertexID + terrainVertexBase;
}
#else
int terrainVertexId() {
    return gl_VertexID;
}
#endif
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
