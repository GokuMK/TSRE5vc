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
// The moving trains over the lamps' maps (task 46 A): a tile a lamp in an
// atlas a set (sets 2 and 3: the fine maps' and the coarse ones'), its map
// copied in, the trains drawn over it, a row of tiles after another.
uniform sampler2D localLightTiles;
uniform sampler2D localLightTilesCoarse;

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

// How wide (texels) a lamp's shadow edge is softened: a box that wide
// around the point, sliding with it through its texel, (width + 1) squared
// texels compared and blended (1: the 2 x 2 blend, edges a texel wide).
const int LocalLightShadowWidth = 3;

// The box's compares (depth: 1 - the point's depth) in a layer of a set's
// cached maps, or in a tile of its atlas (its corner at origin). One of
// them is chosen a lamp, outside the loop: the texture chosen at each texel
// cost 2.5 ms more with the cached maps alone, 4.4 ms with the tiles (the
// standing EMU at Prudnik, the Steam Deck; likely each texel fetched from
// all of them).
float localLightShadowLayer(sampler2DArray maps, int layer, ivec2 base, vec2 f, ivec2 size, float depth) {
    float lit = 0.0;
    for (int y = 0; y <= LocalLightShadowWidth; ++y) {
        float wy = y == 0 ? 1.0 - f.y : (y == LocalLightShadowWidth ? f.y : 1.0);
        for (int x = 0; x <= LocalLightShadowWidth; ++x) {
            float wx = x == 0 ? 1.0 - f.x : (x == LocalLightShadowWidth ? f.x : 1.0);
            float stored = texelFetch(maps, ivec3(clamp(base + ivec2(x, y), ivec2(0), size - 1), layer), 0).r;
            lit += depth >= stored ? wx * wy : 0.0;
        }
    }
    return lit;
}
float localLightShadowTile(sampler2D tiles, ivec2 origin, ivec2 base, vec2 f, ivec2 size, float depth) {
    float lit = 0.0;
    for (int y = 0; y <= LocalLightShadowWidth; ++y) {
        float wy = y == 0 ? 1.0 - f.y : (y == LocalLightShadowWidth ? f.y : 1.0);
        for (int x = 0; x <= LocalLightShadowWidth; ++x) {
            float wx = x == 0 ? 1.0 - f.x : (x == LocalLightShadowWidth ? f.x : 1.0);
            float stored = texelFetch(tiles, origin + clamp(base + ivec2(x, y), ivec2(0), size - 1), 0).r;
            lit += depth >= stored ? wx * wy : 0.0;
        }
    }
    return lit;
}

// How much of a lamp's light passes what stands in its way, by its shadow
// map: compared over LocalLightShadowWidth texels, fading to none toward
// the map's edge (there the lamp's light is weak).
float localLightShadow(int light, float layer, int set, vec3 position) {
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
    bool fine = set == 0 || set == 2;
    ivec2 size = fine ? textureSize(localLightShadows, 0).xy : textureSize(localLightShadowsCoarse, 0).xy;
    // The box's first texel centre and how far into it the box starts.
    vec2 texel = s.xy * vec2(size) - 0.5 - 0.5 * float(LocalLightShadowWidth - 1);
    ivec2 base = ivec2(floor(texel));
    vec2 f = texel - vec2(base);
    int l = int(layer + 0.5);
    float depth = 1.0 - s.z;
    float lit;
    if (set == 0) {
        lit = localLightShadowLayer(localLightShadows, l, base, f, size, depth);
    } else if (set == 1) {
        lit = localLightShadowLayer(localLightShadowsCoarse, l, base, f, size, depth);
    } else {
        // A tile's corner in its atlas.
        int perRow = max(1, (fine ? textureSize(localLightTiles, 0).x : textureSize(localLightTilesCoarse, 0).x) / size.x);
        ivec2 origin = ivec2(l % perRow, l / perRow) * size;
        if (fine)
            lit = localLightShadowTile(localLightTiles, origin, base, f, size, depth);
        else
            lit = localLightShadowTile(localLightTilesCoarse, origin, base, f, size, depth);
    }
    lit /= float(LocalLightShadowWidth * LocalLightShadowWidth);
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
            // Moved toward the lamp against acne, the more the wider the
            // edge (its compares reach farther from the point).
            float shadow = localLightShadow(light, cone.z - 1.0, int(cone.w + 0.5),
                                            position + l * (0.05 + 0.004 * lampDistance)
                                                    * (0.5 + 0.5 * float(LocalLightShadowWidth)));
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
