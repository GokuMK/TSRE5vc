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

// 0 open to 1 enclosed: the share of 16 taps around the point with
// something more than 1.5-4 m above it. The point is moved 2 m off walls
// first, past the eaves, so they do not count their own roof.
float skyEnclosed(vec3 position, vec3 normal) {
    if (skyEnclosureParams.x <= 0.0)
        return 0.0;
    vec2 side = normal.xz;
    float sideLength = length(side);
    vec3 p = position;
    if (sideLength > 1e-3)
        p += vec3(side.x, 0.0, side.y) / sideLength * min(sideLength * 3.0, 1.0) * 2.0;
    vec4 clip = skyEnclosureMatrix * vec4(p, 1.0);
    vec3 m = clip.xyz / clip.w * 0.5 + 0.5;
    // Outside the map, above or below its depth range: open sky, the map
    // knows nothing there (below its far plane every tap read as covered).
    if (m.z <= 0.0 || m.z >= 1.0)
        return 0.0;
    // Towards the map's edge back to open sky gradually, not at a line.
    float inside = 1.0 - smoothstep(0.35, 0.48, max(abs(m.x - 0.5), abs(m.y - 0.5)));
    if (inside <= 0.0)
        return 0.0;
    float sum = 0.0;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) {
            float top = textureLod(skyEnclosureMap, m.xy + (vec2(i, j) - 1.5) * skyEnclosureParams.z, 0.0).r;
            sum += smoothstep(1.5, 4.0, (m.z - top) * skyEnclosureParams.y);
        }
    // Only wide cover counts: a roof or the terrain over a tunnel covers all
    // the taps (half at its edge), while thin things overhead (wires, lamp
    // heads, gantries) cover a few, which lit the ground under them in the
    // map's texels with lamps at night strength by day (user, 2026-10-12).
    return smoothstep(0.3, 0.9, sum / 16.0) * inside;
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
