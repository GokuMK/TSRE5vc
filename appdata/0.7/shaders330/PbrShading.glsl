// Metallic-roughness shading of glTF materials, for the TSRE_PBR variant:
// the sun (GGX specular, Lambert diffuse) with the scene's shadows, and
// image-based light from the environment cube map. Lighting is computed in
// linear colour and converted back to display colour at the end; there is
// no tone mapping, so highlights clip. Included by StandardFog.fs after the
// shadow sampling.
in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec4 vWorldTangent;
in vec2 vTextureCoord1;
in vec4 vColor;

uniform vec4 pbrBaseColor;
uniform vec2 pbrMetallicRoughness;
uniform vec3 pbrEmissive;
uniform float pbrNormalScale;
uniform float pbrOcclusionStrength;
// Alpha below it is discarded; negative keeps every pixel.
uniform float pbrAlphaCutoff;
uniform int pbrBlend;
uniform int pbrUnlit;
#ifdef TSRE_RHI
// Undoes auto exposure (task 29) for unlit materials and emissive colour,
// designed for the screen (linear; 1 without it). Their glow is not undone:
// it follows the lamps.
uniform float emissiveScale;
#endif
// Bit per map present: 1 metallic-roughness, 2 normal, 4 occlusion, 8 emissive,
// 16 clearcoat, 32 clearcoat roughness, 64 clearcoat normal, 128 specular,
// 256 specular colour, 512 transmission, 1024 thickness, 2048 diffuse
// transmission, 4096 diffuse transmission colour. MSTS PBR materials (task
// shapes 13): 8192 the clearcoat also times the metallic-roughness map's
// alpha, 16384 glass (blended: alpha covers the diffuse light only).
uniform int pbrTextures;
// Bit per map (RenderItem::Pbr::Map order) read with the second texture coordinates.
uniform int pbrTexCoords;
// KHR_texture_transform: bit per transformed map, and two rows per map.
uniform int pbrUvTransforms;
uniform vec3 pbrUvTransform[28];
// Clearcoat strength, roughness and normal scale; strength 0 has no layer.
uniform vec3 pbrClearcoat;
// KHR_materials_specular colour (rgb) and strength (a), and KHR_materials_ior.
uniform vec4 pbrSpecular;
uniform float pbrIor;
// KHR_materials_transmission and KHR_materials_volume: transmission,
// thickness, attenuation distance (0 never absorbs), and the mipmap levels
// of the frame copy behind (0: the environment stands in).
uniform vec4 pbrTransmission;
uniform vec3 pbrAttenuationColor;
uniform sampler2D pbrSceneColor;
uniform sampler2D pbrTransmissionMap;
uniform sampler2D pbrThicknessMap;
// KHR_materials_diffuse_transmission: colour (rgb) and amount (a); the
// amount map's alpha (linear) and the colour map's colour (sRGB).
uniform vec4 pbrDiffuseTransmission;
uniform sampler2D pbrDiffuseTransmissionMap;
uniform sampler2D pbrDiffuseTransmissionColorMap;
uniform mat4 uPMatrix;
uniform sampler2D pbrMetallicRoughnessMap;
uniform sampler2D pbrNormalMap;
uniform sampler2D pbrOcclusionMap;
uniform sampler2D pbrEmissiveMap;
uniform sampler2D pbrClearcoatMap;
uniform sampler2D pbrClearcoatRoughnessMap;
uniform sampler2D pbrClearcoatNormalMap;
uniform sampler2D pbrSpecularMap;
uniform sampler2D pbrSpecularColorMap;
#ifdef TSRE_RHI
// Mean radius of the emitters standing in for this surface (0: none), and
// the main view's pixels per unit at distance 1 (glow splats, task 24).
uniform float pbrGlowRadius;
uniform float glowFocal;
// The daylight scale of this surface's glow, 0 with local lights off: the
// lamps' (task 21), or the signal lights' for signals (task 26).
uniform float pbrGlowScale;
#ifdef TSRE_RHI
// The glow's scale where the sky is enclosed (task 27).
uniform float pbrGlowEnclosed;
#endif
#endif
#include "EnvironmentLighting.glsl"

