#version 450

layout(push_constant) uniform Push {
    float time;
};

layout(location = 0) in vec2 position;
layout(location = 0) out vec4 outColor;

// 2D Random from the book of shaders: https://thebookofshaders.com/11/
float random (in vec2 st) {
    return fract(sin(dot(st.xy,
                         vec2(12.9898,78.233)))
                 * 43758.5453123);
}

// 2D Noise from the book of shaders: https://thebookofshaders.com/11/
float noise (in vec2 st) {
    vec2 i = floor(st);
    vec2 f = fract(st);

    // Four corners in 2D of a tile
    float a = random(i);
    float b = random(i + vec2(1.0, 0.0));
    float c = random(i + vec2(0.0, 1.0));
    float d = random(i + vec2(1.0, 1.0));

    // Smooth Interpolation

    // Cubic Hermine Curve.  Same as SmoothStep()
    vec2 u = f*f*(3.0-2.0*f);

    // Mix 4 coorners percentages
    return mix(a, b, u.x) +
            (c - a)* u.y * (1.0 - u.x) +
            (d - b) * u.x * u.y;
}

void main() {
    float noisedValue = noise(position * 20 + sin(time ) * 20);
    float step1 = step(0.8, noisedValue);
    float step2 = step(0.3, noisedValue);
    vec3 color1 = vec3(0.098, 0, 0.2784);
    vec3 color2 = vec3(0.1255, 0.294, 0.6118);
    vec3 color3 = vec3(0.247, 0.8745, 0.769);

    //composite the colors
    vec3 finalColor = max(max(color3 * step1, color2 * step2), color1);

    outColor = vec4(finalColor, 1.0);
}