#version 460

layout(location = 0) in vec2 splatCentre;
layout(location = 1) in vec4 conicOpacity;
layout(location = 2) in vec3 splatColour;

layout(location = 0) out vec4 outColour;

void main() {
    vec2 offset = gl_FragCoord.xy - splatCentre;

    float power = -0.5 * (conicOpacity.x * offset.x * offset.x + conicOpacity.z * offset.y * offset.y) -
                  conicOpacity.y * offset.x * offset.y;
    if (power > 0.0) {
        discard;
    }

    float alpha = min(0.99, conicOpacity.w * exp(power));
    if (alpha < 0.00392156862) {
        discard;
    }

    outColour = vec4(splatColour * alpha, alpha);
}
