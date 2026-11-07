// Local lights (task 21): lamps and emissive surfaces binned by the QRhi
// renderer into a world-space grid around the camera (LightGrid): 512 x 512
// cells of 16 m, localLightLayers.z layers (1: columns of every height).
// Included by the lit standard shaders under TSRE_RHI; localLightLayers.y is
// the light count, 0 when there are none.
uniform vec4 localLightGrid;   // grid origin, horizontal cell size
uniform vec4 localLightLayers; // vertical cell size, light count, layers
// Four texels per light, one light per row: position and range; colour
// times intensity and squared radius; spot direction and spot flag;
// cosine of the outer angle and 1 / (cos inner - cos outer).
uniform sampler2D localLightData;
// Each cell's first index and count packed into a float's bits
// (LightGrid::packCell), cells x + z * 512 + y * 512 * 512, 4096 a row.
uniform sampler2D localLightCells;
// Light indices, 4096 per row.
uniform sampler2D localLightIndices;
// The lamps' static shadow maps (task 46): a layer a lamp, 1 - its depth
// seen from it (as the depth buffer holds it), drawn with the matrix in its
// data's texels 4 to 7; fine maps and coarse ones.
uniform sampler2DArray localLightShadows;
uniform sampler2DArray localLightShadowsCoarse;

const float LocalLightPi = 3.14159265;
// How far a lamp's shadow map reaches at most (RhiRenderer::LampShadowFar,
// the same): a lamp reaching farther lights on past it unshadowed.
const float LocalLightShadowFar = 100.0;

// First index and count of the lights reaching a position.
ivec2 localLightCell(vec3 position) {
    if (localLightLayers.y <= 0.0)
        return ivec2(0);
    vec3 size = vec3(localLightGrid.w, localLightLayers.x, localLightGrid.w);
    ivec3 cell = ivec3(floor((position - localLightGrid.xyz) / size));
    if (any(lessThan(cell, ivec3(0))) || any(greaterThanEqual(cell, ivec3(512, int(localLightLayers.z), 512))))
        return ivec2(0);
    int index = cell.x + cell.z * 512 + cell.y * 262144;
    uint packed = floatBitsToUint(texelFetch(localLightCells, ivec2(index % 4096, index / 4096), 0).r);
    return ivec2(int((packed >> 7u) & 0x7FFFFFu), int(packed & 0x7Fu));
}

// 1 - depth at a texel of a lamp's map, fine or coarse.
float localLightShadowTexel(bool coarse, ivec3 texel) {
    return coarse ? texelFetch(localLightShadowsCoarse, texel, 0).r : texelFetch(localLightShadows, texel, 0).r;
}

// The weight of a texel this far (texels) from the point: a quadratic
// B-spline (weights adding up to 1, smooth as the point moves).
float localLightShadowWeight(float d) {
    d = abs(d);
    return d < 0.5 ? 0.75 - d * d : (d < 1.5 ? 0.5 * (1.5 - d) * (1.5 - d) : 0.0);
}

