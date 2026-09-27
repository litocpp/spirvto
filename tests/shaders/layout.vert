#version 450
layout(location=0) in vec3 position;
layout(location=1) in uvec2 joints;
layout(location=0) out vec2 uv;
struct Nested { vec4 color; float gain; };
layout(set=1,binding=2,std140,row_major) uniform Params {
    mat2x3 transform;
    float weights[3];
    Nested nested;
} params;
void main() {
    uv = vec2(joints);
    gl_Position = vec4(params.transform * position.xy + params.nested.color.xyz * params.weights[1], 1);
}
