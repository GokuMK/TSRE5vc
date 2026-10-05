// Direct procedural terrain, drawn in one pass: picks this pixel's material
// from the categorical material map and samples its layer of the material
// arrays. Included by terrain shader variants.
uniform int terrainMaterialEnabled;
uniform sampler2D terrainMaterialMap;
// Per material id: base layer, detail layer (-1 for none), detail scale.
uniform sampler2D terrainMaterialParams;
uniform sampler2DArray terrainMaterialTextures;
uniform sampler2DArray terrainMaterialDetails;
uniform vec3 terrainMaterialMapRemap;
uniform int terrainMaterialMapSide;
uniform float terrainMaterialNoiseScale;

uint terrainScatterHash(uint value) {
    value ^= value >> 16; value *= 0x7feb352du;
    value ^= value >> 15; value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

int terrainMapId(ivec2 position) {
    ivec2 safePosition=clamp(position,ivec2(0),ivec2(terrainMaterialMapSide-1));
    vec2 uv=(vec2(safePosition)+vec2(0.5))/float(terrainMaterialMapSide);
    return int(floor(texture(terrainMaterialMap,uv).r*255.0+0.5));
}

int selectedTerrainMaterial() {
    vec2 mapPosition=clamp(vTerrainMapCoord,vec2(0.0),vec2(1.0))*float(terrainMaterialMapSide)-vec2(0.5);
    ivec2 cell=ivec2(floor(mapPosition));
    vec2 fraction=fract(mapPosition);
    float weights[4]=float[4]((1.0-fraction.x)*(1.0-fraction.y),
                             fraction.x*(1.0-fraction.y),
                             (1.0-fraction.x)*fraction.y,
                             fraction.x*fraction.y);
    int ids[4]=int[4](terrainMapId(cell),terrainMapId(cell+ivec2(1,0)),
                      terrainMapId(cell+ivec2(0,1)),terrainMapId(cell+ivec2(1,1)));
    uvec2 noisePixel=uvec2(max(floor(vTextureCoord*terrainMaterialNoiseScale),vec2(0.0)));
    uint seed=noisePixel.x*0x9e3779b9u ^ noisePixel.y*0x85ebca6bu ^ 0x73518u;
    float probability=float(terrainScatterHash(seed))/4294967296.0;
    for (int i=0;i<3;++i) {
        if (probability<weights[i]) return ids[i];
        probability-=weights[i];
    }
    return ids[3];
}

// Colour of this pixel's material, with its detail texture.
vec4 terrainMaterialColor() {
    vec4 params = texelFetch(terrainMaterialParams, ivec2(selectedTerrainMaterial(), 0), 0);
    vec4 color = texture(terrainMaterialTextures, vec3(vTextureCoord, params.x));
    if (params.y >= 0.0 && params.z != 0.0)
        color *= texture(terrainMaterialDetails, vec3(vTextureCoord * params.z, params.y)) * 2.0;
    return color;
}
