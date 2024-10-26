#version 450

#ifndef TONEMAP
	#include "tonemap.glsl"
#endif

#ifndef LIGHT
	#include "light.glsl"
#endif

layout(set=0,binding=0,std140) uniform World {
	vec3 CAMERA_POSITION;
	uint ENVIRONMENT_MIPS;
	uint SUN_LIGHT_COUNT;
	uint SPHERE_LIGHT_COUNT;
	uint SPOT_LIGHT_COUNT;
};
layout(set=0, binding=1) uniform samplerCube ENVIRONMENT;

layout(set=0, binding=3, std140) readonly buffer SunLights {
	SunLight SUNLIGHTS[];
};

layout(set=0, binding=4, std140) readonly buffer SphereLights {
	SphereLight SPHERELIGHTS[];
};

layout(set=0, binding=5, std140) readonly buffer SpotLights {
	SpotLight SPOTLIGHTS[];
};

layout(set=2, binding=0) uniform sampler2D NORMAL;
layout(set=2, binding=1) uniform sampler2D DISPLACEMENT;
layout(set=2, binding=2) uniform sampler2D ALBEDO;

layout(location=0) in vec3 position;
layout(location=1) in vec2 texCoord;
layout(location=2) in mat3 TBN;

layout(location=0) out vec4 outColor;

#define PI 3.1415926538

// //hemisphere lighting from direction l:
// vec3 e = SKY_ENERGY * (0.5 * dot(worldNormal,SKY_DIRECTION) + 0.5)
//        + SUN_ENERGY * max(0.0, dot(worldNormal,SUN_DIRECTION)) ;


void main() {

	vec3 albedo = texture(ALBEDO, texCoord).rgb;

	// Sample the normal map and convert from [0,1] to [-1,1]
    vec3 normal_rgb = texture(NORMAL, texCoord).rgb; 
    vec3 tangentNormal = normalize(normal_rgb * 2.0 - 1.0); 

    // Transform the normal from tangent space to world space
    vec3 worldNormal = TBN * tangentNormal;

	vec3 irradiance = textureLod(ENVIRONMENT, worldNormal, ENVIRONMENT_MIPS).rgb;

	for (uint i = 0; i < SUN_LIGHT_COUNT; ++i) {
        irradiance += calculateSunLight(SUNLIGHTS[i], worldNormal);
    }

    // Sphere Light Contributions
    for (uint i = 0; i < SPHERE_LIGHT_COUNT; ++i) {
        irradiance += calculateSphereLight(SPHERELIGHTS[i], position, worldNormal);
    }

    // Spot Light Contributions
    for (uint i = 0; i < SPOT_LIGHT_COUNT; ++i) {
        irradiance += calculateSpotLight(SPOTLIGHTS[i], position, worldNormal);
    }

	outColor = vec4(gamma_correction(ACESFitted(albedo * irradiance / PI)), 1.0f);
}