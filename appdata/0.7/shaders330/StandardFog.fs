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
#endif
in vec3 vLightPosition;
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
// The highlights' light where brighter than diffuseColor's (a low sun
// through the air; linear, 0 without one): MstsSpecular.glsl.
uniform vec4 specularColor;
uniform vec3 lightDirection;
uniform sampler2D uSampler;
// Mip level bias of the base texture (core.rendering.textureLodBias):
// below 0 sharper, above 0 softer.
uniform float textureLodBias;
uniform sampler2D uSampler2;
uniform sampler2DShadow shadow1;
uniform sampler2DShadow shadow2;
uniform sampler2DShadow shadow0;
uniform float secondTexEnabled;
// MSTS night texture (task 35), shapes' only (the main program): 1 when the
// packet has one; its light's scale (linear, as the lamps'), and under cover.
#ifndef TSRE_TERRAIN
#ifndef TSRE_PBR
#ifndef TSRE_WATER
uniform sampler2D uNightSampler;
uniform float nightTextureEnabled;
uniform float nightScale;
uniform float nightEnclosedScale;
#endif
#endif
#endif
uniform mat4 uMVMatrix;
flat in mat4 vModelView;
uniform mat4 uMSMatrix;
uniform float enableNormals;
uniform float colorBrightness;
#ifdef TSRE_RHI
// Undoes auto exposure (task 29) for unlit shading, designed for the screen
// (display space; 1 without it).
uniform float unlitScale;
#endif
// 1 - RenderItem::Material::opacity; 0 (opaque) where nothing sets it.
uniform float materialTransparency;
// Blended packets drawn in two, as Open Rails draws them
// (QueueRenderer::BlendedTexels): 1 only the opaque texels (from Open
// Rails' 250 of 255), 2 only the others, 0 all.
uniform int blendedTexels;
const float BlendedOpaqueAlpha = 250.0 / 255.0;
// The translucent texels (2) write no depth, so they cover nothing they let
// through: only the nearly invisible ones are left out, not those under the
// vertices' threshold (0.3 for blended parts, which hid faint glass).
const float BlendedTranslucentAlpha = 4.0 / 255.0;

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
// Drawing the map's terrain layer: the texture's alpha is kept.
uniform int terrainMapPass;
#endif
#include "ShadowSampling.glsl"
#ifdef TSRE_RHI
#include "LocalLights.glsl"
#include "Haze.glsl"
#endif
#ifdef TSRE_PBR
#include "PbrShading.glsl"
#endif
#ifdef TSRE_WATER
#include "WaterShading.glsl"
#endif
#if !defined(TSRE_PBR) && !defined(TSRE_WATER) && !defined(TSRE_TERRAIN)
#include "MstsSpecular.glsl"
#endif
#if !defined(TSRE_PBR) && !defined(TSRE_WATER)
#include "EnvironmentLighting.glsl"
// The shade's light (display colour): the ambient's brightness in the
// colour of the sky's light from above, the light PBR's shade takes (the
// environment cube's roughest level straight up, PbrShading.glsl): a clear
// sky's blue, a cloud's grey, a sunset's glow. In plain grey, shadows here
// were grey beside PBR's blue ones (the user, 2026-10-27). In the scene's
// scale, as PBR's (a thirtieth at night with auto exposure): no floor but
// zero's. The sky's light from above comes from the atmosphere (skyAbove,
// QRhi with a physical sky), the same wherever the camera stands: the cube,
// drawn from the camera, saw a platform roof over it and gave the whole view
// the roof's colour (the user, 2026-10-28). The cube's without it.
uniform vec4 skyAbove;

vec3 ambientLight() {
    vec3 sky = skyLight(skyAbove.w > 0.0 ? skyAbove.rgb : environmentRadiance(vec3(0.0, 1.0, 0.0), 1.0));
    float skyLuminance = luminance(sky);
    if (skyLuminance < 1e-12)
        return ambientColor.rgb;
    return pow(sky * (luminance(toLinear(ambientColor.rgb)) / skyLuminance), vec3(1.0 / 2.2));
}

