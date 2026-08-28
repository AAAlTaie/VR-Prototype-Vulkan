#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

struct Splat {
    vec3 position;
    float opacity;
    vec3 scale;
    float padding;
    vec4 rotation;
    vec3 colour;
    float padding2;
};

struct Projected {
    vec2 centre;
    float radius;
    float depth;
    vec4 conicOpacity;
    vec4 colour;
};

layout(buffer_reference, std430) readonly buffer SplatBuffer {
    Splat splats[];
};

layout(buffer_reference, std430) buffer ProjectedBuffer {
    Projected entries[];
};

layout(buffer_reference, std430) buffer DrawArguments {
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
};