vec2 pbrUv(int map) {
    vec2 uv = (pbrTexCoords & (1 << map)) != 0 ? vTextureCoord1 : vTextureCoord;
    if ((pbrUvTransforms & (1 << map)) != 0) {
        vec3 p = vec3(uv, 1.0);
        uv = vec2(dot(pbrUvTransform[map * 2], p), dot(pbrUvTransform[map * 2 + 1], p));
    }
    return uv;
}

// A normal from a tangent-space map around n.
vec3 mappedNormal(vec3 n, sampler2D map, int uvMap, float scale) {
    vec3 t = vWorldTangent.xyz - n * dot(n, vWorldTangent.xyz);
    if (dot(t, t) <= 1e-10)
        return n;
    t = normalize(t);
    vec3 b = cross(n, t) * (vWorldTangent.w < 0.0 ? -1.0 : 1.0);
    vec3 m = texture(map, pbrUv(uvMap)).xyz * 2.0 - 1.0;
    m.xy *= scale;
    return normalize(mat3(t, b, n) * m);
}

// Light reaching the eye through a transmissive surface: the frame drawn
// before the transmission pass, read where the refracted ray leaves the
// volume (at the surface itself when thin-walled) and blurred by roughness,
// or the environment along the refracted ray without a frame copy.
vec3 transmittedLight(vec3 n, vec3 v, float roughness) {
    float thickness = pbrTransmission.y;
    if ((pbrTextures & 1024) != 0)
        thickness *= texture(pbrThicknessMap, pbrUv(11)).g;
    // Thickness is given in the model's units.
    mat4 model = vModelView * uMSMatrix;
    float scale = length(model[0].xyz);
    vec3 refracted = normalize(refract(-v, n, 1.0 / pbrIor));
    float path = thickness * scale;
    vec3 exitPoint = vWorldPosition + refracted * path;
    // A higher index of refraction spreads the light more (as the Khronos
    // sample viewer does).
    float spread = roughness * clamp(pbrIor * 2.0 - 2.0, 0.0, 1.0);
    vec3 behind;
    if (pbrTransmission.w > 0.0) {
        vec4 clip = uPMatrix * vec4(exitPoint, 1.0);
        // Off-screen lookups read the edge pixels of the copy.
        vec2 uv = clamp(clip.xy / max(clip.w, 1e-4) * 0.5 + 0.5, 0.0, 1.0);
        behind = toLinear(textureLod(pbrSceneColor, uv, spread * (pbrTransmission.w - 1.0)).rgb);
    } else {
        behind = environmentRadiance(refracted, spread) * colorBrightness;
    }
    if (pbrTransmission.z > 0.0 && path > 0.0)
        behind *= pow(max(pbrAttenuationColor, vec3(1e-4)), vec3(path / pbrTransmission.z));
    return behind;
}

// Diffuse light passing through a thin surface from behind (a light at l on
// the far side): Lambert on that side, less what the surface reflects, with
// the Fresnel of the light mirrored to the viewer's side (as the Khronos
// sample viewer does).
vec3 transmittedDiffuse(vec3 n, vec3 v, vec3 l, vec3 albedo, vec3 f0, vec3 f90) {
    float backNDotL = max(-dot(n, l), 0.0);
    if (backNDotL <= 0.0)
        return vec3(0.0);
    vec3 h = normalize(l - 2.0 * n * dot(n, l) + v);
    vec3 fresnel = f0 + (f90 - f0) * pow(1.0 - clamp(dot(v, h), 0.0, 1.0), 5.0);
    return (1.0 - fresnel) * albedo / PbrPi * backNDotL;
}

