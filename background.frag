#version 450

layout(location = 0) in vec2 position;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4( sin(position.x * 30 + gl_FragCoord.y / 400 ), sin(position.y * 40 + gl_FragCoord.x / 300), 0.2, 1.0);
}