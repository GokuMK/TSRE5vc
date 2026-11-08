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
// The moving trains' boxes the lamps take (task 46 A), an entry a row: a
// car's header (its bounding sphere, the count of its boxes in the
// thousands of the radius; its sideways axis and half width), then its
// boxes (three rows of a world-to-box matrix each).
uniform sampler2D localLightBoxes;

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

// How wide (texels) a lamp's shadow edge is softened: a box that wide
// around the point, sliding with it through its texel, (width + 1) squared
// texels compared and blended (1: the 2 x 2 blend, edges a texel wide).
const int LocalLightShadowWidth = 3;

// How much of a lamp's light passes what stands in its way, by its shadow
// map: compared over LocalLightShadowWidth texels, fading to none toward
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
    // The box's first texel centre and how far into it the box starts.
    vec2 texel = s.xy * vec2(size) - 0.5 - 0.5 * float(LocalLightShadowWidth - 1);
    ivec2 base = ivec2(floor(texel));
    vec2 f = texel - vec2(base);
    int l = int(layer + 0.5);
    float lit = 0.0;
    for (int y = 0; y <= LocalLightShadowWidth; ++y) {
        float wy = y == 0 ? 1.0 - f.y : (y == LocalLightShadowWidth ? f.y : 1.0);
        for (int x = 0; x <= LocalLightShadowWidth; ++x) {
            float wx = x == 0 ? 1.0 - f.x : (x == LocalLightShadowWidth ? f.x : 1.0);
            float stored = localLightShadowTexel(coarse, ivec3(clamp(base + ivec2(x, y), ivec2(0), size - 1), l));
            lit += 1.0 - s.z >= stored ? wx * wy : 0.0;
        }
    }
    lit /= float(LocalLightShadowWidth * LocalLightShadowWidth);
    return mix(lit, 1.0, smoothstep(0.8, 1.0, edge));
}

// How much of a lamp's light passes the moving trains near it (task 46
// A): each car a body box and two bogie boxes, the light fading with how
// far its way from the point to the lamp runs inside a box (soft at the
// edges). A way missing a car's sphere, or a point and a lamp beyond the
// same side of it, skips its boxes; a point inside a box (just outside
// included: the car's own surfaces, its cab) passes it.
const float LocalLightBoxSoftness = 0.25;
float localLightBoxShadow(int light, vec3 position, vec3 lamp) {
    vec4 list = texelFetch(localLightData, ivec2(8, light), 0);
    int first = int(list.x + 0.5);
    int count = int(list.y + 0.5);
    vec3 toLamp = lamp - position;
    float way = length(toLamp);
    float lit = 1.0;
    int b = 0;
    while (b < count) {
        vec4 car = texelFetch(localLightBoxes, ivec2(0, first + b), 0);
        float boxes = floor(car.w / 1000.0);
        float radius = car.w - 1000.0 * boxes;
        int inner = int(boxes + 0.5);
        vec3 nearest = position + toLamp * clamp(dot(car.xyz - position, toLamp) / max(way * way, 1e-8), 0.0, 1.0);
        vec3 off = nearest - car.xyz;
        bool reaches = dot(off, off) < radius * radius;
        if (reaches) {
            vec4 side = texelFetch(localLightBoxes, ivec2(1, first + b), 0);
            float atPoint = dot(side.xyz, position - car.xyz);
            float atLamp = dot(side.xyz, lamp - car.xyz);
            reaches = !((atPoint > side.w && atLamp > side.w) || (atPoint < -side.w && atLamp < -side.w));
        }
        if (reaches) {
            for (int k = 1; k <= inner; ++k) {
                vec4 r0 = texelFetch(localLightBoxes, ivec2(0, first + b + k), 0);
                vec4 r1 = texelFetch(localLightBoxes, ivec2(1, first + b + k), 0);
                vec4 r2 = texelFetch(localLightBoxes, ivec2(2, first + b + k), 0);
                vec3 a = vec3(dot(r0.xyz, position) + r0.w, dot(r1.xyz, position) + r1.w,
                              dot(r2.xyz, position) + r2.w);
                if (max(max(abs(a.x), abs(a.y)), abs(a.z)) < 1.02)
                    continue;
                vec3 c = vec3(dot(r0.xyz, lamp) + r0.w, dot(r1.xyz, lamp) + r1.w, dot(r2.xyz, lamp) + r2.w);
                vec3 d = c - a;
                d = mix(d, vec3(1e-6), lessThan(abs(d), vec3(1e-6)));
                vec3 t0 = (vec3(-1.0) - a) / d;
                vec3 t1 = (vec3(1.0) - a) / d;
                vec3 lo = min(t0, t1);
                vec3 hi = max(t0, t1);
                float enter = max(max(lo.x, lo.y), max(lo.z, 0.0));
                float leave = min(min(hi.x, hi.y), min(hi.z, 1.0));
                lit *= 1.0 - smoothstep(0.0, LocalLightBoxSoftness, max(leave - enter, 0.0) * way);
            }
            if (lit <= 0.0)
                break;
        }
        b += 1 + inner;
    }
    return lit;
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
    if (mod(spot.w, 2.0) > 0.5) {
        vec4 cone = texelFetch(localLightData, ivec2(3, light), 0);
        float inside = clamp((dot(-l, spot.xyz) - cone.x) * cone.y, 0.0, 1.0);
        attenuation *= inside * inside;
        // Its static shadow (task 46), the point moved toward the lamp
        // against acne.
        // Past 80 m (lamps reaching farther than their maps) the shadow
        // fades out by LocalLightShadowFar.
        if (cone.z > 0.5 && attenuation > 0.0) {
            float lampDistance = sqrt(squared);
            // Moved toward the lamp against acne, the more the wider the
            // edge (its compares reach farther from the point).
            float shadow = localLightShadow(light, cone.z - 1.0, cone.w > 0.5,
                                            position + l * (0.05 + 0.004 * lampDistance)
                                                    * (0.5 + 0.5 * float(LocalLightShadowWidth)));
            attenuation *= mix(shadow, 1.0, smoothstep(0.8 * LocalLightShadowFar, LocalLightShadowFar, lampDistance));
        }
    }
    // The moving trains' boxes (task 46 A), spots and points alike. (No
    // skip where a lamp's light is faint: many lamps' faint light behind a
    // train adds up to a leak through it.)
    if (spot.w > 1.5 && attenuation > 0.0)
        attenuation *= localLightBoxShadow(light, position, placement.xyz);
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
