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

layout(set=0, binding=6) uniform sampler2DShadow SHADOW_ATLAS;

layout(set=2, binding=0) uniform sampler2D NORMAL;
layout(set=2, binding=1) uniform sampler2D DISPLACEMENT;
layout(set=2, binding=2) uniform sampler2D ALBEDO;

layout(location=0) in vec3 position;
layout(location=1) in vec2 texCoord;
layout(location=2) in mat3 TBN;

layout(location=0) out vec4 outColor;

vec3 computeDirectLightDiffuse(vec3 worldNormal, vec3 albedo) {
    vec3 light_energy = vec3(0.0);

    // Sun Lights
    for (uint i = 0; i < SUN_LIGHT_COUNT; ++i) {
        SunLight light = SUNLIGHTS[i];
        vec3 L = normalize(light.DIRECTION);
        float NdotL = max(dot(worldNormal, L), -light.SIN_ANGLE);
		float factor = (NdotL + light.SIN_ANGLE) / (light.SIN_ANGLE * 2.0f);
		bool aboveHorizon = bool(floor(factor));
        light_energy += (float(aboveHorizon) * NdotL + float(!aboveHorizon) * (factor * light.SIN_ANGLE)) * light.ENERGY * (albedo );
    }

    // Sphere Lights
    for (uint i = 0; i < SPHERE_LIGHT_COUNT; ++i) {
        SphereLight light = SPHERELIGHTS[i];
        vec3 L = normalize(light.POSITION - position);
        float d = length(light.POSITION - position);
		
		vec3 e = light.ENERGY / (4 * max(d, light.RADIUS) * max(d, light.RADIUS));
        float attenuation = light.LIMIT == 0.0f ? 1.0f : max(0.0, 1.0 - pow(d / light.LIMIT, 4.0));

		if (light.RADIUS == 0.0) {
			float NdotL = max(dot(worldNormal, L), 0);
			light_energy += albedo * e * (NdotL * attenuation / PI);
		}
		else if (light.RADIUS >= d) {
			light_energy += albedo * e * (attenuation / PI);
		}
		else {
			float sinHalfTheta = light.RADIUS / d;
			float NdotL = max(dot(worldNormal, L), -sinHalfTheta);
			float factor = (NdotL + sinHalfTheta) / (sinHalfTheta * 2.0f);
			bool aboveHorizon = bool(floor(factor));
			light_energy += (float(aboveHorizon) * NdotL + float(!aboveHorizon) * (factor * sinHalfTheta)) * (albedo * e * (attenuation / PI));
		}

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

		if (light.RADIUS == 0.0) {
			float NdotL = max(dot(worldNormal, L), 0);
			light_energy += albedo * e * (shadowTerm * NdotL * attenuation * smoothFalloff / PI);
		}
		else if (light.RADIUS >= d) {
			light_energy += albedo * e * (attenuation / PI);
		}
		else {
			float sinHalfTheta = light.RADIUS / d;
			float NdotL = max(dot(worldNormal, L), -sinHalfTheta);
			float factor = (NdotL + sinHalfTheta) / (sinHalfTheta * 2.0f);
			bool aboveHorizon = bool(floor(factor));
			light_energy += (float(aboveHorizon) * NdotL + float(!aboveHorizon) * (factor * sinHalfTheta)) * (albedo * e * (shadowTerm * attenuation * smoothFalloff / PI));
		}
    }

    return light_energy;

}


void main() {

	vec3 albedo = texture(ALBEDO, texCoord).rgb;

	// Sample the normal map and convert from [0,1] to [-1,1]
    vec3 normal_rgb = texture(NORMAL, texCoord).rgb; 
    vec3 tangentNormal = normalize(normal_rgb * 2.0 - 1.0); 

    // Transform the normal from tangent space to world space
    vec3 worldNormal = TBN * tangentNormal;

	vec3 irradiance = textureLod(ENVIRONMENT, worldNormal, ENVIRONMENT_MIPS).rgb;

	vec3 light_energy = computeDirectLightDiffuse(worldNormal, albedo);

	outColor = vec4(ACESFitted(albedo * irradiance / PI) + light_energy, 1.0f);
}