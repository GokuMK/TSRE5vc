// Sun visibility of a lit fragment from the near, middle and far shadow
// maps. Each fragment samples one map: the near map (shadow0) covers the
// camera's surroundings in more detail, shadow1 takes over outside it and
// shadow2 beyond that. Included by the lit standard shaders after their
// shadow inputs, uniforms and poissonDisk.

// Tap spread (x near, y middle) and depth-bias (z near, w middle) scales of
// the near and middle maps; they keep the blur and the bias of surfaces
// without normals the same in world space in every map.
uniform vec4 shadowMapScale;
// Depth bias of the near and middle maps for surfaces with normals, whose
// lookups the vertex shader moves along the normal.
uniform vec2 shadowDepthBias;

// Receiver-plane depth bias: the change of the fragment's shadow-map depth
// per unit of shadow-map UV, from the screen-space derivatives of its
// shadow coordinates. Each filter tap compares against the receiver's depth
// at the tap, not at the centre, so wide filters do not shadow sloped
// surfaces themselves. Zero where the derivatives are degenerate.
vec2 receiverPlaneGradient(vec3 coord) {
    vec3 dx = dFdx(coord);
    vec3 dy = dFdy(coord);
    float det = dx.x*dy.y - dx.y*dy.x;
    if (abs(det) < 1e-14)
        return vec2(0.0);
    return vec2(dy.y*dx.z - dx.y*dy.z, dx.x*dy.z - dy.x*dx.z) / det;
}

// Receiver depth change at a tap offset, limited at grazing angles and at
// silhouettes, where the derivatives span different surfaces.
float receiverPlaneBias(vec2 gradient, vec2 offset, float limit) {
    return clamp(dot(gradient, offset), -limit, limit);
}

#ifdef TSRE_RHI
// Clouds between the sun and a point (task 28): a map of their
// transmittance along the sun from a plane under them, around the camera.
uniform sampler2D cloudShadowMap;
// The map's centre (x, z), 1 / its extent, on (1) or off (0).
uniform vec4 cloudShadowArea;
// The sun's x / y and z / y (along it to the map's plane), the plane's y,
// the clouds' average shade (past the map).
uniform vec4 cloudShadowSun;

float cloudShadow(vec3 position) {
    if (cloudShadowArea.w <= 0.0)
        return 1.0;
    vec2 p = position.xz + cloudShadowSun.xy * (cloudShadowSun.z - position.y);
    vec2 uv = (p - cloudShadowArea.xy) * cloudShadowArea.z + 0.5;
    // Past the map the clouds' average shade (its clamped edge would
    // stretch into stripes), with no hard border.
    float inside = smoothstep(0.5, 0.45, max(abs(uv.x - 0.5), abs(uv.y - 0.5)));
    float shade = mix(cloudShadowSun.w, textureLod(cloudShadowMap, uv, 0.0).r, inside);
    return mix(1.0, shade, cloudShadowArea.w);
}
#endif

#ifdef TSRE_RHI
// The sky enclosure (task 27): the depth of the highest surface seen from
// straight above around the camera. A point with something over it (terrain
// over a tunnel, a deck, a roof) is enclosed: it gets less of the sky's
// ambient light, and lamps keep their night strength there by day.
uniform sampler2D skyEnclosureMap;
uniform mat4 skyEnclosureMatrix;
// On (1) or off (0), metres per unit of the map's depth, the filter's
// spacing (map coordinates), the ambient light left where fully enclosed.
uniform vec4 skyEnclosureParams;
// The daylight scale of lamps where the sky is open (1 without the map).
uniform float localLightDay;
// The far map behind it (wider, coarser; terrain and large objects): on (1)
// or off (0), its filter's spacing.
uniform sampler2D skyEnclosureFarMap;
uniform mat4 skyEnclosureFarMatrix;
uniform vec2 skyEnclosureFar;
// How open the sky is around each point of the near and the far map (0 deep
// under cover, towards 1 near open sky), on (1) or off (0) each.
uniform sampler2D skyEnclosureOpenMap;
uniform sampler2D skyEnclosureFarOpenMap;
uniform vec2 skyEnclosureOpen;

