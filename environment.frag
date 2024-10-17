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
// layout(set=2, binding=0) uniform sampler2D TEXTURE;

layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texCoord;


layout(location=0) out vec4 outColor;

void main() {
	vec3 n = normalize(normal);
	vec3 radiance = vec3(texture(ENVIRONMENT, n));
	outColor = vec4(ACESFitted(radiance), 1.0f);
}