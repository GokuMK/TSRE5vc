#version 330 core

in vec4 vertex;
in vec4 normal;
in vec2 aTextureCoord;
in float alpha;

uniform float lod;
uniform mat4 uPMatrix;
uniform mat4 uShadowPMatrix;
uniform mat4 uMVMatrix;
uniform mat4 uMSMatrix;
#include "Instancing.glsl"
uniform int terrainPaged;
uniform int terrainVerticesPerPatch;
uniform int terrainPatchSide;
uniform float terrainSampleSpacing;
uniform int terrainApplyGaps;
uniform int terrainMapPass;

#include "TerrainPatch.glsl"

out vec2 vTextureCoord;
out float vTerrainGap;

void main() {
    // QRhi has no packed 2_10_10_10 vertex format: the paged terrain normal
    // and gap flag arrive as unsigned bytes, round(v * 127) + 128.
    vec4 vertexNormal = normal;
#if defined(TSRE_RHI)
    if (terrainPaged != 0)
        vertexNormal = (normal * 255.0 - 128.0) / 127.0;
#endif
    vec4 renderVertex = vertex;
    vec2 renderUv = aTextureCoord;
    if (terrainPaged != 0) {
        int patchSlot = gl_VertexID / terrainVerticesPerPatch;
        int localVertexId = gl_VertexID - patchSlot * terrainVerticesPerPatch;
        int localSampleZ = localVertexId / terrainPatchSide;
        int localSampleX = localVertexId - localSampleZ * terrainPatchSide;
        vec2 terrainLocalSample = vec2(float(localSampleX), float(localSampleZ));
        TerrainPatchParams params = terrainPatchParams(patchSlot);
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
    gl_Position = uShadowPMatrix * instanceModelView() * uMSMatrix * renderVertex;
    vTextureCoord = renderUv;
    vTerrainGap = terrainPaged != 0 && terrainApplyGaps != 0 ? vertexNormal.w : 0.0;
}
