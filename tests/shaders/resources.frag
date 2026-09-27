#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(set=0,binding=1) uniform sampler2D imageTexture;
layout(set=0,binding=2) uniform sampler2D unusedTexture;
layout(push_constant) uniform Push { vec4 tint; } pc;
vec4 sampleColor() { return texture(imageTexture, uv); }
void main() { color = sampleColor() * pc.tint; }
