#version 330 core

in vec2 vTextureCoord;
in float fogFactor;
in vec3 vNormal;
in vec4 shadowPos;
in vec4 shadow2Pos;
in vec4 shadow0Pos;
in float vAlpha;
in float vTerrainGap;
in vec2 vTerrainMapCoord;
out vec4 fragColor;

uniform float textureEnabled;
uniform int shadowsEnabled;
uniform float shadow1Res;
uniform float shadow1Bias;
uniform float shadow2Res;
uniform float shadow2Bias;
uniform vec4 shapeColor;
uniform float isAlpha;
uniform float alphaTest;
uniform vec4 skyColor;
uniform vec4 diffuseColor;
uniform vec4 ambientColor;
uniform vec4 specularColor;
uniform vec3 lightDirection;
uniform sampler2D uSampler;
uniform sampler2D uSampler2;
uniform sampler2DShadow shadow1;
uniform sampler2DShadow shadow2;
uniform sampler2DShadow shadow0;
uniform float secondTexEnabled;
uniform mat4 uMVMatrix;
flat in mat4 vModelView;
uniform mat4 uMSMatrix;
uniform float enableNormals;
uniform float colorBrightness;

vec2 poissonDisk[16] = vec2[]( 
   vec2( -0.94201624, -0.39906216 ), 
   vec2( 0.94558609, -0.76890725 ), 
   vec2( -0.094184101, -0.92938870 ), 
   vec2( 0.34495938, 0.29387760 ), 
   vec2( -0.91588581, 0.45771432 ), 
   vec2( -0.81544232, -0.87912464 ), 
   vec2( -0.38277543, 0.27676845 ), 
   vec2( 0.97484398, 0.75648379 ), 
   vec2( 0.44323325, -0.97511554 ), 
   vec2( 0.53742981, -0.47373420 ), 
   vec2( -0.26496911, -0.41893023 ), 
   vec2( 0.79197514, 0.19090188 ), 
   vec2( -0.24188840, 0.99706507 ), 
   vec2( -0.81409955, 0.91437590 ), 
   vec2( 0.19984126, 0.78641367 ), 
   vec2( 0.14383161, -0.14100790 ) 
);

float random(vec3 seed, int i){
	vec4 seed4 = vec4(seed,i);
	float dot_product = dot(seed4, vec4(12.9898,78.233,45.164,94.673));
	return fract(sin(dot_product) * 43758.5453);
}

float insideBox(vec2 v, vec2 bottomLeft, vec2 topRight) {
    vec2 s = step(bottomLeft, v) - step(topRight, v);
    return s.x * s.y;   
}

#ifdef TSRE_TERRAIN
#include "TerrainMaterial.glsl"
#endif

