#version 450
#extension GL_ARB_gpu_shader_int64 : require
layout(set=0, binding=0, std430, column_major) buffer Numeric {
    double scalar;
    dvec3 vector;
    int64_t signedValue;
    uint64_t unsignedValue;
    mat3x2 matrix;
} data;
void main() {
    gl_Position = vec4(data.matrix * vec3(data.vector),
                      float(data.scalar), float(data.signedValue + int64_t(data.unsignedValue)));
}
