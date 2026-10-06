// Local lights (task 21): lamps and emissive surfaces binned by the QRhi
// renderer into a world-space grid around the camera (LightGrid). Included
// by the lit standard shaders under TSRE_RHI; localLightLayers.y is the
// light count, 0 when there are none.
uniform vec4 localLightGrid;   // grid origin, horizontal cell size
uniform vec4 localLightLayers; // vertical cell size, light count
// Four texels per light, one light per row: position and range; colour
// times intensity and squared radius; spot direction and spot flag;
// cosine of the outer angle and 1 / (cos inner - cos outer).
uniform sampler2D localLightData;
// First index and count of each cell (x + z * 64 across, y down).
uniform sampler2D localLightCells;
// Light indices, 4096 per row.
uniform sampler2D localLightIndices;

const float LocalLightPi = 3.14159265;

// First index and count of the lights reaching a position.
ivec2 localLightCell(vec3 position) {
    if (localLightLayers.y <= 0.0)
        return ivec2(0);
    vec3 size = vec3(localLightGrid.w, localLightLayers.x, localLightGrid.w);
    ivec3 cell = ivec3(floor((position - localLightGrid.xyz) / size));
    if (any(lessThan(cell, ivec3(0))) || any(greaterThanEqual(cell, ivec3(64, 16, 64))))
        return ivec2(0);
    return ivec2(texelFetch(localLightCells, ivec2(cell.x + cell.z * 64, cell.y), 0).xy);
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
