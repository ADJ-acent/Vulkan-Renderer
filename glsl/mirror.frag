#version 450
#ifndef TONEMAP
	#include "tonemap.glsl"
#endif
layout(set=0,binding=0,std140) uniform World {
	vec3 SKY_DIRECTION;
	vec3 SKY_ENERGY; //energy supplied by sky to a surface patch with normal = SKY_DIRECTION
	vec3 SUN_DIRECTION;
	vec3 SUN_ENERGY; //energy supplied by sun to a surface patch with normal = SUN_DIRECTION
	vec3 CAMERA_POSITION;
};

layout(set=0, binding=1) uniform samplerCube ENVIRONMENT;
layout(set=2, binding=0) uniform sampler2D NORMAL;
layout(set=2, binding=1) uniform sampler2D DISPLACEMENT;

layout(location=0) in vec3 position;
layout(location=1) in vec2 texCoord;
layout(location=2) in mat3 TBN;


layout(location=0) out vec4 outColor;

void main() {
	// Sample the normal map and convert from [0,1] to [-1,1]
    vec3 normal_rgb = texture(NORMAL, texCoord).rgb; 
    vec3 tangentNormal = normalize(normal_rgb * 2.0 - 1.0); 

    // Transform the normal from tangent space to world space
    vec3 worldNormal = TBN * tangentNormal;

	vec3 viewDir = normalize(position - CAMERA_POSITION);
	vec3 radiance = vec3(textureLod(ENVIRONMENT, reflect(viewDir,worldNormal),0.0f));
	outColor = vec4(ACESFitted(radiance), 1.0f);
}