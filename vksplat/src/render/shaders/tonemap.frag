#version 460

layout(set = 0, binding = 0) uniform sampler2DArray hdrTarget;

layout(push_constant) uniform Constants {
    float exposure;
    uint operatorIndex;
    uint viewCount;
} constants;

layout(location = 0) in vec2 texelCoordinate;
layout(location = 0) out vec4 outColour;

vec3 reinhard(vec3 colour) {
    return colour / (colour + vec3(1.0));
}

vec3 aces(vec3 colour) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((colour * (a * colour + b)) / (colour * (c * colour + d) + e), 0.0, 1.0);
}

void main() {
    float scaled = texelCoordinate.x * float(constants.viewCount);
    float layer = min(floor(scaled), float(constants.viewCount - 1u));
    vec2 coordinate = vec2(scaled - layer, texelCoordinate.y);

    vec3 colour = texture(hdrTarget, vec3(coordinate, layer)).rgb * constants.exposure;

    if (constants.operatorIndex == 1u) {
        colour = reinhard(colour);
    } else if (constants.operatorIndex == 2u) {
        colour = aces(colour);
    }

    outColour = vec4(clamp(colour, 0.0, 1.0), 1.0);
}