// The sun's and the sky's light on a surface (display colour): as bright as
// MSTS sums them (display colours added), in the colour they make together
// (linear light, as PBR's). Summed as display colours, the sky's blue
// outweighed the sun's warmth on sunlit ground (Prudnik at noon cold, the
// user, 2026-10-27); in the shade the sky's alone, grey light as before.
vec3 mixedLight(vec3 sun, vec3 sky) {
    vec3 summed = sun + sky;
    vec3 mixed = toLinear(sun) + toLinear(sky);
    float mixedLuminance = luminance(mixed);
    if (mixedLuminance < 1e-8)
        return summed;
    return pow(mixed * (luminance(toLinear(summed)) / mixedLuminance), vec3(1.0 / 2.2));
}
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
            fragColor.a *= 1.0 - materialTransparency;
#ifdef TSRE_RHI
            // Plain colours (editor lines, markers) are for the screen, never
            // lit: undo auto exposure (task 29).
            fragColor.rgb *= unlitScale;
#endif
        } else {
            fragColor = texture(uSampler, vec2(vTextureCoord.s, vTextureCoord.t), textureLodBias);
            vec4 tex2 = texture(uSampler2, vec2(vTextureCoord.s*secondTexEnabled, vTextureCoord.t*secondTexEnabled));
            float isSecondTexEnabled = sign(secondTexEnabled);
            fragColor = fragColor*(1-isSecondTexEnabled) + fragColor*tex2*2.0*isSecondTexEnabled;
            vec3 dayTexel = fragColor.rgb;
#ifdef TSRE_TERRAIN
            if(terrainMaterialEnabled != 0)
                fragColor = terrainMaterialColor();
#endif
            // discard if transparent
            //if(gl_FragColor.a < alphaTest)
            //    discard;    
            fragColor.a = max(fragColor.a, vAlpha);  
#ifdef TSRE_TERRAIN
            // Terrain is opaque, as Open Rails draws it: a texture's alpha
            // let the sky behind it through (Prudnik's fields and forest
            // floors pale; the view blends) or, below 0.01, cut holes.
            if(terrainMapPass == 0)
                fragColor.a = 1.0;
#endif
            // discard if transparent 
            if(fragColor.a < (blendedTexels == 2 ? min(-vAlpha, BlendedTranslucentAlpha) : -vAlpha))
                discard;
            if(blendedTexels == 1) {
                if(fragColor.a < BlendedOpaqueAlpha)
                    discard;
                fragColor.a = 1.0;
            } else if(blendedTexels == 2 && fragColor.a >= BlendedOpaqueAlpha) {
                discard;
            }
            // Alpha tested (-0.51): what passes is opaque, as Open Rails and
            // glTF's MASK draw it. Blended at its filtered alpha, a cutout's
            // edge took the colour of what was drawn before it (the sky) and
            // hid the leaves behind drawn after: pale rings round the holes
            // of trees.
            if(vAlpha < -0.5)
                fragColor.a = 1.0;
            fragColor.a *= 1.0 - materialTransparency;
            //gl_FragColor.a = 1.0;

#ifdef TSRE_UNLIT
            // Overlays: no sun lighting, shadows or fog.
            fragColor.xyz *= colorBrightness;
#ifdef TSRE_RHI
            fragColor.xyz *= unlitScale;
#endif
#else
            // calculate normals
            vec3 normal = normalize(mat3(vModelView) * mat3(uMSMatrix) * vNormal);
            vec3 lights = normalize(lightDirection);
            float cosTheta = clamp(dot( normal, lights ), 0, 1);
            float visibility = shadowedVisibility(cosTheta);
            // What the shadows leave of the sun, without the N.L the visibility
            // starts from (the MSTS specular has its own).
            float sunVisible = clamp(1.0 - cosTheta + visibility, 0.0, 1.0);
#ifdef TSRE_RHI
            float sunPassing = cloudShadow(vLightPosition);
            float shade;
            float enclosed = skyEnclosed(vLightPosition, normal, shade);
            sunPassing *= enclosureSun(vLightPosition, shade, lights);
            visibility *= sunPassing;
            sunVisible *= sunPassing;
#endif

            // calculate light color
            vec3 color = diffuseColor.xyz;
            color *= clamp(visibility, 0.0, 1.0);
            vec3 ambient = ambientLight();
#ifdef TSRE_RHI
            // Under cover the shade's light comes from what is around it
            // (ground, walls) and the horizon rather than the sky overhead:
            // part way to the neutral ambient and darker the farther in
            // (underCover), so a platform's floor under its roof no longer
            // takes the open sky's full blue and light. The enclosure's own
            // darkening (shade) counts a roof open at its sides as open;
            // where it darkens more (a tunnel) it stays.
            float under = enclosed > 0.0 ? underCover(vLightPosition) : 0.0;
            ambient = mix(ambient, ambientColor.rgb, UnderCoverColour * under);
            float skyShare = min(skyAmbient(shade), mix(1.0, UnderCoverLight, under));
            color = mixedLight(color, ambient * skyShare);
#else
            color = mixedLight(color, ambient);
#endif
#ifdef TSRE_RHI
            // Lamps and glowing surfaces add their light in linear terms.
            vec3 local = localLightsDiffuse(vLightPosition, normal, enableNormals) * lampScale(shade);
            if (local != vec3(0.0))
                color = pow(pow(max(color, vec3(0.0)), vec3(2.2)) + local, vec3(1.0 / 2.2));
            ambientOut = vec4(fragColor.rgb * ambient * skyShare * colorBrightness
                              * (hazeOn() ? hazeThrough(vLightPosition) : vec3(1.0 - fogFactor)),
                              fragColor.a);
#endif
            fragColor.xyz *= color*colorBrightness;
            // The night texture's light (task 35) over the lit day texture, the
            // brighter of the two in linear terms: by night the shape as its
            // author made it, lit windows at dusk and in shadow; under cover as
            // the lamps. (Added, a night texture copying the day one doubled a
            // facade still lit by the dusk: the lamps' scale is full from
            // sunset.)
#ifndef TSRE_TERRAIN
            if (nightTextureEnabled != 0.0) {
#ifdef TSRE_RHI
                float nightLight = mix(nightScale, nightEnclosedScale, enclosed);
#else
                float nightLight = nightScale;
#endif
                vec3 nightTexel = pow(texture(uNightSampler, vTextureCoord).rgb, vec3(2.2));
                vec3 night = nightTexel * (nightLight * colorBrightness);
                fragColor.rgb = pow(max(pow(max(fragColor.rgb, vec3(0.0)), vec3(2.2)), night), vec3(1.0 / 2.2));
#ifdef TSRE_RHI
                // The glow (a bonus): where the night texture is painted
                // brighter than the day one (lit windows, lamps), it shines past
                // white and feeds the bloom. Night textures keep the parts lit
                // by lamps at their day brightness (whole fronts in black
                // masks, copies): those are self-lit, not glowing.
                vec3 glow = max(nightTexel - pow(max(dayTexel, vec3(0.0)), vec3(2.2)) * 1.25, vec3(0.0))
                        * (nightLight * colorBrightness * 2.0);
                fragColor.rgb = pow(pow(fragColor.rgb, vec3(2.2)) + glow, vec3(1.0 / 2.2));
                glowOut.rgb += glow * (hazeOn() ? hazeThrough(vLightPosition) : vec3(1.0 - fogFactor));
#endif
            }
            // MSTS specular light materials (task shapes 13), over the lit
            // texture in linear terms.
            if (materialSpecular > 0.0 && enableNormals != 0.0) {
#ifdef TSRE_RHI
                vec3 skyLight = ambient * skyShare;
#else
                vec3 skyLight = ambient;
#endif
                vec3 shine = mstsSpecular(normal, lights, sunVisible, skyLight)
                        * colorBrightness;
                fragColor.rgb = pow(pow(max(fragColor.rgb, vec3(0.0)), vec3(2.2)) + shine, vec3(1.0 / 2.2));
            }
#endif

            // Fog tints the colour only; mixing alpha towards the sky's 1 made
            // translucent surfaces turn opaque with distance.
#ifdef TSRE_RHI
            // Under cover the haze is lit as the ambient is: no bright fog
            // deep in a tunnel.
            if (hazeOn())
                fragColor.rgb = applyHaze(fragColor.rgb, vLightPosition, 1.0, skyAmbient(shade));
            else
                fragColor.rgb = mix(fragColor.rgb, skyColor.rgb * skyAmbient(shade), fogFactor);
#else
            fragColor.rgb = mix(fragColor.rgb, skyColor.rgb, fogFactor);
#endif
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
