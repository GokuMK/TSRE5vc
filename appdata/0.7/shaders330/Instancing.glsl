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
