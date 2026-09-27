#version 450
struct Small { float value; };
layout(set=0, binding=0, std430, row_major) buffer Packing {
    vec3 position;
    float gain;
    Small nested;
    float tail;
    mat2x3 transforms[2];
    float grid[2][3];
} data;
void main() {
    gl_Position = vec4(data.position * data.gain + data.transforms[1] * vec2(data.grid[1][2]),
                       data.nested.value + data.tail);
}
