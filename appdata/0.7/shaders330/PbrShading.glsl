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
// Bit per map present: 1 metallic-roughness, 2 normal, 4 occlusion, 8 emissive,
// 16 clearcoat, 32 clearcoat roughness, 64 clearcoat normal, 128 specular,
// 256 specular colour, 512 transmission, 1024 thickness.
uniform int pbrTextures;
// Bit per map (RenderItem::Pbr::Map order) read with the second texture coordinates.
uniform int pbrTexCoords;
// KHR_texture_transform: bit per transformed map, and two rows per map.
uniform int pbrUvTransforms;
uniform vec3 pbrUvTransform[24];
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

vec4 pbrShade() {
    vec4 base = textureEnabled != 0.0 ? texture(uSampler, pbrUv(0)) : shapeColor;
    base.rgb = toLinear(base.rgb);
    base *= pbrBaseColor * vColor;
    if (pbrAlphaCutoff >= 0.0 && base.a < pbrAlphaCutoff)
        discard;
    float alpha = pbrBlend != 0 ? base.a : 1.0;
    if (pbrUnlit != 0) {
        vec3 unlit = toDisplay(base.rgb * colorBrightness);
        return vec4(mix(unlit, skyColor.rgb, fogFactor), alpha);
    }

    float metallic = pbrMetallicRoughness.x;
    float roughness = pbrMetallicRoughness.y;
    if ((pbrTextures & 1) != 0) {
        vec4 metallicRoughness = texture(pbrMetallicRoughnessMap, pbrUv(1));
        roughness *= metallicRoughness.g;
        metallic *= metallicRoughness.b;
    }
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
    float transmission = pbrTransmission.x;
    if ((pbrTextures & 512) != 0)
        transmission *= texture(pbrTransmissionMap, pbrUv(10)).r;
    // Light passing through is not scattered diffusely.
    if (transmission > 0.0)
        diffuseAlbedo *= 1.0 - transmission;

    // The sun. Its irradiance matches the legacy diffuse light, so a white
    // matte surface facing it is as bright as in the legacy shading.
    // Clearcoat: a smooth dielectric layer (F0 0.04) over the base, with
    // its own roughness and normal. The base receives what it transmits.
    float coat = pbrClearcoat.x;
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
    float shadow = clamp(shadowedVisibility(1.0), 0.0, 1.0);
    vec3 sun = PbrPi * toLinear(diffuseColor.rgb) * shadow;
    if (nDotL > 0.0) {
        vec3 fresnel;
        vec3 specular = ggxSpecular(n, v, l, roughness, f0, f90, fresnel);
        vec3 diffuse = (1.0 - fresnel) * diffuseAlbedo / PbrPi;
        color += (diffuse + specular) * sun * nDotL;
    }
#ifdef TSRE_RHI
    // Lamps and glowing surfaces, shaded as the sun is.
    vec3 coatLocal = vec3(0.0);
    ivec2 lightCell = localLightCell(vWorldPosition);
    for (int i = 0; i < lightCell.y; ++i) {
        vec3 lightDir, radiance;
        if (!localLight(lightCell.x + i, vWorldPosition, lightDir, radiance))
            continue;
        float lightNDotL = max(dot(n, lightDir), 0.0);
        if (lightNDotL > 0.0) {
            vec3 fresnel;
            vec3 specular = ggxSpecular(n, v, lightDir, roughness, f0, f90, fresnel);
            color += ((1.0 - fresnel) * diffuseAlbedo / PbrPi + specular) * radiance * lightNDotL;
        }
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
    // scaled so open sky gives the legacy ambient light.
    float occlusion = 1.0;
    if ((pbrTextures & 4) != 0)
        occlusion = 1.0 + pbrOcclusionStrength * (texture(pbrOcclusionMap, pbrUv(3)).r - 1.0);
    vec3 irradiance = environmentRadiance(n, 1.0);
    float ambientScale = luminance(toLinear(ambientColor.rgb))
            / max(luminance(toLinear(skyColor.rgb)), 0.05);
    vec3 reflected = environmentRadiance(reflect(-v, n), roughness);
    vec3 environment = diffuseAlbedo * irradiance * ambientScale
            + reflected * environmentBrdf(f0, f90, roughness, nDotV);
    if (coat > 0.0) {
        vec3 coatReflected = environmentRadiance(reflect(-v, coatNormal), coatRoughness);
        environment = environment * (1.0 - coatFresnel)
                + coat * coatReflected * environmentBrdf(vec3(0.04), coatRoughness, coatNDotV);
    }
    color += environment * occlusion;
#ifdef TSRE_RHI
    vec3 ambientLight = environment * occlusion * colorBrightness;
#endif
    color *= colorBrightness;

    // Transmission: the light from behind replaces the diffuse part of
    // non-metal surfaces, tinted by the base colour and, through a volume,
    // absorbed along the refracted path; the clearcoat lies over it.
    if (transmission > 0.0)
        color += transmission * (1.0 - coatFresnel) * (1.0 - metallic)
                * (vec3(1.0) - environmentBrdf(f0, f90, roughness, nDotV))
                * base.rgb * transmittedLight(n, v, roughness);

    // Emission adds its own colour only; it does not light other surfaces.
    vec3 emissive = pbrEmissive;
    if ((pbrTextures & 8) != 0)
        emissive *= toLinear(texture(pbrEmissiveMap, pbrUv(4)).rgb);
    color += emissive;

#ifdef TSRE_RHI
    // The display-space share of the environment light, after fog.
    pbrAmbientOut = vec4((toDisplay(color) - toDisplay(max(color - ambientLight, vec3(0.0))))
                         * (1.0 - fogFactor), alpha);
#endif
    return vec4(mix(toDisplay(color), skyColor.rgb, fogFactor), alpha);
}
