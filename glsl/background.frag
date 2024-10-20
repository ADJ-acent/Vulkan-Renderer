#version 450
#define M_PI 3.1415926535897932384626433832795

layout(push_constant) uniform Push {
    float time;
};

layout(location = 0) in vec2 position;
layout(location = 0) out vec4 outColor;

//constructed using equation from https://forums.ni.com/t5/LabVIEW/3D-sinc-graph/td-p/394054
float sinc2D (in vec2 st) {
    float dist = distance(st, vec2(0));
    
    return dist == 0 ? 1 : sin(M_PI * dist)/(M_PI * dist);
}

void main() {
    float color1 = sinc2D((position-.5)* 20) + 0.2 + sin(time);
    vec2 pos2 = (position+ vec2(sin(time), cos(time))-.5)* 10 ;
    float color2 = sinc2D(pos2) + 0.3;
    vec2 pos3 = (position + vec2(cos(time), sin(time)) - .5) * 20;
    float color3 = sinc2D(pos3) + 0.25;

    outColor = vec4((color1*2*color2*color3) * gl_FragCoord.xy/300, 0.3, 1.0);
}