#ifdef TSRE_SKINNED
// Skinned glTF meshes (task humans 02): up to four joints a vertex, their
// weights, and the drawing state's joint matrices (joint global times
// inverse bind, shape space) as three rows of four floats each, set per
// draw. 64 joints at most.
in vec4 joints;
in vec4 weights;
uniform vec4 jointRows[192];

mat4 jointMatrix(int joint) {
    int at = joint * 3;
    vec4 r0 = jointRows[at];
    vec4 r1 = jointRows[at + 1];
    vec4 r2 = jointRows[at + 2];
    // Columns of the matrix whose rows are r0, r1, r2 and (0, 0, 0, 1).
    return mat4(vec4(r0.x, r1.x, r2.x, 0.0), vec4(r0.y, r1.y, r2.y, 0.0),
                vec4(r0.z, r1.z, r2.z, 0.0), vec4(r0.w, r1.w, r2.w, 1.0));
}

// The weighted joint matrix of this vertex.
mat4 skinMatrix() {
    ivec4 j = clamp(ivec4(joints + 0.5), ivec4(0), ivec4(63));
    return jointMatrix(j.x) * weights.x + jointMatrix(j.y) * weights.y
            + jointMatrix(j.z) * weights.z + jointMatrix(j.w) * weights.w;
}
#endif
