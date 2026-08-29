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
    vec2 extent;
    vec4 conicOpacity;
    vec4 colour;
};

layout(buffer_reference, std430) readonly buffer SplatBuffer {
    Splat splats[];
};

layout(buffer_reference, std430) buffer ProjectedBuffer {
    Projected entries[];
};

layout(buffer_reference, std430) buffer KeyBuffer {
    uint keys[];
};

layout(buffer_reference, std430) buffer IndexBuffer {
    uint indices[];
};

layout(buffer_reference, std430) buffer HistogramBuffer {
    uint counts[];
};

layout(buffer_reference, std430) buffer DispatchArguments {
    uint x;
    uint y;
    uint z;
};

layout(buffer_reference, std430) buffer DrawArguments {
    uint vertexCount;
    uint instanceCount;
    uint firstVertex;
    uint firstInstance;
};