// How much of a lamp's light passes what stands in its way, by its shadow
// map: 4 x 4 texels around the point compared, blended by the B-spline: the
// edge softened over about two texels without the texels' steps, thin
// shadows kept dark (2 x 2 showed every step; a box of 3 texels washed a
// pole's shadow out; the same GPU time at the yard). Fading to none toward
// the map's edge (there the lamp's light is weak).
float localLightShadow(int light, float layer, bool coarse, vec3 position) {
    mat4 m = mat4(texelFetch(localLightData, ivec2(4, light), 0), texelFetch(localLightData, ivec2(5, light), 0),
                  texelFetch(localLightData, ivec2(6, light), 0), texelFetch(localLightData, ivec2(7, light), 0));
    vec4 clip = m * vec4(position, 1.0);
    if (clip.w <= 0.0)
        return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    float edge = max(abs(ndc.x), abs(ndc.y));
    if (edge >= 1.0 || ndc.z >= 1.0)
        return 1.0;
    vec3 s = ndc * 0.5 + 0.5;
    ivec2 size = coarse ? textureSize(localLightShadowsCoarse, 0).xy : textureSize(localLightShadows, 0).xy;
    vec2 texel = s.xy * vec2(size) - 0.5;
    ivec2 base = ivec2(floor(texel)) - 1;
    int l = int(layer + 0.5);
    float lit = 0.0;
    for (int y = 0; y < 4; ++y) {
        float wy = localLightShadowWeight(float(base.y + y) - texel.y);
        for (int x = 0; x < 4; ++x) {
            float wx = localLightShadowWeight(float(base.x + x) - texel.x);
            float stored = localLightShadowTexel(coarse, ivec3(clamp(base + ivec2(x, y), ivec2(0), size - 1), l));
            lit += 1.0 - s.z >= stored ? wx * wy : 0.0;
        }
    }
    return mix(lit, 1.0, smoothstep(0.8, 1.0, edge));
}

// One light of a cell: the direction towards it and the light it brings,
// scaled as the sun's (pi times the irradiance). False when it does not
// reach the position.
bool localLight(int slot, vec3 position, out vec3 l, out vec3 radiance) {
    l = vec3(0.0);
    radiance = vec3(0.0);
    int light = int(texelFetch(localLightIndices, ivec2(slot % 4096, slot / 4096), 0).r);
    vec4 placement = texelFetch(localLightData, ivec2(0, light), 0);
    vec3 toLight = placement.xyz - position;
    float squared = dot(toLight, toLight);
    float range = placement.w;
    if (squared >= range * range)
        return false;
    vec4 colour = texelFetch(localLightData, ivec2(1, light), 0);
    l = toLight * inversesqrt(max(squared, 1e-8));
    // Inverse square, softened by the emitter's size, fading to nothing at
    // the range.
    float ratio = squared / (range * range);
    float window = clamp(1.0 - ratio * ratio, 0.0, 1.0);
    float attenuation = window * window / (squared + colour.w);
    vec4 spot = texelFetch(localLightData, ivec2(2, light), 0);
    if (spot.w > 0.5) {
        vec4 cone = texelFetch(localLightData, ivec2(3, light), 0);
        float inside = clamp((dot(-l, spot.xyz) - cone.x) * cone.y, 0.0, 1.0);
        attenuation *= inside * inside;
        // Its static shadow (task 46), the point moved toward the lamp
        // against acne.
        // Past 80 m (lamps reaching farther than their maps) the shadow
        // fades out by LocalLightShadowFar.
        if (cone.z > 0.5 && attenuation > 0.0) {
            float lampDistance = sqrt(squared);
            // Moved toward the lamp against acne, half as much again as
            // for 2 x 2 texels: the compares reach 1.5 texels farther.
            float shadow = localLightShadow(light, cone.z - 1.0, cone.w > 0.5,
                                            position + l * (0.075 + 0.006 * lampDistance));
            attenuation *= mix(shadow, 1.0, smoothstep(0.8 * LocalLightShadowFar, LocalLightShadowFar, lampDistance));
        }
    }
    radiance = LocalLightPi * colour.rgb * attenuation;
    return attenuation > 0.0;
}

// Lambert light of the local lights on a white surface, in the sun's units
// (irradiance). Without normals every light counts fully.
vec3 localLightsDiffuse(vec3 position, vec3 normal, float normalsEnabled) {
    vec3 sum = vec3(0.0);
    ivec2 cell = localLightCell(position);
    for (int i = 0; i < cell.y; ++i) {
        vec3 l, radiance;
        if (!localLight(cell.x + i, position, l, radiance))
            continue;
        sum += radiance / LocalLightPi * mix(1.0, max(dot(normal, l), 0.0), normalsEnabled);
    }
    return sum;
}
