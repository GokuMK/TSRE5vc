// MSTS specular light materials (task shapes 13) for the main program's
// shapes: light material -6 a broad sheen (power 25: painted bodies, rail
// heads), -7 a tight glint (power 750: chrome, glass); 0 none. A normalized
// Blinn-Phong highlight of the sun, in the colour and strength it lights the
// scene with, shadowed, and the sky's light reflected at grazing angles
// (Fresnel); linear colour, added over the lit texture untinted, as a
// dielectric reflects. The highlight widens and dims where a pixel spans
// many normals (curves, distance), so rails thinner than a pixel do not
// twinkle. Included by StandardFog.fs.
uniform float materialSpecular;
#ifndef TSRE_CAMERA_POSITION
#define TSRE_CAMERA_POSITION
uniform vec3 cameraPosition;
#endif

// Reflectance head-on of the sky (paint, varnish, glass) and, more, of the
// sun: MSTS authors meant these parts to shine, rail heads most (polished
// steel). Open Rails adds the sun's whole light at the peak; a power 25
// sheen here peaks at about that ((25 + 8) / 8 * 0.25), a power 750 glint
// far above it. At 0.08 bbb's rails gained 40-60 of 255 in the evening
// sun, too little to see (the user, 2026-10-27).
const float MstsSpecularF0 = 0.04;
const float MstsSpecularSunF0 = 0.25;
// The spread of normals a pixel takes per metre away (a 1 m wide pixel
// footprint at 2 km's lens): a 750 glint at 200 m spreads to about 90.
const float MstsSpecularFar = 1.0 / 2000.0;

vec3 mstsSpecular(vec3 n, vec3 l, float visibility, vec3 skyLight) {
    vec3 toCamera = cameraPosition - vLightPosition;
    vec3 v = normalize(toCamera);
    // Thin double-sided parts reflect on the side they are seen from.
    if (dot(n, v) < 0.0)
        n = -n;
    // Normals spread over the pixel (Toksvig): their change across it, and
    // its footprint far away.
    vec3 dx = dFdx(n), dy = dFdy(n);
    float far = length(toCamera) * MstsSpecularFar;
    float spread = 0.5 * (dot(dx, dx) + dot(dy, dy)) + far * far;
    float power = materialSpecular / (1.0 + materialSpecular * spread);
    vec3 shine = vec3(0.0);
    float nDotL = dot(n, l);
    if (nDotL > 0.0) {
        vec3 h = normalize(l + v);
        float fresnel = MstsSpecularSunF0
                + (1.0 - MstsSpecularSunF0) * pow(1.0 - max(dot(v, h), 0.0), 5.0);
        float lobe = (power + 8.0) / 8.0 * pow(max(dot(n, h), 0.0), power);
        shine = pow(max(diffuseColor.rgb, vec3(0.0)), vec3(2.2)) * (visibility * nDotL * fresnel * lobe);
    }
    // The sky, mirrored the more the smoother the surface, towards grazing
    // angles.
    float smoothness = 1.0 - sqrt(2.0 / (power + 2.0));
    float skyFresnel = MstsSpecularF0 + max(smoothness - MstsSpecularF0, 0.0)
            * pow(1.0 - max(dot(n, v), 0.0), 5.0);
    return shine + pow(max(skyLight, vec3(0.0)), vec3(2.2)) * skyFresnel;
}
