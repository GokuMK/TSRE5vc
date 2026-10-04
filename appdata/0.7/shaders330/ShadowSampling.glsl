// Sun visibility of a lit fragment from the near, middle and far shadow
// maps. Each fragment samples one map: the near map (shadow0) covers the
// camera's surroundings in more detail, shadow1 takes over outside it and
// shadow2 beyond that. Included by the lit standard shaders after their
// shadow inputs, uniforms and poissonDisk.
float shadowedVisibility(float cosTheta) {
    float visibility = (1.0-enableNormals) + cosTheta*enableNormals;
    float shadowIntensity = 0.1;
    float bias = shadow1Bias*tan(acos(cosTheta))*enableNormals + 0.0025*(1.0-enableNormals);
    bias = clamp(bias, 0, 0.01);

    vec4 shadowPos2 = shadowPos*0.5+0.5;
    vec4 shadow2Pos2 = shadow2Pos*0.5+0.5;
    float camdist = length(shadowPos.xyz);
    camdist = clamp(camdist, 0, 1.0);
    float camdist2 = length(shadow2Pos.xyz);
    camdist2 = clamp(camdist2, 0, 1.0);
    vec4 shadow0Pos2 = shadow0Pos*0.5+0.5;
    float t0 = 1.0 - floor(clamp(length(shadow0Pos.xyz), 0, 1.0));
    // The near map covers a third of the middle map's extent at the same
    // depth range; spread its taps three times wider in texels to keep the
    // same soft falloff, and scale the bias with its three times smaller
    // texels so contact shadows survive.
    float shadow0Res = shadow1Res/3.0;
    float bias0 = bias/3.0;
    float tMid = 1.0 - floor(camdist);
    float t = tMid*(1.0 - t0);
    float t2 = 1.0 - floor(camdist2);

    float shadowsEnabled2 = shadowsEnabled;
    float bias2 = shadow2Bias;
    // One map per fragment: the weights below are 0 or 1.
    if (t0 > 0.0) {
        for (int i = 0; i < 16; i++)
            visibility -= shadowsEnabled2*t0*shadowIntensity*(1.0-texture( shadow0, vec3(shadow0Pos2.xy + poissonDisk[i]/shadow0Res, (shadow0Pos2.z-bias0)) ));
    } else if (t > 0.0) {
        for (int i = 0; i < 16; i++)
            visibility -= shadowsEnabled2*t*shadowIntensity*(1.0-texture( shadow1, vec3(shadowPos2.xy + poissonDisk[i]/shadow1Res, (shadowPos2.z-bias)) ));
    } else {
        for (int i = 0; i < 2; i++)
            visibility -= shadowsEnabled2*(1.0-tMid)*t2*0.4*(1.0-texture( shadow2, vec3(shadow2Pos2.xy + poissonDisk[i]/shadow2Res, (shadow2Pos2.z-bias2)) ));
    }
    return visibility;
}
