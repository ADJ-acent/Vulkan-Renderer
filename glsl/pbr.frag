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
	uint SHADOW_ATLAS_SIZE;
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

layout(set=0, binding=6) uniform sampler2DShadow SHADOW_ATLAS;

layout(set=2, binding=0) uniform sampler2D NORMAL;
layout(set=2, binding=1) uniform sampler2D DISPLACEMENT;
layout(set=2, binding=2) uniform sampler2D ALBEDO;
layout(set=2, binding=3) uniform sampler2D ROUGHNESS;
layout(set=2, binding=4) uniform sampler2D METALNESS;

layout(location=0) in vec3 position;
layout(location=1) in vec2 texCoord;
layout(location=2) in mat3 TBN;

layout(location=0) out vec4 outColor;

//partly from https://learnopengl.com/PBR/IBL/Specular-IBL and https://learnopengl.com/code_viewer_gh.php?code=src/6.pbr/2.2.2.ibl_specular_textured/2.2.2.pbr.fs

vec3 FresnelSchlickRoughness(float cosTheta, float roughness, vec3 F0)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}


// derived from https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
// and referenced https://github.com/SaschaWillems/Vulkan/blob/master/shaders/glsl/pbrtexture/pbrtexture.frag
float SpecularD(float dotNH, float roughness)
{
	float alpha = roughness * roughness;
	float alpha2 = alpha * alpha;
	float denom = dotNH * dotNH * (alpha2 - 1.0) + 1.0;
	return (alpha2)/(PI * denom*denom); 
}

float SpecularG(float dotNL, float dotNV, float roughness)
{
	float r = (roughness + 1.0);
	float k = (r*r) / 8.0;
	float GL = dotNL / (dotNL * (1.0 - k) + k);
	float GV = dotNV / (dotNV * (1.0 - k) + k);
	return GL * GV;
}

vec3 SpecularF(float dotVH, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(2.0, (-5.55473*dotVH - 6.98316)* dotVH);
}

vec3 specularContribution(vec3 albedo, vec3 L, vec3 V, vec3 N, vec3 F0, float metalness, float roughness)
{
	// Precalculate vectors and dot products	
	vec3 H = normalize (V + L);
	float dotNH = clamp(dot(N, H), 0.0, 1.0);
	float dotNV = clamp(dot(N, V), 0.0, 1.0);
	float dotNL = clamp(dot(N, L), 0.0, 1.0);
	float dotVH = clamp(dot(V, H), 0.0, 1.0);

	vec3 color = vec3(0.0);

	if (dotNL > 0.0) {

		float D = SpecularD(dotNH, roughness); 

		float G = SpecularG(dotNL, dotNV, roughness);

		vec3 F = SpecularF(dotVH, F0);		
		vec3 spec = D * F * G / (4.0 * dotNL * dotNV + 0.001);		
		vec3 kD = (vec3(1.0) - F) * (1.0 - metalness);			
		color += (kD * albedo / PI + spec) * dotNL;
	}

	return color;
}

