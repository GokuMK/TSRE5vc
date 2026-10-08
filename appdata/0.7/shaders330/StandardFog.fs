#version 330 core

in vec2 vTextureCoord;
in float fogFactor;
in vec3 vNormal;
in vec4 shadowPos;
in vec4 shadow2Pos;
in vec4 shadow0Pos;
in float vAlpha;
in float vTerrainGap;
in vec2 vTerrainMapCoord;
#ifdef TSRE_RHI
in float vClipDistance;
in vec3 vLightPosition;
#endif
out vec4 fragColor;
#ifdef TSRE_RHI
// The share of fragColor that ambient and environment light make, for
// ambient occlusion: the renderer subtracts the occluded part of it.
layout(location = 1) out vec4 ambientOut;
// Emitted light in linear colour, the source of bloom: only what glows,
// never what is merely bright.
layout(location = 2) out vec4 glowOut;
// Set by pbrShade.
vec4 pbrAmbientOut = vec4(0.0);
vec4 pbrGlowOut = vec4(0.0);
#endif

uniform float textureEnabled;
uniform int shadowsEnabled;
uniform float shadow1Res;
uniform float shadow2Res;
uniform float shadow2Bias;
uniform vec4 shapeColor;
uniform float isAlpha;
uniform float alphaTest;
uniform vec4 skyColor;
uniform vec4 diffuseColor;
uniform vec4 ambientColor;
uniform vec4 specularColor;
uniform vec3 lightDirection;
uniform sampler2D uSampler;
uniform sampler2D uSampler2;
uniform sampler2DShadow shadow1;
uniform sampler2DShadow shadow2;
uniform sampler2DShadow shadow0;
uniform float secondTexEnabled;
uniform mat4 uMVMatrix;
flat in mat4 vModelView;
uniform mat4 uMSMatrix;
uniform float enableNormals;
uniform float colorBrightness;

vec2 poissonDisk[16] = vec2[]( 
   vec2( -0.94201624, -0.39906216 ), 
   vec2( 0.94558609, -0.76890725 ), 
   vec2( -0.094184101, -0.92938870 ), 
   vec2( 0.34495938, 0.29387760 ), 
   vec2( -0.91588581, 0.45771432 ), 
   vec2( -0.81544232, -0.87912464 ), 
   vec2( -0.38277543, 0.27676845 ), 
   vec2( 0.97484398, 0.75648379 ), 
   vec2( 0.44323325, -0.97511554 ), 
   vec2( 0.53742981, -0.47373420 ), 
   vec2( -0.26496911, -0.41893023 ), 
   vec2( 0.79197514, 0.19090188 ), 
   vec2( -0.24188840, 0.99706507 ), 
   vec2( -0.81409955, 0.91437590 ), 
   vec2( 0.19984126, 0.78641367 ), 
   vec2( 0.14383161, -0.14100790 ) 
);

float random(vec3 seed, int i){
	vec4 seed4 = vec4(seed,i);
	float dot_product = dot(seed4, vec4(12.9898,78.233,45.164,94.673));
	return fract(sin(dot_product) * 43758.5453);
}

float insideBox(vec2 v, vec2 bottomLeft, vec2 topRight) {
    vec2 s = step(bottomLeft, v) - step(topRight, v);
    return s.x * s.y;   
}

#ifdef TSRE_TERRAIN
#include "TerrainMaterial.glsl"
#endif
#include "ShadowSampling.glsl"
#ifdef TSRE_RHI
#include "LocalLights.glsl"
#endif
#ifdef TSRE_PBR
#include "PbrShading.glsl"
#endif
#ifdef TSRE_WATER
#include "WaterShading.glsl"
#endif

void main() {
#ifdef TSRE_RHI
    if (vClipDistance < 0.0)
        discard;
    ambientOut = vec4(0.0);
    glowOut = vec4(0.0);
#endif
#if defined(TSRE_PBR)
        fragColor = pbrShade();
#ifdef TSRE_RHI
        ambientOut = pbrAmbientOut;
        glowOut = pbrGlowOut;
#endif
#elif defined(TSRE_WATER)
        fragColor = textureEnabled != 0.0 ? waterShade() : shapeColor;
#else
#ifdef TSRE_TERRAIN
        if(vTerrainGap > 0.0)
            discard;
#endif
        if(textureEnabled == 0) {
            fragColor = shapeColor;
        } else {
            fragColor = texture(uSampler, vec2(vTextureCoord.s, vTextureCoord.t));
            vec4 tex2 = texture(uSampler2, vec2(vTextureCoord.s*secondTexEnabled, vTextureCoord.t*secondTexEnabled));
            float isSecondTexEnabled = sign(secondTexEnabled);
            fragColor = fragColor*(1-isSecondTexEnabled) + fragColor*tex2*2.0*isSecondTexEnabled;
#ifdef TSRE_TERRAIN
            if(terrainMaterialEnabled != 0)
                fragColor = terrainMaterialColor();
#endif
            // discard if transparent
            //if(gl_FragColor.a < alphaTest)
            //    discard;    
            fragColor.a = max(fragColor.a, vAlpha);  
            // discard if transparent 
            if(fragColor.a < -vAlpha)
                discard;
            //gl_FragColor.a = 1.0;

#ifdef TSRE_UNLIT
            // Overlays: no sun lighting, shadows or fog.
            fragColor.xyz *= colorBrightness;
#else
            // calculate normals
            vec3 normal = normalize(mat3(vModelView) * mat3(uMSMatrix) * vNormal);
            vec3 lights = normalize(lightDirection);
            float cosTheta = clamp(dot( normal, lights ), 0, 1);
            float visibility = shadowedVisibility(cosTheta);

            // calculate light color
            vec3 color = diffuseColor.xyz;
            color *= clamp(visibility, 0.0, 1.0);
            color += ambientColor.xyz;
#ifdef TSRE_RHI
            // Lamps and glowing surfaces add their light in linear terms.
            vec3 local = localLightsDiffuse(vLightPosition, normal, enableNormals);
            if (local != vec3(0.0))
                color = pow(pow(max(color, vec3(0.0)), vec3(2.2)) + local, vec3(1.0 / 2.2));
            ambientOut = vec4(fragColor.rgb * ambientColor.rgb * colorBrightness * (1.0 - fogFactor),
                              fragColor.a);
#endif
            fragColor.xyz *= color*colorBrightness;

            fragColor = mix(fragColor, skyColor, fogFactor);
#endif
        }
#endif
#ifdef TSRE_RHI
    // Floating-point targets (HDR) keep what 8-bit ones clamp: alpha above
    // 1 or negative colour would turn blending around.
    fragColor = vec4(max(fragColor.rgb, vec3(0.0)), clamp(fragColor.a, 0.0, 1.0));
    ambientOut.a = clamp(ambientOut.a, 0.0, 1.0);
#ifndef TSRE_PBR
    // No emission, but a blended surface must still cover the glow behind it
    // as much as its colour covers what is behind: blending uses each
    // output's alpha (alpha 0 left lamps glowing through it).
    glowOut.a = fragColor.a;
#endif
    glowOut = vec4(max(glowOut.rgb, vec3(0.0)), clamp(glowOut.a, 0.0, 1.0));
#endif
}
