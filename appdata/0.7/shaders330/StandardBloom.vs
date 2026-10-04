#version 330 core

in vec4 vertex;
in vec4 normal;
in vec2 aTextureCoord;
in float alpha;

uniform float lod;
uniform mat4 uShadowPMatrix;
uniform mat4 uShadow2PMatrix;
uniform mat4 uPMatrix;
uniform mat4 uFMatrix;
uniform mat4 uMVMatrix;
uniform mat4 uMSMatrix;
#include "Instancing.glsl"
uniform float fogDensity;
#ifdef TSRE_TERRAIN
uniform int terrainPaged;
// Zero is identity. A procedural patch can sample its tile bake without a mesh rebuild.
uniform vec3 terrainTextureRemap;
uniform vec3 terrainMaterialMapRemap;
uniform int terrainVerticesPerPatch;
uniform int terrainPatchSide;
uniform float terrainSampleSpacing;
uniform int terrainApplyGaps;
uniform int terrainMapPass;

struct TerrainPatchParams {
    vec4 uvAndOriginX;
    vec4 uvAndOriginZ;
};
layout(std140) uniform TerrainPatchBlock {
    TerrainPatchParams terrainPatch[256];
};
#endif

out vec2 vTextureCoord;
// Model-view of this vertex's instance, for the fragment lighting.
flat out mat4 vModelView;
out float fogFactor;
out vec3 vNormal;
out vec4 shadowPos;
out vec4 shadow2Pos;
out float vAlpha;
out float vTerrainGap;
out vec2 vTerrainMapCoord;

void main() {
    mat4 modelView = instanceModelView();
    vModelView = modelView;
    vec4 renderVertex = vertex;
    vec2 renderUv = aTextureCoord;
#ifdef TSRE_TERRAIN
    if (terrainPaged != 0) {
        int patchSlot = gl_VertexID / terrainVerticesPerPatch;
        int localVertexId = gl_VertexID - patchSlot * terrainVerticesPerPatch;
        int localSampleZ = localVertexId / terrainPatchSide;
        int localSampleX = localVertexId - localSampleZ * terrainPatchSide;
        vec2 terrainLocalSample = vec2(float(localSampleX), float(localSampleZ));
        TerrainPatchParams params = terrainPatch[patchSlot];
        renderVertex = vec4(params.uvAndOriginX.w + terrainLocalSample.x * terrainSampleSpacing,
                            vertex.x,
                            params.uvAndOriginZ.w + terrainLocalSample.y * terrainSampleSpacing,
                            1.0);
        renderUv = vec2(terrainLocalSample.x * params.uvAndOriginX.x
                        + terrainLocalSample.y * params.uvAndOriginX.y
                        + params.uvAndOriginX.z,
                        terrainLocalSample.x * params.uvAndOriginZ.x
                        + terrainLocalSample.y * params.uvAndOriginZ.y
                        + params.uvAndOriginZ.z);
    }
#endif
    shadowPos = uShadowPMatrix * modelView * uMSMatrix * renderVertex;
    shadow2Pos = uShadow2PMatrix * modelView * uMSMatrix * renderVertex;
    gl_Position = uPMatrix * modelView * uMSMatrix * renderVertex;
    vec4 fogPosition = uFMatrix * modelView * uMSMatrix * renderVertex;
#ifdef TSRE_TERRAIN
    vTextureCoord = renderUv * (1.0 + terrainTextureRemap.x) + terrainTextureRemap.yz;
    vTerrainMapCoord = renderUv * terrainMaterialMapRemap.x + terrainMaterialMapRemap.yz;
#else
    vTextureCoord = renderUv;
    vTerrainMapCoord = vec2(0.0);
#endif

    fogFactor = sqrt((fogPosition.x)*(fogPosition.x) + (fogPosition.z)*(fogPosition.z))/(lod*1.4);
    fogFactor = clamp(fogFactor, 0.0, fogDensity);
    fogFactor = min(fogFactor, lod);
    fogFactor = abs(fogFactor);


    vNormal = normal.xyz;
#ifdef TSRE_TERRAIN
    vAlpha = terrainPaged != 0
            ? (terrainMapPass != 0 ? -0.01 : 0.0) : alpha;
    vTerrainGap = terrainPaged != 0 && terrainApplyGaps != 0 ? normal.w : 0.0;
#else
    vAlpha = alpha;
    vTerrainGap = 0.0;
#endif

}
