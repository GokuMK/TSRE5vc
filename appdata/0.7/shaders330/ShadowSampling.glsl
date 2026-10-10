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
// The sun's x / y and z / y (along it to the map's plane) and the plane's y.
uniform vec4 cloudShadowSun;

float cloudShadow(vec3 position) {
    if (cloudShadowArea.w <= 0.0)
        return 1.0;
    vec2 p = position.xz + cloudShadowSun.xy * (cloudShadowSun.z - position.y);
    vec2 uv = (p - cloudShadowArea.xy) * cloudShadowArea.z + 0.5;
    // Past the map, fading to its average edge (no hard border).
    float edge = smoothstep(0.5, 0.45, max(abs(uv.x - 0.5), abs(uv.y - 0.5)));
    return mix(1.0, textureLod(cloudShadowMap, uv, 0.0).r, cloudShadowArea.w * mix(0.6, 1.0, edge));
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
