#version 330 core

in vec2 vTextureCoord;
in float fogFactor;
in vec3 vNormal;
in float vAlpha;
in float vTerrainGap;
in vec2 vTerrainMapCoord;
out vec4 fragColor;

uniform float textureEnabled;
uniform vec4 shapeColor;
// 1 - RenderItem::Material::opacity; 0 (opaque) where nothing sets it.
uniform float materialTransparency;
uniform vec4 skyColor;
uniform vec4 diffuseColor;
uniform vec4 ambientColor;
uniform vec3 lightDirection;
uniform sampler2D uSampler;
uniform sampler2D uSampler2;
uniform float secondTexEnabled;
uniform mat4 uMVMatrix;
flat in mat4 vModelView;
uniform mat4 uMSMatrix;
uniform float enableNormals;
uniform float colorBrightness;

#ifdef TSRE_TERRAIN
#include "TerrainMaterial.glsl"
#endif

void main() {
#ifdef TSRE_TERRAIN
    if (vTerrainGap > 0.0)
        discard;
#endif

    if (textureEnabled == 0.0) {
        fragColor = shapeColor;
        fragColor.a *= 1.0 - materialTransparency;
        return;
    }

    fragColor = texture(uSampler, vTextureCoord);
    if (secondTexEnabled != 0.0) {
        vec4 detail = texture(uSampler2, vTextureCoord * secondTexEnabled);
        fragColor *= detail * 2.0;
    }
#ifdef TSRE_TERRAIN
    if (terrainMaterialEnabled != 0)
        fragColor = terrainMaterialColor();
#endif
    fragColor.a = max(fragColor.a, vAlpha);
    if (fragColor.a < -vAlpha)
        discard;
    fragColor.a *= 1.0 - materialTransparency;

#ifdef TSRE_UNLIT
    // Overlays: no sun lighting, shadows or fog.
    fragColor.xyz *= colorBrightness;
#else
    vec3 normal = normalize(mat3(vModelView) * mat3(uMSMatrix) * vNormal);
    float cosTheta = clamp(dot(normal, normalize(lightDirection)), 0.0, 1.0);
    float visibility = (1.0 - enableNormals) + cosTheta * enableNormals;
    fragColor.rgb *= (diffuseColor.rgb * clamp(visibility, 0.0, 1.0)
            + ambientColor.rgb)
            * colorBrightness;
    // Fog tints the colour only; mixing alpha towards the sky's 1 made
    // translucent surfaces turn opaque with distance.
    fragColor.rgb = mix(fragColor.rgb, skyColor.rgb, fogFactor);
#endif
}