// Where a map has the point: its map coordinates and depth, and how much the
// map counts there (1 inside, fading to 0 towards its edge; 0 outside it or
// its depth range, where it knows nothing: below its far plane every tap
// read as covered).
float enclosureShare(mat4 matrix, vec3 p, float fadeStart, float fadeEnd, out vec3 m) {
    vec4 clip = matrix * vec4(p, 1.0);
    m = clip.xyz / clip.w * 0.5 + 0.5;
    if (m.z <= 0.0 || m.z >= 1.0)
        return 0.0;
    return 1.0 - smoothstep(fadeStart, fadeEnd, max(abs(m.x - 0.5), abs(m.y - 0.5)));
}

// 0 open to 1 enclosed in one map: the share of 16 taps around the point
// with something more than 1.5-4 m above it. Only wide cover counts: a roof
// or the terrain over a tunnel covers all the taps (half at its edge), while
// thin things overhead (wires, lamp heads, gantries) cover a few, which lit
// the ground under them in the map's texels with lamps at night strength by
// day (user, 2026-10-12).
// The 16 texels come in 4 gathers (2 x 2 texels each, a filter step
// apart): a quarter of the fetches of 16 taps (5-7 ms at 4K).
float enclosureTaps(sampler2D map, vec3 m, float spacing) {
    float sum = 0.0;
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
            vec4 top = textureGather(map, m.xy + (vec2(i, j) - 0.5) * 2.0 * spacing);
            sum += dot(smoothstep(vec4(1.5), vec4(4.0), (m.z - top) * skyEnclosureParams.y), vec4(1.0));
        }
    return smoothstep(0.3, 0.9, sum / 16.0);
}

// The same in the far map, coarse anyway: one gather.
float enclosureTapsCoarse(sampler2D map, vec3 m, float spacing) {
    vec4 top = textureGather(map, m.xy);
    float sum = dot(smoothstep(vec4(1.5), vec4(4.0), (m.z - top) * skyEnclosureParams.y), vec4(1.0));
    return smoothstep(0.3, 0.9, sum / 4.0);
}

// 0 open to 1 enclosed: the near map, fading into the far one towards its
// edge, the far one into open sky towards its own. The point is moved 2 m off
// walls first, past the eaves, so they do not count their own roof.
// shade: how deep under cover, for the sky's ambient light and the lamps:
// the enclosure less where open sky is near, so a tunnel darkens from its
// portals in and the arches of a viaduct, open at both sides, stay lighter
// (user, 2026-10-12).
float skyEnclosed(vec3 position, vec3 normal, out float shade) {
    shade = 0.0;
    if (skyEnclosureParams.x <= 0.0)
        return 0.0;
    vec2 side = normal.xz;
    float sideLength = length(side);
    vec3 p = position;
    if (sideLength > 1e-3)
        p += vec3(side.x, 0.0, side.y) / sideLength * min(sideLength * 3.0, 1.0) * 2.0;
    vec3 m;
    float near = enclosureShare(skyEnclosureMatrix, p, 0.35, 0.48, m);
    float enclosed = 0.0;
    if (near > 0.0) {
        float e = enclosureTaps(skyEnclosureMap, m, skyEnclosureParams.z) * near;
        enclosed = e;
        shade = skyEnclosureOpen.x > 0.0 ? e * (1.0 - textureLod(skyEnclosureOpenMap, m.xy, 0.0).r) : e;
    }
    if (near < 1.0 && skyEnclosureFar.x > 0.0) {
        vec3 f;
        float far = enclosureShare(skyEnclosureFarMatrix, p, 0.4, 0.49, f);
        if (far > 0.0) {
            float e = enclosureTapsCoarse(skyEnclosureFarMap, f, skyEnclosureFar.y) * far * (1.0 - near);
            enclosed += e;
            shade += skyEnclosureOpen.y > 0.0 ? e * (1.0 - textureLod(skyEnclosureFarOpenMap, f.xy, 0.0).r) : e;
        }
    }
    return enclosed;
}

// Whether the maps have cover more than 0.5-2 m over a point (0-1): the
// near map, else the far one, else none.
float coverOver(vec3 q) {
    vec3 m;
    if (enclosureShare(skyEnclosureMatrix, q, 0.45, 0.48, m) > 0.5)
        return smoothstep(0.5, 2.0, (m.z - textureLod(skyEnclosureMap, m.xy, 0.0).r) * skyEnclosureParams.y);
    if (skyEnclosureFar.x > 0.0 && enclosureShare(skyEnclosureFarMatrix, q, 0.45, 0.49, m) > 0.5)
        return smoothstep(0.5, 2.0, (m.z - textureLod(skyEnclosureFarMap, m.xy, 0.0).r) * skyEnclosureParams.y);
    return 0.0;
}

