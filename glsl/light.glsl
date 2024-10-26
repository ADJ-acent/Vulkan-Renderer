#define LIGHT

struct SunLight {
	vec3 DIRECTION;
	vec3 ENERGY;
	float SIN_ANGLE;
};

struct SphereLight {
	vec3 POSITION;
	float RADIUS;
	vec3 ENERGY;
	float LIMIT;
};

struct SpotLight {
	vec3 POSITION;
	vec3 DIRECTION;
	float RADIUS;
	vec3 ENERGY;
	float LIMIT;
	vec2 CONE_ANGLES;
};

vec3 calculateSunLight(SunLight light, vec3 worldNormal) {
    float cosTheta = max(dot(worldNormal, -light.DIRECTION), 0.0);
    if (cosTheta < light.SIN_ANGLE) return vec3(0.0); // Outside the sun's angular radius

    return light.ENERGY * cosTheta;
}


vec3 calculateSphereLight(SphereLight light, vec3 worldPosition, vec3 worldNormal) {
    vec3 toLight = light.POSITION - worldPosition;
    float distance = length(toLight);
    toLight /= distance;

    float attenuation = clamp(1.0 - distance / light.LIMIT, 0.0, 1.0);
    float cosTheta = max(dot(worldNormal, toLight), 0.0);

    return light.ENERGY * cosTheta * attenuation / (distance * distance);
}


vec3 calculateSpotLight(SpotLight light, vec3 worldPosition, vec3 worldNormal) {
    vec3 toLight = light.POSITION - worldPosition;
    float distance = length(toLight);
    toLight /= distance;

    // Distance attenuation
    float attenuation = clamp(1.0 - distance / light.LIMIT, 0.0, 1.0);

    // Angle attenuation based on cone
    float cosAngle = dot(toLight, -light.DIRECTION);
    float smoothFalloff = smoothstep(light.CONE_ANGLES.x, light.CONE_ANGLES.y, cosAngle);

    // Diffuse lighting
    float cosTheta = max(dot(worldNormal, toLight), 0.0);

    return light.ENERGY * cosTheta * attenuation/ (distance * distance);
}