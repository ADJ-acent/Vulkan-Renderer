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
layout(set=0, binding=2) uniform sampler2D ENVIRONMENT_BRDF_LUT;

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
layout(set=2, binding=3) uniform sampler2D ROUGHNESS;
layout(set=2, binding=4) uniform sampler2D METALNESS;

layout(location=0) in vec3 position;
layout(location=1) in vec2 texCoord;
layout(location=2) in mat3 TBN;

layout(location=0) out vec4 outColor;

const float PI = 3.1415926538;

//partly from https://learnopengl.com/PBR/IBL/Specular-IBL and https://learnopengl.com/code_viewer_gh.php?code=src/6.pbr/2.2.2.ibl_specular_textured/2.2.2.pbr.fs

vec3 FresnelSchlickRoughness(float cosTheta, float roughness, vec3 F0)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
	vec3 F0 = vec3(0.04,0.04,0.04);
	vec3 albedo = texture(ALBEDO, texCoord).rgb;
	float metalness = texture(METALNESS, texCoord).r;
	//tint for metallic surface
	F0 = mix(F0, albedo, metalness);
	// Sample the normal map and convert from [0,1] to [-1,1]
    vec3 normal_rgb = texture(NORMAL, texCoord).rgb; 
    vec3 tangentNormal = normalize(normal_rgb * 2.0 - 1.0); 

    // Transform the normal from tangent space to world space
    vec3 worldNormal = TBN * tangentNormal;

	float roughness = texture(ROUGHNESS, texCoord).r * ENVIRONMENT_MIPS;

	vec3 viewDir = normalize(CAMERA_POSITION - position);
	vec3 radiance = textureLod(ENVIRONMENT, reflect(-viewDir,worldNormal), roughness).rgb;
	vec3 irradiance = textureLod(ENVIRONMENT, worldNormal, ENVIRONMENT_MIPS).rgb;

	vec2 brdfCoord = vec2(max(dot(viewDir, worldNormal), 0.0),roughness);
	vec2 environment_brdf = texture(ENVIRONMENT_BRDF_LUT, brdfCoord).rg;

	vec3 F = FresnelSchlickRoughness(max(dot(viewDir, worldNormal), 0.0), roughness, F0);
	vec3 kS = F;
	vec3 kD = 1.0 - kS;
	kD *= 1.0 - metalness;

	vec3 light_energy = vec3(0.0, 0.0, 0.0);

	for (uint i = 0; i < SUN_LIGHT_COUNT; ++i) {
        light_energy += calculateSunLight(SUNLIGHTS[i], worldNormal);
    }

    // Sphere Light Contributions
    for (uint i = 0; i < SPHERE_LIGHT_COUNT; ++i) {
        light_energy += calculateSphereLight(SPHERELIGHTS[i], position, worldNormal);
    }

    // Spot Light Contributions
    for (uint i = 0; i < SPOT_LIGHT_COUNT; ++i) {
        light_energy += calculateSpotLight(SPOTLIGHTS[i], position, worldNormal);
    }

	vec3 specular = radiance * (environment_brdf.r * F + environment_brdf.g);

	outColor = vec4(gamma_correction(ACESFitted(kD/PI * albedo * irradiance + specular + light_energy)) , 1.0f);
}