#version 450

layout(set=0,binding=0,std140) uniform World {
	vec3 SKY_DIRECTION;
	vec3 SKY_ENERGY; //energy supplied by sky to a surface patch with normal = SKY_DIRECTION
	vec3 SUN_DIRECTION;
	vec3 SUN_ENERGY; //energy supplied by sun to a surface patch with normal = SUN_DIRECTION
	vec3 CAMERA_POSITION;
	float ENVIRONMENT_MIPS;
};
layout(set=0, binding=1) uniform samplerCube ENVIRONMENT;
layout(set=2, binding=0) uniform sampler2D NORMAL;
layout(set=2, binding=1) uniform sampler2D DISPLACEMENT;
layout(set=2, binding=2) uniform sampler2D ALBEDO;
layout(set=2, binding=3) uniform sampler2D ROUGHNESS;
layout(set=2, binding=4) uniform sampler2D METALNESS;

layout(location=0) in vec3 position;
layout(location=1) in vec2 texCoord;
layout(location=2) in mat3 TBN;

layout(location=0) out vec4 outColor;

#define PI 3.1415926538

void main() {
	
	// Sample the normal map and convert from [0,1] to [-1,1]
    vec3 normal_rgb = texture(NORMAL, texCoord).rgb; 
    vec3 tangentNormal = normalize(normal_rgb * 2.0 - 1.0); 

    // Transform the normal from tangent space to world space
    vec3 worldNormal = TBN * tangentNormal;

	float roughness = texture(ROUGHNESS, texCoord).r * ENVIRONMENT_MIPS;

	vec3 viewDir = normalize(position - CAMERA_POSITION);
	vec3 radiance = textureLod(ENVIRONMENT, reflect(viewDir,worldNormal), roughness).rgb;
	vec3 albedo = radiance * (texture(ALBEDO, texCoord).rgb);
	// vec3 color = ACESFitted(radiance);
	outColor = vec4(albedo , 1.0f);
}