void main() {
#ifdef TSRE_TERRAIN
        if(vTerrainGap > 0.0)
            discard;
#endif
        if(textureEnabled == 0) {
            fragColor = shapeColor;
        } else {
            fragColor = texture(uSampler, vec2(vTextureCoord.s, vTextureCoord.t));
            vec4 tex2 = texture(uSampler2, vec2(vTextureCoord.s*secondTexEnabled, vTextureCoord.t*secondTexEnabled));
            float isSecondTexEnabled = sign(secondTexEnabled);
            fragColor = fragColor*(1-isSecondTexEnabled) + fragColor*tex2*2.0*isSecondTexEnabled;
#ifdef TSRE_TERRAIN
            if(terrainMaterialEnabled != 0)
                fragColor = terrainMaterialColor();
#endif
            // discard if transparent
            //if(gl_FragColor.a < alphaTest)
            //    discard;    
            fragColor.a = max(fragColor.a, vAlpha);  
            // discard if transparent 
            if(fragColor.a < -vAlpha)
                discard;
            //gl_FragColor.a = 1.0;

#ifdef TSRE_UNLIT
            // Overlays: no sun lighting, shadows or fog.
            fragColor.xyz *= colorBrightness;
#else
            // calculate normals
            vec3 normal = normalize(mat3(vModelView) * mat3(uMSMatrix) * vNormal);
            vec3 lights = normalize(lightDirection);
            float cosTheta = clamp(dot( normal, lights ), 0, 1);
            float visibility = (1.0-enableNormals) + cosTheta*enableNormals;
            float shadowIntensity = 0.1;
            //float shadow1Res = 2000.0;
            float bias = shadow1Bias*tan(acos(cosTheta))*enableNormals + 0.0025*(1.0-enableNormals);
            //float shadow1Res = 5000.0;
            //float bias = 0.0005*tan(acos(cosTheta))*enableNormals + 0.0025*(1.0-enableNormals);
            bias = clamp(bias, 0, 0.01);

            // calculate shadows
            vec4 shadowPos2 = shadowPos*0.5+0.5;
            vec4 shadow2Pos2 = shadow2Pos*0.5+0.5;
            float camdist = length(shadowPos.xyz);
            camdist = clamp(camdist, 0, 1.0);
            float camdist2 = length(shadow2Pos.xyz);
            camdist2 = clamp(camdist2, 0, 1.0);
            // The near map (shadow0) covers the camera's surroundings in more
            // detail; shadow1 takes over outside it and shadow2 beyond that.
            vec4 shadow0Pos2 = shadow0Pos*0.5+0.5;
            float t0 = 1.0 - floor(clamp(length(shadow0Pos.xyz), 0, 1.0));
            // The near map covers a third of the middle map's extent; spread its
            // taps three times wider in texels to keep the same soft falloff.
            float shadow0Res = shadow1Res/3.0;
            float tMid = 1.0 - floor(camdist);
            float t = tMid*(1.0 - t0);
            float t2 = 1.0 - floor(camdist2);

            float shadowsEnabled2 = shadowsEnabled;
            float bias2 = shadow2Bias;//0.001; // 0.002;
            // One map per fragment: the weights below are 0 or 1.
            if (t0 > 0.0) {
                visibility -= shadowsEnabled2*t0*shadowIntensity*(1.0-texture( shadow0, vec3(shadow0Pos2.xy + poissonDisk[0]/shadow0Res, (shadow0Pos2.z-bias)) ));
                visibility -= shadowsEnabled2*t0*shadowIntensity*(1.0-texture( shadow0, vec3(shadow0Pos2.xy + poissonDisk[1]/shadow0Res, (shadow0Pos2.z-bias)) ));
                visibility -= shadowsEnabled2*t0*shadowIntensity*(1.0-texture( shadow0, vec3(shadow0Pos2.xy + poissonDisk[2]/shadow0Res, (shadow0Pos2.z-bias)) ));
                visibility -= shadowsEnabled2*t0*shadowIntensity*(1.0-texture( shadow0, vec3(shadow0Pos2.xy + poissonDisk[3]/shadow0Res, (shadow0Pos2.z-bias)) ));
                visibility -= shadowsEnabled2*t0*shadowIntensity*(1.0-texture( shadow0, vec3(shadow0Pos2.xy + poissonDisk[4]/shadow0Res, (shadow0Pos2.z-bias)) ));
                visibility -= shadowsEnabled2*t0*shadowIntensity*(1.0-texture( shadow0, vec3(shadow0Pos2.xy + poissonDisk[5]/shadow0Res, (shadow0Pos2.z-bias)) ));
                visibility -= shadowsEnabled2*t0*shadowIntensity*(1.0-texture( shadow0, vec3(shadow0Pos2.xy + poissonDisk[6]/shadow0Res, (shadow0Pos2.z-bias)) ));
                visibility -= shadowsEnabled2*t0*shadowIntensity*(1.0-texture( shadow0, vec3(shadow0Pos2.xy + poissonDisk[7]/shadow0Res, (shadow0Pos2.z-bias)) ));
                visibility -= shadowsEnabled2*t0*shadowIntensity*(1.0-texture( shadow0, vec3(shadow0Pos2.xy + poissonDisk[8]/shadow0Res, (shadow0Pos2.z-bias)) ));
                visibility -= shadowsEnabled2*t0*shadowIntensity*(1.0-texture( shadow0, vec3(shadow0Pos2.xy + poissonDisk[9]/shadow0Res, (shadow0Pos2.z-bias)) ));
                visibility -= shadowsEnabled2*t0*shadowIntensity*(1.0-texture( shadow0, vec3(shadow0Pos2.xy + poissonDisk[10]/shadow0Res, (shadow0Pos2.z-bias)) ));
                visibility -= shadowsEnabled2*t0*shadowIntensity*(1.0-texture( shadow0, vec3(shadow0Pos2.xy + poissonDisk[11]/shadow0Res, (shadow0Pos2.z-bias)) ));
                visibility -= shadowsEnabled2*t0*shadowIntensity*(1.0-texture( shadow0, vec3(shadow0Pos2.xy + poissonDisk[12]/shadow0Res, (shadow0Pos2.z-bias)) ));
                visibility -= shadowsEnabled2*t0*shadowIntensity*(1.0-texture( shadow0, vec3(shadow0Pos2.xy + poissonDisk[13]/shadow0Res, (shadow0Pos2.z-bias)) ));
                visibility -= shadowsEnabled2*t0*shadowIntensity*(1.0-texture( shadow0, vec3(shadow0Pos2.xy + poissonDisk[14]/shadow0Res, (shadow0Pos2.z-bias)) ));
                visibility -= shadowsEnabled2*t0*shadowIntensity*(1.0-texture( shadow0, vec3(shadow0Pos2.xy + poissonDisk[15]/shadow0Res, (shadow0Pos2.z-bias)) ));
            } else if (t > 0.0) {
                visibility -= shadowsEnabled2*t*shadowIntensity*(1.0-texture( shadow1, vec3(shadowPos2.xy + poissonDisk[0]/shadow1Res, (shadowPos2.z-bias)) ));
                visibility -= shadowsEnabled2*t*shadowIntensity*(1.0-texture( shadow1, vec3(shadowPos2.xy + poissonDisk[1]/shadow1Res, (shadowPos2.z-bias)) ));
                visibility -= shadowsEnabled2*t*shadowIntensity*(1.0-texture( shadow1, vec3(shadowPos2.xy + poissonDisk[2]/shadow1Res, (shadowPos2.z-bias)) ));
                visibility -= shadowsEnabled2*t*shadowIntensity*(1.0-texture( shadow1, vec3(shadowPos2.xy + poissonDisk[3]/shadow1Res, (shadowPos2.z-bias)) ));
                visibility -= shadowsEnabled2*t*shadowIntensity*(1.0-texture( shadow1, vec3(shadowPos2.xy + poissonDisk[4]/shadow1Res, (shadowPos2.z-bias)) ));
                visibility -= shadowsEnabled2*t*shadowIntensity*(1.0-texture( shadow1, vec3(shadowPos2.xy + poissonDisk[5]/shadow1Res, (shadowPos2.z-bias)) ));
                visibility -= shadowsEnabled2*t*shadowIntensity*(1.0-texture( shadow1, vec3(shadowPos2.xy + poissonDisk[6]/shadow1Res, (shadowPos2.z-bias)) ));
                visibility -= shadowsEnabled2*t*shadowIntensity*(1.0-texture( shadow1, vec3(shadowPos2.xy + poissonDisk[7]/shadow1Res, (shadowPos2.z-bias)) ));
                visibility -= shadowsEnabled2*t*shadowIntensity*(1.0-texture( shadow1, vec3(shadowPos2.xy + poissonDisk[8]/shadow1Res, (shadowPos2.z-bias)) ));
                visibility -= shadowsEnabled2*t*shadowIntensity*(1.0-texture( shadow1, vec3(shadowPos2.xy + poissonDisk[9]/shadow1Res, (shadowPos2.z-bias)) ));
                visibility -= shadowsEnabled2*t*shadowIntensity*(1.0-texture( shadow1, vec3(shadowPos2.xy + poissonDisk[10]/shadow1Res, (shadowPos2.z-bias)) ));
                visibility -= shadowsEnabled2*t*shadowIntensity*(1.0-texture( shadow1, vec3(shadowPos2.xy + poissonDisk[11]/shadow1Res, (shadowPos2.z-bias)) ));
                visibility -= shadowsEnabled2*t*shadowIntensity*(1.0-texture( shadow1, vec3(shadowPos2.xy + poissonDisk[12]/shadow1Res, (shadowPos2.z-bias)) ));
                visibility -= shadowsEnabled2*t*shadowIntensity*(1.0-texture( shadow1, vec3(shadowPos2.xy + poissonDisk[13]/shadow1Res, (shadowPos2.z-bias)) ));
                visibility -= shadowsEnabled2*t*shadowIntensity*(1.0-texture( shadow1, vec3(shadowPos2.xy + poissonDisk[14]/shadow1Res, (shadowPos2.z-bias)) ));
                visibility -= shadowsEnabled2*t*shadowIntensity*(1.0-texture( shadow1, vec3(shadowPos2.xy + poissonDisk[15]/shadow1Res, (shadowPos2.z-bias)) ));
            } else {
                visibility -= shadowsEnabled2*(1.0-tMid)*t2*0.4*(1.0-texture( shadow2, vec3(shadow2Pos2.xy + poissonDisk[0]/shadow2Res, (shadow2Pos2.z-bias2)) ));
                visibility -= shadowsEnabled2*(1.0-tMid)*t2*0.4*(1.0-texture( shadow2, vec3(shadow2Pos2.xy + poissonDisk[1]/shadow2Res, (shadow2Pos2.z-bias2)) ));
            }
            
            // calculate light color
            vec3 color = diffuseColor.xyz;
            color *= clamp(visibility, 0.0, 1.0);
            color += ambientColor.xyz;
            fragColor.xyz *= color*colorBrightness;

            fragColor = mix(fragColor, skyColor, fogFactor);
#endif
        }
}
