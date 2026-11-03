#version 330 core

// Smoke and steam (task renderer 41, RhiExhaust.cpp): the particles' light
// pass, one fragment a particle. A triangle over the whole target; the
// fragment's texel is the particle's.
void main() {
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
