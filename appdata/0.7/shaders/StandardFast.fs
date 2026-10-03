#version 140

varying vec2 vTextureCoord;
varying float fogFactor;
varying vec3 vNormal;
varying float vAlpha;
varying float vTerrainGap;
varying vec2 vTerrainMapCoord;

uniform float textureEnabled;
uniform vec4 shapeColor;
uniform vec4 skyColor;
uniform vec4 diffuseColor;
uniform vec4 ambientColor;
uniform vec3 lightDirection;
uniform sampler2D uSampler;
uniform sampler2D uSampler2;
uniform float secondTexEnabled;
uniform int terrainMaterialEnabled;
uniform sampler2D terrainMaterialMap;
uniform int terrainMaterialId;
uniform vec3 terrainMaterialMapRemap;
uniform int terrainMaterialMapSide;
uniform float terrainMaterialNoiseScale;
uniform mat4 uMVMatrix;
uniform mat4 uMSMatrix;
uniform float enableNormals;
uniform float colorBrightness;

uint terrainScatterHash(uint value) {
    value ^= value >> 16; value *= 0x7feb352du;
    value ^= value >> 15; value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

int terrainMapId(ivec2 position) {
    ivec2 safePosition=clamp(position,ivec2(0),ivec2(terrainMaterialMapSide-1));
    vec2 uv=(vec2(safePosition)+vec2(0.5))/float(terrainMaterialMapSide);
    return int(floor(texture2D(terrainMaterialMap,uv).r*255.0+0.5));
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

void main() {
    if (vTerrainGap > 0.0)
        discard;
    if (terrainMaterialEnabled != 0 && selectedTerrainMaterial() != terrainMaterialId)
        discard;

    if (textureEnabled == 0.0) {
        gl_FragColor = shapeColor;
        return;
    }

    gl_FragColor = texture(uSampler, vTextureCoord);
    if (secondTexEnabled != 0.0) {
        vec4 detail = texture(uSampler2, vTextureCoord * secondTexEnabled);
        gl_FragColor *= detail * 2.0;
    }
    gl_FragColor.a = max(gl_FragColor.a, vAlpha);
    if (gl_FragColor.a < -vAlpha)
        discard;

    vec3 normal = normalize(mat3(uMVMatrix) * mat3(uMSMatrix) * vNormal);
    float cosTheta = clamp(dot(normal, normalize(lightDirection)), 0.0, 1.0);
    float visibility = (1.0 - enableNormals) + cosTheta * enableNormals;
    gl_FragColor.rgb *= (diffuseColor.rgb * clamp(visibility, 0.0, 1.0)
            + ambientColor.rgb)
            * colorBrightness;
    gl_FragColor = mix(gl_FragColor, skyColor, fogFactor);
}
