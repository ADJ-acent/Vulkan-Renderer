#define LIGHT

struct SunLight {
	vec3 DIRECTION;
	vec3 ENERGY;
	float SIN_ANGLE; // sin (theta / 2)
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
	vec2 CONE_ANGLES;//cosine of the inner and outer angles
};

#define PI 3.1415926538