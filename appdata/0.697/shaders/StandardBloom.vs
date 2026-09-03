#version 140

attribute vec4 vertex;
attribute vec4 normal;
attribute vec2 aTextureCoord;
attribute float alpha;

uniform float lod;
uniform mat4 uShadowPMatrix;
uniform mat4 uShadow2PMatrix;
uniform mat4 uPMatrix;
uniform mat4 uFMatrix;
uniform mat4 uMVMatrix;
uniform mat4 uMSMatrix;
uniform float fogDensity;
uniform int terrainPaged;
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

varying vec2 vTextureCoord;
varying float fogFactor;
varying vec3 vNormal;
varying vec4 shadowPos;
varying vec4 shadow2Pos;
varying float vAlpha;
varying float vTerrainGap;

void main() {
    vec4 renderVertex = vertex;
    vec2 renderUv = aTextureCoord;
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
    shadowPos = uShadowPMatrix * uMVMatrix * uMSMatrix * renderVertex;
    shadow2Pos = uShadow2PMatrix * uMVMatrix * uMSMatrix * renderVertex;
    gl_Position = uPMatrix * uMVMatrix * uMSMatrix * renderVertex;
    vec4 fogPosition = uFMatrix * uMVMatrix * uMSMatrix * renderVertex;
    vTextureCoord = renderUv;

    fogFactor = sqrt((fogPosition.x)*(fogPosition.x) + (fogPosition.z)*(fogPosition.z))/(lod*1.4);
    fogFactor = clamp(fogFactor, 0.0, fogDensity);
    fogFactor = min(fogFactor, lod);
    fogFactor = abs(fogFactor);


    vNormal = normal.xyz;
    vAlpha = terrainPaged != 0
            ? (terrainMapPass != 0 ? -0.01 : 0.0) : alpha;
    vTerrainGap = terrainPaged != 0 && terrainApplyGaps != 0 ? normal.w : 0.0;

}