// How much of its surroundings a reflection keeps in a crease: specular
// occlusion (Lagarde and de Rousiers 2014, as Filament has it), the
// diffuse occlusion on rough surfaces, less of it on smooth ones seen
// head-on and more at grazing angles; and none of what a reflection below
// the surface itself would see (horizon occlusion: a normal map's slopes
// turned away from the view reflect the inside of the body).
float reflectionOcclusion(float occlusion, float nDotV, float roughness, vec3 r, vec3 surface) {
    float specular = clamp(pow(nDotV + occlusion, exp2(-16.0 * roughness - 1.0)) - 1.0 + occlusion, 0.0, 1.0);
    float horizon = min(1.0 + dot(r, surface), 1.0);
    return specular * horizon * horizon;
}

vec4 pbrShade() {
    vec4 base = textureEnabled != 0.0 ? texture(uSampler, pbrUv(0)) : shapeColor;
    base.rgb = toLinear(base.rgb);
    base *= pbrBaseColor * vColor;
    if (pbrAlphaCutoff >= 0.0 && base.a < pbrAlphaCutoff)
        discard;
    float alpha = pbrBlend != 0 ? base.a : 1.0;
    if (pbrUnlit != 0) {
        vec3 unlitLight = base.rgb * colorBrightness;
#ifdef TSRE_RHI
        unlitLight *= emissiveScale;
#endif
        vec3 unlit = toDisplay(unlitLight);
#ifdef TSRE_RHI
        if (hazeOn())
            return vec4(applyHaze(unlit, vLightPosition, 1.0, 1.0), alpha);
#endif
        return vec4(mix(unlit, skyColor.rgb, fogFactor), alpha);
    }

    float metallic = pbrMetallicRoughness.x;
    float roughness = pbrMetallicRoughness.y;
    float coatMask = 1.0;
    if ((pbrTextures & 1) != 0) {
        vec4 metallicRoughness = texture(pbrMetallicRoughnessMap, pbrUv(1));
        roughness *= metallicRoughness.g;
        metallic *= metallicRoughness.b;
        if ((pbrTextures & 8192) != 0)
            coatMask = metallicRoughness.a;
    }
    // Glass: its paint and dirt cover as alpha says; its reflection is added
    // whole, and the coverage made up at the end.
    bool glass = (pbrTextures & 16384) != 0 && pbrBlend != 0;
    metallic = clamp(metallic, 0.0, 1.0);
    roughness = clamp(roughness, 0.03, 1.0);

    vec3 geometricNormal = normalize(vWorldNormal);
    if (!gl_FrontFacing)
        geometricNormal = -geometricNormal;
    vec3 n = geometricNormal;
    if ((pbrTextures & 2) != 0)
        n = mappedNormal(n, pbrNormalMap, 2, pbrNormalScale);
    vec3 v = normalize(cameraPosition - vWorldPosition);
    float nDotV = max(dot(n, v), 1e-4);
    // Dielectric reflectance from the index of refraction, tinted and
    // weighted by KHR_materials_specular; metals reflect their base colour.
    float specularWeight = pbrSpecular.a;
    if ((pbrTextures & 128) != 0)
        specularWeight *= texture(pbrSpecularMap, pbrUv(8)).a;
    vec3 specularTint = pbrSpecular.rgb;
    if ((pbrTextures & 256) != 0)
        specularTint *= toLinear(texture(pbrSpecularColorMap, pbrUv(9)).rgb);
    float iorF0 = (pbrIor - 1.0) / (pbrIor + 1.0);
    vec3 dielectricF0 = min(iorF0 * iorF0 * specularTint, vec3(1.0)) * specularWeight;
    vec3 f0 = mix(dielectricF0, base.rgb, metallic);
    vec3 f90 = vec3(mix(specularWeight, 1.0, metallic));
    vec3 diffuseAlbedo = base.rgb * (1.0 - metallic);
    if (glass)
        diffuseAlbedo *= alpha;
    // Diffuse transmission (KHR_materials_diffuse_transmission): thin
    // surfaces such as leaves pass that share of the diffuse light through,
    // in their own colour, lighting them from behind.
    float diffuseTransmission = pbrDiffuseTransmission.a;
    if ((pbrTextures & 2048) != 0)
        diffuseTransmission *= texture(pbrDiffuseTransmissionMap, pbrUv(12)).a;
    vec3 transmittedAlbedo = vec3(0.0);
    if (diffuseTransmission > 0.0) {
        vec3 tint = pbrDiffuseTransmission.rgb;
        if ((pbrTextures & 4096) != 0)
            tint *= toLinear(texture(pbrDiffuseTransmissionColorMap, pbrUv(13)).rgb);
        transmittedAlbedo = tint * diffuseTransmission * (1.0 - metallic);
        diffuseAlbedo *= 1.0 - diffuseTransmission;
    }
    float transmission = pbrTransmission.x;
    if ((pbrTextures & 512) != 0)
        transmission *= texture(pbrTransmissionMap, pbrUv(10)).r;
    // Light passing through is not scattered diffusely.
    if (transmission > 0.0) {
        diffuseAlbedo *= 1.0 - transmission;
        transmittedAlbedo *= 1.0 - transmission;
    }

    // The sun. Its irradiance matches the legacy diffuse light, so a white
    // matte surface facing it is as bright as in the legacy shading.
    // Clearcoat: a smooth dielectric layer (F0 0.04) over the base, with
    // its own roughness and normal. The base receives what it transmits.
    float coat = pbrClearcoat.x * coatMask;
    float coatRoughness = pbrClearcoat.y;
    vec3 coatNormal = geometricNormal;
    if (coat > 0.0) {
        if ((pbrTextures & 16) != 0)
            coat *= texture(pbrClearcoatMap, pbrUv(5)).r;
        if ((pbrTextures & 32) != 0)
            coatRoughness *= texture(pbrClearcoatRoughnessMap, pbrUv(6)).g;
        if ((pbrTextures & 64) != 0)
            coatNormal = mappedNormal(coatNormal, pbrClearcoatNormalMap, 7, pbrClearcoat.z);
    }
    coatRoughness = clamp(coatRoughness, 0.03, 1.0);
    float coatNDotV = max(dot(coatNormal, v), 1e-4);
    float coatFresnel = coat * (0.04 + 0.96 * pow(1.0 - coatNDotV, 5.0));

    vec3 color = vec3(0.0);
    vec3 l = normalize(lightDirection);
    float nDotL = max(dot(n, l), 0.0);
    // Shadowed as the rest of the content: the legacy visibility is N.L less
    // 1.6 times the shadowed share (ShadowSampling.glsl), so a partly shadowed
    // surface a low sun grazes is dark through. The sun's light here takes
    // N.L itself, so the shadow is that visibility over N.L (the geometric
    // normal's, from either side for thin foliage lit through). From 1 less
    // the share, rails' shadows on PBR ballast at 8:30 were faint patches
    // beside the legacy track's dark ones (the user, 2026-10-27).
    float geometricNDotL = max(abs(dot(geometricNormal, l)), 1e-3);
    float shadow = clamp(shadowedVisibility(geometricNDotL) / geometricNDotL, 0.0, 1.0);
#ifdef TSRE_RHI
    shadow *= cloudShadow(vLightPosition);
    float shade;
    float enclosed = skyEnclosed(vLightPosition, n, shade);
    shadow *= enclosureSun(vLightPosition, shade, l);
#endif
    vec3 sun = PbrPi * toLinear(diffuseColor.rgb) * shadow;
    if (nDotL > 0.0) {
        vec3 fresnel;
        vec3 specular = ggxSpecular(n, v, l, roughness, f0, f90, fresnel);
        vec3 diffuse = (1.0 - fresnel) * diffuseAlbedo / PbrPi;
        color += (diffuse + specular) * sun * nDotL;
    }
    if (diffuseTransmission > 0.0)
        color += transmittedDiffuse(n, v, l, transmittedAlbedo, f0, f90) * sun;
#ifdef TSRE_RHI
    float lamps = lampScale(shade);
    // Lamps and glowing surfaces, shaded as the sun is.
    vec3 coatLocal = vec3(0.0);
    ivec2 lightCell = localLightCell(vWorldPosition);
    for (int i = 0; i < lightCell.y; ++i) {
        vec3 lightDir, radiance;
        if (!localLight(lightCell.x + i, vWorldPosition, lightDir, radiance))
            continue;
        radiance *= lamps;
        float lightNDotL = max(dot(n, lightDir), 0.0);
        if (lightNDotL > 0.0) {
            vec3 fresnel;
            vec3 specular = ggxSpecular(n, v, lightDir, roughness, f0, f90, fresnel);
            color += ((1.0 - fresnel) * diffuseAlbedo / PbrPi + specular) * radiance * lightNDotL;
        }
        if (diffuseTransmission > 0.0)
            color += transmittedDiffuse(n, v, lightDir, transmittedAlbedo, f0, f90) * radiance;
        float lightCoatNDotL = max(dot(coatNormal, lightDir), 0.0);
        if (coat > 0.0 && lightCoatNDotL > 0.0) {
            vec3 fresnel;
            coatLocal += coat * ggxSpecular(coatNormal, v, lightDir, coatRoughness, vec3(0.04), fresnel)
                    * radiance * lightCoatNDotL;
        }
    }
#endif
    color *= 1.0 - coatFresnel;
#ifdef TSRE_RHI
    color += coatLocal;
#endif
    float coatNDotL = max(dot(coatNormal, l), 0.0);
    if (coat > 0.0 && coatNDotL > 0.0) {
        vec3 fresnel;
        color += coat * ggxSpecular(coatNormal, v, l, coatRoughness, vec3(0.04), fresnel)
                * sun * coatNDotL;
    }

    // Image-based light from the prefiltered cube: level i holds reflections
    // for roughness i / (levels - 1); diffuse light uses the roughest level,
    // scaled so open sky gives the legacy ambient light: the cube's light
    // from straight up (the sky colour without a cube) stands for it. The
    // cube's own level cancels, so its dark zenith at dawn and dusk (the sky
    // colour, the fog's, glows there: PBR came out several times darker than
    // the rest) and a cube some frames behind a time change (a face a frame:
    // a jump lit PBR up, then darkened it) light as the rest does. Both are
    // in the scene's scale: with auto exposure a thirtieth at night, so the
    // sky's light from above came under the division's former floor of 1e-4
    // and the night's ambient fell to a tenth (Prudnik 21:00, 2026-10-27).
    float occlusion = 1.0;
    if ((pbrTextures & 4) != 0)
        occlusion = 1.0 + pbrOcclusionStrength * (texture(pbrOcclusionMap, pbrUv(3)).r - 1.0);
    vec3 irradiance = environmentRadiance(n, 1.0);
    float ambientScale = luminance(toLinear(ambientColor.rgb))
            / max(luminance(environmentRadiance(vec3(0.0, 1.0, 0.0), 1.0)), 1e-12);
    vec3 r = reflect(-v, n);
    vec3 reflected = environmentRadiance(r, roughness);
    vec3 diffuseEnvironment = diffuseAlbedo * irradiance * ambientScale;
    // The sky and surroundings behind a thin surface shine through it.
    if (diffuseTransmission > 0.0)
        diffuseEnvironment += transmittedAlbedo * environmentRadiance(-n, 1.0) * ambientScale;
    // The occlusion darkens the diffuse light; reflections their own share
    // of it (reflectionOcclusion).
    vec3 environment = diffuseEnvironment * occlusion
            + reflected * environmentBrdf(f0, f90, roughness, nDotV)
            * reflectionOcclusion(occlusion, nDotV, roughness, r, geometricNormal);
    if (coat > 0.0) {
        vec3 coatR = reflect(-v, coatNormal);
        vec3 coatReflected = environmentRadiance(coatR, coatRoughness);
        environment = environment * (1.0 - coatFresnel)
                + coat * coatReflected * environmentBrdf(vec3(0.04), coatRoughness, coatNDotV)
                * reflectionOcclusion(occlusion, coatNDotV, coatRoughness, coatR, geometricNormal);
    }
#ifdef TSRE_RHI
    environment *= skyAmbient(shade);
#endif
    color += environment;
#ifdef TSRE_RHI
    vec3 ambientLight = environment * colorBrightness;
#endif
    color *= colorBrightness;

    // Transmission: the light from behind replaces the diffuse part of
    // non-metal surfaces, tinted by the base colour and, through a volume,
    // absorbed along the refracted path; the clearcoat lies over it.
    if (transmission > 0.0)
        color += transmission * (1.0 - coatFresnel) * (1.0 - metallic)
                * (vec3(1.0) - environmentBrdf(f0, f90, roughness, nDotV))
                * base.rgb * transmittedLight(n, v, roughness);

    // Emission adds its own colour; on the QRhi renderer emitters placed on
    // it also light other surfaces (task 21) and its glow feeds bloom.
    vec3 emissive = pbrEmissive;
    if ((pbrTextures & 8) != 0)
        emissive *= toLinear(texture(pbrEmissiveMap, pbrUv(4)).rgb);
#ifdef TSRE_RHI
    color += emissive * emissiveScale;
#else
    color += emissive;
#endif
    // Glass: what it reflects hides what lies behind as much as it reflects
    // (Fresnel), and a highlight brighter than that as much as it shines.
    if (glass) {
        float reflectance = clamp(luminance(environmentBrdf(f0, f90, roughness, nDotV)) + coatFresnel,
                                  0.0, 1.0);
        float covered = clamp(max(alpha + (1.0 - alpha) * reflectance, luminance(color)), alpha, 1.0);
        color /= max(covered, 1e-3);
        alpha = covered;
    }

#ifdef TSRE_RHI
    // Shrinking below 32 pixels, the surface hands its glow to its emitters'
    // splats, drawn at their exact position (RhiImage.cpp): its own pixels
    // come and go as it moves across the pixel grid.
    float glowShare = 1.0;
    if (pbrGlowRadius > 0.0) {
        float scale = length(mat3(vModelView) * mat3(uMSMatrix) * vec3(1.0, 0.0, 0.0));
        float pixels = pbrGlowRadius * scale * glowFocal
                / max(distance(cameraPosition, vWorldPosition), 0.001);
        glowShare = smoothstep(16.0, 32.0, pixels);
    }
    vec3 notFog = hazeOn() ? hazeThrough(vLightPosition) : vec3(1.0 - fogFactor);
    pbrGlowOut = vec4(emissive * notFog * glowShare * mix(pbrGlowScale, pbrGlowEnclosed, enclosed),
                      alpha);
    // The display-space share of the environment light, after fog.
    pbrAmbientOut = vec4((toDisplay(color) - toDisplay(max(color - ambientLight, vec3(0.0))))
                         * notFog, alpha);
    // Under cover the haze is lit as the ambient is.
    if (hazeOn())
        return vec4(applyHaze(toDisplay(color), vLightPosition, 1.0, skyAmbient(shade)), alpha);
    return vec4(mix(toDisplay(color), skyColor.rgb * skyAmbient(shade), fogFactor), alpha);
#else
    return vec4(mix(toDisplay(color), skyColor.rgb, fogFactor), alpha);
#endif
}
