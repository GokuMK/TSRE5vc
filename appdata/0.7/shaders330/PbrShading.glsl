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
// Bit per map present: 1 metallic-roughness, 2 normal, 4 occlusion, 8 emissive.
uniform int pbrTextures;
// Bit per map (RenderItem::Pbr::Map order) read with the second texture coordinates.
uniform int pbrTexCoords;
uniform sampler2D pbrMetallicRoughnessMap;
uniform sampler2D pbrNormalMap;
uniform sampler2D pbrOcclusionMap;
uniform sampler2D pbrEmissiveMap;
uniform samplerCube environmentMap;
// Mipmap levels of the environment map; 0 when none is bound.
uniform float environmentMapLevels;
uniform vec3 cameraPosition;

const float PbrPi = 3.14159265;

vec2 pbrUv(int map) {
    return (pbrTexCoords & (1 << map)) != 0 ? vTextureCoord1 : vTextureCoord;
}

vec3 toLinear(vec3 c) {
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

vec3 toDisplay(vec3 c) {
    return pow(clamp(c, vec3(0.0), vec3(1.0)), vec3(1.0 / 2.2));
}

float luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// The surroundings along a direction, blurred by mipmap level, in linear
// colour. Without a map: the sky above and dark ground below.
vec3 environmentRadiance(vec3 direction, float level) {
    if (environmentMapLevels > 0.0)
        return toLinear(textureLod(environmentMap, direction,
                                   clamp(level, 0.0, environmentMapLevels - 1.0)).rgb);
    float up = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    return mix(vec3(0.05), toLinear(skyColor.rgb), up);
}

// Split-sum environment BRDF as an analytic fit (Karis, mobile), in place
// of a lookup texture.
vec3 environmentBrdf(vec3 f0, float roughness, float nDotV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
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

    vec3 n = normalize(vWorldNormal);
    if (!gl_FrontFacing)
        n = -n;
    if ((pbrTextures & 2) != 0) {
        vec3 t = vWorldTangent.xyz - n * dot(n, vWorldTangent.xyz);
        if (dot(t, t) > 1e-10) {
            t = normalize(t);
            vec3 b = cross(n, t) * (vWorldTangent.w < 0.0 ? -1.0 : 1.0);
            vec3 m = texture(pbrNormalMap, pbrUv(2)).xyz * 2.0 - 1.0;
            m.xy *= pbrNormalScale;
            n = normalize(mat3(t, b, n) * m);
        }
    }
    vec3 v = normalize(cameraPosition - vWorldPosition);
    float nDotV = max(dot(n, v), 1e-4);
    vec3 f0 = mix(vec3(0.04), base.rgb, metallic);
    vec3 diffuseAlbedo = base.rgb * (1.0 - metallic);

    // The sun. Its irradiance matches the legacy diffuse light, so a white
    // matte surface facing it is as bright as in the legacy shading.
    vec3 color = vec3(0.0);
    vec3 l = normalize(lightDirection);
    float nDotL = max(dot(n, l), 0.0);
    if (nDotL > 0.0) {
        vec3 h = normalize(l + v);
        float nDotH = max(dot(n, h), 0.0);
        float vDotH = max(dot(v, h), 0.0);
        float a = roughness * roughness;
        float a2 = a * a;
        float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
        float distribution = a2 / (PbrPi * d * d);
        float k = a * 0.5;
        float visibilityTerm = 0.25 / ((nDotL * (1.0 - k) + k) * (nDotV * (1.0 - k) + k));
        vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);
        vec3 specular = distribution * visibilityTerm * fresnel;
        vec3 diffuse = (1.0 - fresnel) * diffuseAlbedo / PbrPi;
        float shadow = clamp(shadowedVisibility(1.0), 0.0, 1.0);
        color += (diffuse + specular) * PbrPi * toLinear(diffuseColor.rgb) * nDotL * shadow;
    }

    // Image-based light. Reflections take the surroundings at full
    // strength, sharper for smoother surfaces; diffuse light uses the most
    // blurred levels, scaled so open sky gives the legacy ambient light.
    float occlusion = 1.0;
    if ((pbrTextures & 4) != 0)
        occlusion = 1.0 + pbrOcclusionStrength * (texture(pbrOcclusionMap, pbrUv(3)).r - 1.0);
    float blurLevel = max(environmentMapLevels - 2.0, 0.0);
    vec3 irradiance = environmentRadiance(n, blurLevel);
    float ambientScale = luminance(toLinear(ambientColor.rgb))
            / max(luminance(toLinear(skyColor.rgb)), 0.05);
    vec3 reflected = environmentRadiance(reflect(-v, n),
                                         roughness * max(environmentMapLevels - 1.0, 0.0));
    color += (diffuseAlbedo * irradiance * ambientScale
              + reflected * environmentBrdf(f0, roughness, nDotV)) * occlusion;
    color *= colorBrightness;

    // Emission adds its own colour only; it does not light other surfaces.
    vec3 emissive = pbrEmissive;
    if ((pbrTextures & 8) != 0)
        emissive *= toLinear(texture(pbrEmissiveMap, pbrUv(4)).rgb);
    color += emissive;

    return vec4(mix(toDisplay(color), skyColor.rgb, fogFactor), alpha);
}
