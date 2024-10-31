#define LIGHT

struct SunLight {
	vec3 DIRECTION;
	vec3 ENERGY; // divided by pi 
	float SIN_ANGLE; // sin (theta / 2)
};

struct SphereLight {
	vec3 POSITION;
	float RADIUS;
	vec3 ENERGY; // divided by pi 
	float LIMIT;
};

struct SpotLight {
	vec3 POSITION;
	uint SHADOW_SIZE;
	vec3 DIRECTION;
	float RADIUS;
	vec3 ENERGY; // divided by pi 
	float LIMIT;
	vec2 CONE_ANGLES;//cosine of the inner and outer angles
	uint SHADOW_X;
	uint SHADOW_Y;
	mat4 LIGHT_FROM_WORLD;
};

#define PI 3.1415926538