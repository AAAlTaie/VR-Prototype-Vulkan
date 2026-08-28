#version 460
#extension GL_GOOGLE_include_directive : require

#include "splat_common.glsl"

layout(push_constant) uniform Constants {
    vec2 viewport;
    ProjectedBuffer projected;
} constants;

layout(location = 0) out vec2 splatCentre;
layout(location = 1) out vec4 conicOpacity;
layout(location = 2) out vec3 splatColour;

void main() {
    Projected entry = constants.projected.entries[gl_InstanceIndex];

    vec2 corner = vec2((gl_VertexIndex & 1) == 0 ? -1.0 : 1.0,
                       (gl_VertexIndex & 2) == 0 ? -1.0 : 1.0);

    vec2 position = entry.centre + corner * entry.radius;

    splatCentre = entry.centre;
    conicOpacity = entry.conicOpacity;
    splatColour = entry.colour.rgb;

    gl_Position = vec4(position / constants.viewport * 2.0 - 1.0, 0.0, 1.0);
}