// The sun past the covers the shadow maps miss (terrain casts no shadow;
// tunnel tubes, seen from inside, face the sun with their backs): where a
// point is enclosed, the points 3 and 12 m higher on its way to the sun are
// looked up; cover over either blocks the sun (deep under a roof or in a
// tunnel), none lets it in (under a roof's edge). 1 in the open, with no
// lookups (user, 2026-10-12: a tunnel floor in the noon sun).
float enclosureSun(vec3 position, float enclosed, vec3 sunDirection) {
    if (enclosed <= 0.0)
        return 1.0;
    vec3 s = normalize(sunDirection);
    if (s.y < 0.05)
        return 1.0;
    float blocked = max(coverOver(position + s * (3.0 / s.y)), coverOver(position + s * (12.0 / s.y)));
    return 1.0 - blocked * enclosed;
}

float skyAmbient(float enclosed) {
    return mix(1.0, skyEnclosureParams.w, enclosed);
}

float lampScale(float enclosed) {
    return mix(localLightDay, 1.0, enclosed);
}
#endif

float shadowedVisibility(float cosTheta) {
    float visibility = (1.0-enableNormals) + cosTheta*enableNormals;
    float shadowIntensity = 0.1;
    // Constant bias of surfaces without normals, scaled per map below.
    float bias = 0.0025;

    vec4 shadowPos2 = shadowPos*0.5+0.5;
    vec4 shadow2Pos2 = shadow2Pos*0.5+0.5;
    float camdist = length(shadowPos.xyz);
    camdist = clamp(camdist, 0, 1.0);
    float camdist2 = length(shadow2Pos.xyz);
    camdist2 = clamp(camdist2, 0, 1.0);
    vec4 shadow0Pos2 = shadow0Pos*0.5+0.5;
    float t0 = 1.0 - floor(clamp(length(shadow0Pos.xyz), 0, 1.0));
    float shadow0Res = shadow1Res*shadowMapScale.x;
    float shadowMidRes = shadow1Res*shadowMapScale.y;
    // Without normals keep the tuned bias.
    float bias0 = mix(bias*shadowMapScale.z, shadowDepthBias.x, enableNormals);
    float biasMid = mix(bias*shadowMapScale.w, shadowDepthBias.y, enableNormals);
    float tMid = 1.0 - floor(camdist);
    float t = tMid*(1.0 - t0);
    float t2 = 1.0 - floor(camdist2);

    float shadowsEnabled2 = shadowsEnabled;
    float bias2 = shadow2Bias;
    // Derivatives need all fragments of a quad, so take them before branching.
    vec2 gradient0 = receiverPlaneGradient(shadow0Pos2.xyz);
    vec2 gradientMid = receiverPlaneGradient(shadowPos2.xyz);
    // Up to a slope of about 70 degrees across the filter.
    float limit0 = 3.0*shadowDepthBias.x;
    float limitMid = 3.0*shadowDepthBias.y;
    // One map per fragment: the weights below are 0 or 1.
    if (t0 > 0.0) {
        for (int i = 0; i < 16; i++)
            visibility -= shadowsEnabled2*t0*shadowIntensity*(1.0-texture( shadow0, vec3(shadow0Pos2.xy + poissonDisk[i]/shadow0Res,
                    shadow0Pos2.z - bias0 + receiverPlaneBias(gradient0, poissonDisk[i]/shadow0Res, limit0)) ));
    } else if (t > 0.0) {
        for (int i = 0; i < 16; i++)
            visibility -= shadowsEnabled2*t*shadowIntensity*(1.0-texture( shadow1, vec3(shadowPos2.xy + poissonDisk[i]/shadowMidRes,
                    shadowPos2.z - biasMid + receiverPlaneBias(gradientMid, poissonDisk[i]/shadowMidRes, limitMid)) ));
    } else {
        for (int i = 0; i < 2; i++)
            visibility -= shadowsEnabled2*(1.0-tMid)*t2*0.4*(1.0-texture( shadow2, vec3(shadow2Pos2.xy + poissonDisk[i]/shadow2Res, (shadow2Pos2.z-bias2)) ));
    }
    return visibility;
}
