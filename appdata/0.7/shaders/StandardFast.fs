#version 140

varying vec2 vTextureCoord;
varying float fogFactor;
varying vec3 vNormal;
varying float vAlpha;
varying float vTerrainGap;

uniform float textureEnabled;
uniform vec4 shapeColor;
uniform vec4 skyColor;
uniform vec4 diffuseColor;
uniform vec4 ambientColor;
uniform vec3 lightDirection;
uniform sampler2D uSampler;
uniform sampler2D uSampler2;
uniform float secondTexEnabled;
uniform mat4 uMVMatrix;
uniform mat4 uMSMatrix;
uniform float enableNormals;
uniform float colorBrightness;

void main() {
    if (vTerrainGap > 0.0)
        discard;

    if (textureEnabled == 0.0) {
        gl_FragColor = shapeColor;
        return;
    }

    gl_FragColor = texture(uSampler, vTextureCoord);
    if (secondTexEnabled != 0.0) {
        vec4 detail = texture(uSampler2, vTextureCoord * secondTexEnabled);
        gl_FragColor *= detail * 2.0;
    }
    gl_FragColor.a = max(gl_FragColor.a, vAlpha);
    if (gl_FragColor.a < -vAlpha)
        discard;

    vec3 normal = normalize(mat3(uMVMatrix) * mat3(uMSMatrix) * vNormal);
    float cosTheta = clamp(dot(normal, normalize(lightDirection)), 0.0, 1.0);
    float visibility = (1.0 - enableNormals) + cosTheta * enableNormals;
    gl_FragColor.rgb *= (diffuseColor.rgb * clamp(visibility, 0.0, 1.0)
            + ambientColor.rgb)
            * colorBrightness;
    gl_FragColor = mix(gl_FragColor, skyColor, fogFactor);
}