vec3 computeDirectLight(vec3 worldNormal, vec3 viewDir, vec3 reflectDir, vec3 albedo, float roughness, vec3 F0, float metalness) {
    vec3 light_energy = vec3(0.0);

    // Sun Lights
    for (uint i = 0; i < SUN_LIGHT_COUNT; ++i) {
        SunLight light = SUNLIGHTS[i];
        vec3 L = normalize(light.DIRECTION);
        float NdotL = max(dot(worldNormal, L), -light.SIN_ANGLE);
		float factor = (NdotL + light.SIN_ANGLE) / (light.SIN_ANGLE * 2.0f);
		bool aboveHorizon = bool(floor(factor));
        vec3 diffuse = (float(aboveHorizon) * NdotL + float(!aboveHorizon) * (factor * light.SIN_ANGLE)) * (light.ENERGY * albedo);
		light_energy += diffuse;
    }

    // Sphere Lights
    for (uint i = 0; i < SPHERE_LIGHT_COUNT; ++i) {
        SphereLight light = SPHERELIGHTS[i];
        vec3 L = normalize(light.POSITION - position);
        float d = length(light.POSITION - position);
		
		vec3 e = light.ENERGY / (4 * max(d, light.RADIUS) * max(d, light.RADIUS));
        float attenuation = light.LIMIT == 0.0f ? 1.0f : max(0.0, 1.0 - pow(d / light.LIMIT, 4.0));
		vec3 diffuse = vec3(0.0,0.0,0.0);
		if (light.RADIUS == 0.0) {
			float NdotL = max(dot(worldNormal, L), 0);
			diffuse = albedo * e * (NdotL * attenuation / PI);
		}
		else if (light.RADIUS >= d) {
			diffuse = albedo * e * (attenuation / PI);
		}
		else {
			float sinHalfTheta = light.RADIUS / d;
			float NdotL = max(dot(worldNormal, L), -sinHalfTheta);
			float factor = (NdotL + sinHalfTheta) / (sinHalfTheta * 2.0f);
			bool aboveHorizon = bool(floor(factor));
			diffuse = (float(aboveHorizon) * NdotL + float(!aboveHorizon) * (factor * sinHalfTheta)) * (albedo * e * (attenuation / PI));
		}

		//specular
		vec3 centerToRay = dot(L,reflectDir) * reflectDir - L;
		vec3 closestPoint = L + centerToRay * clamp(light.RADIUS / length(centerToRay), 0.0, 1.0);
		vec3 specular = e * attenuation * specularContribution(albedo, normalize(closestPoint), viewDir, worldNormal, F0, metalness, roughness);
		float alpha = roughness * roughness;
		float alpha_prime = clamp(alpha + light.RADIUS / 2 * d, 0.0001, 1.0);
		specular *= (alpha * alpha / (alpha_prime * alpha_prime));

		light_energy += diffuse + specular;
    }
	
    // Spot Lights
    for (uint i = 0; i < SPOT_LIGHT_COUNT; ++i) {
        SpotLight light = SPOTLIGHTS[i];

		float shadowTerm = 1.0f;
		//calculate shadow
		if (light.SHADOW_SIZE > 0) {
			vec4 lightSpacePositionHomogenous = light.LIGHT_FROM_WORLD * vec4(position, 1.0);
			shadowTerm = textureProj(SHADOW_ATLAS, lightSpacePositionHomogenous);
		}

        vec3 L = normalize(light.POSITION - position);
        float d = length(light.POSITION - position);

		vec3 e = light.ENERGY / (4 * max(d, light.RADIUS) * max(d, light.RADIUS));
        float attenuation = light.LIMIT == 0.0f ? 1.0f : max(0.0, 1.0 - pow(d / light.LIMIT, 4.0));

        float angleToLight = acos(dot(L, light.DIRECTION));

		float smoothFalloff = 1.0;
		if (light.CONE_ANGLES.x == light.CONE_ANGLES.y) {
			smoothFalloff = angleToLight <= light.CONE_ANGLES.y ? 1.0f : 0.0f;
		}
		else  {
        	float angleToLightClamped = clamp(angleToLight, light.CONE_ANGLES.x, light.CONE_ANGLES.y);
    		smoothFalloff = (angleToLightClamped - light.CONE_ANGLES.y) / (light.CONE_ANGLES.x - light.CONE_ANGLES.y);
 		}

		vec3 diffuse = vec3(0.0,0.0,0.0);
		if (light.RADIUS == 0.0) {
			float NdotL = max(dot(worldNormal, L), 0);
			diffuse = albedo * e * (shadowTerm * NdotL * attenuation * smoothFalloff / PI);
		}
		else if (light.RADIUS >= d) {
			diffuse = albedo * e * (attenuation / PI);
		}
		else {
			float sinHalfTheta = light.RADIUS / d;
			float NdotL = max(dot(worldNormal, L), -sinHalfTheta);
			float factor = (NdotL + sinHalfTheta) / (sinHalfTheta * 2.0f);
			bool aboveHorizon = bool(floor(factor));
			diffuse = (float(aboveHorizon) * NdotL + float(!aboveHorizon) * (factor * sinHalfTheta)) * (albedo * e * (shadowTerm * attenuation * smoothFalloff / PI));
		}

		//specular
		vec3 centerToRay = dot(L,reflectDir) * reflectDir - L;
		vec3 closestPoint = L + centerToRay * clamp(light.RADIUS / length(centerToRay), 0.0, 1.0);
		vec3 specular = e * smoothFalloff * attenuation * shadowTerm * specularContribution(albedo, normalize(closestPoint), viewDir, worldNormal, F0, metalness, roughness);
		float alpha = roughness * roughness;
		float alpha_prime = clamp(alpha + light.RADIUS / 2 * d, 0.0001, 1.0);
		specular *= (alpha * alpha / (alpha_prime * alpha_prime));

		light_energy += diffuse + specular;
    }	

    return light_energy;

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

	float roughness = texture(ROUGHNESS, texCoord).r;

	vec3 viewDir = normalize(CAMERA_POSITION - position);
	vec3 reflectDir = reflect(-viewDir,worldNormal);
	vec3 radiance = textureLod(ENVIRONMENT, reflectDir, roughness * ENVIRONMENT_MIPS).rgb;
	vec3 irradiance = textureLod(ENVIRONMENT, worldNormal, ENVIRONMENT_MIPS).rgb;

	vec2 brdfCoord = vec2(max(dot(viewDir, worldNormal), 0.0),roughness);
	vec2 environment_brdf = texture(ENVIRONMENT_BRDF_LUT, brdfCoord).rg;

	vec3 F = FresnelSchlickRoughness(max(dot(viewDir, worldNormal), 0.0), roughness, F0);
	vec3 kS = F;
	vec3 kD = 1.0 - kS;
	kD *= 1.0 - metalness;

	vec3 light_energy = computeDirectLight(worldNormal, viewDir, reflectDir, albedo, roughness, F0, metalness);

	
	vec3 specular = radiance * (environment_brdf.r * F + environment_brdf.g);

	outColor = vec4(ACESFitted(kD/PI * albedo * irradiance + specular) + light_energy, 1.0f);
}