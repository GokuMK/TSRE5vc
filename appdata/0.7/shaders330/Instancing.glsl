#ifdef TSRE_RHI
// QRhi: every draw is instanced and reads its model-view matrix from
// per-instance attributes.
in vec4 instanceColumn0;
in vec4 instanceColumn1;
in vec4 instanceColumn2;
in vec4 instanceColumn3;

mat4 instanceModelView() {
    return mat4(instanceColumn0, instanceColumn1, instanceColumn2, instanceColumn3);
}
#else
// Instanced draws read each instance's model-view matrix from a buffer
// texture (four RGBA32F texels per matrix); other draws use uMVMatrix.
uniform int instanced;
uniform int instanceBase;
uniform samplerBuffer instanceMatrices;

mat4 instanceModelView() {
    if (instanced == 0)
        return uMVMatrix;
    int texel = (instanceBase + gl_InstanceID) * 4;
    return mat4(texelFetch(instanceMatrices, texel), texelFetch(instanceMatrices, texel + 1),
                texelFetch(instanceMatrices, texel + 2), texelFetch(instanceMatrices, texel + 3));
}
#endif
